#include "cancellation.hpp"
#include "scheduler.hpp"
#include "session.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using ernc::application::CommandId;
using ernc::application::CommandOp;
using ernc::application::CommandOutcome;
using ernc::application::CommandPriority;
using ernc::application::CommandRequest;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::FailedCommand;
using ernc::application::ReadCommandResult;
using ernc::application::disposeFrame;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::SessionId;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;
using std::uint8_t;

/// 长度前缀的一槽 FC03 响应，CRC 已含在末尾两个字节。数据区是 0x78 0x56 0x34 0x12。
/// 它对应 Read、firstSlot 0、slotCount 1、从站 0x01。期望字节是字面量，不由编码器生成。
constexpr uint8_t kOneSlotResponse[] = {0x09, 0x01, 0x03, 0x04, 0x78, 0x56, 0x34, 0x12, 0x94, 0x4E};

static_assert(sizeof(kOneSlotResponse) == 10);

/// @brief 供调度器使用的假端口。不打开串口，也不使用 QSerialPort。
///
/// 新建时为关闭。测试先构造本对象，再构造 DeviceSession 和 CommandScheduler。三者共用这一条端口。
class Fake final : public IDeviceTransport {
public:
    /// @brief 构造为关闭。提交记录和步骤队列都为空。
    Fake() = default;

    /// @brief 打开假端口。
    /// @return 进入打开并成功。成功只表示端口被收下，还没有设备响应。
    Result<void> open() override {
        open_ = true;
        return Result<void>::success();
    }

    /// @brief 关闭假端口。
    /// @return 已经关闭时成功，并且不排步骤。
    /// 从打开状态关闭时清掉打开标记，并只排一个 Kind::Closed。
    Result<void> close() override {
        if (!open_) {
            return Result<void>::success();
        }
        open_ = false;
        TransportStep step;
        step.kind = TransportStep::Kind::Closed;
        queued_.push_back(std::move(step));
        return Result<void>::success();
    }

    /// @brief 收下一段线字节。
    /// @param bytes 调用方的线字节，只在本次调用期间借用。函数保存自己的副本。
    /// @return 关闭时为 DeviceDisconnected，并且不追加。否则成功。成功不是设备响应。
    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        if (!open_) {
            return Result<void>::failure(ErrorCode::DeviceDisconnected);
        }
        submitted.emplace_back(bytes.begin(), bytes.end());
        return Result<void>::success();
    }

    /// @brief 取走队列中的下一步。
    /// @return 有排队步骤时弹出队首。没有时为 Idle，bytes 为空，error 为空。
    TransportStep poll() override {
        if (queued_.empty()) {
            return TransportStep{};
        }
        TransportStep step = std::move(queued_.front());
        queued_.erase(queued_.begin());
        return step;
    }

    /// @brief 排入一段已收到的线字节，供之后的 poll 交还。
    /// @param bytes 线字节，只在本次调用期间借用。函数复制进 Kind::Bytes，error 保持为空。
    void queueBytes(std::span<const std::uint8_t> bytes) {
        TransportStep step;
        step.kind = TransportStep::Kind::Bytes;
        step.bytes.assign(bytes.begin(), bytes.end());
        queued_.push_back(std::move(step));
    }

    /// 已收下的线字节。每个元素是一次 submit 的整帧副本。关闭时的 submit 不会追加。
    std::vector<std::vector<std::uint8_t>> submitted;

private:
    /// 当前是否打开。新建时为 false。从打开状态 close 后回到 false。
    bool open_{false};

    /// poll 待弹出的步骤。关闭状态下的 close 不往这里追加。
    std::vector<TransportStep> queued_;
};

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 入队结果成功，并交出内部命令代号。
/// @param result 被测返回值，只在本次调用期间借用。本函数不调用 error()。
/// @param id 成功时指向结果内部的代号。指针不转移所有权，只在 result 存活期间有效。
/// @return ok() 为 true、tryError() 为空且 valid() 为 true 时为 true。
bool holdsCommand(const Result<CommandId>& result, const CommandId*& id) {
    id = result.tryValue();
    return result.ok() && result.tryError() == nullptr && id != nullptr && id->valid();
}

/// @brief 打开会话，供随后的 enqueue 使用。
/// @param session 已绑定假端口的会话，只在本次调用期间借用。
/// @return connect 成功且 tryError() 为空时为 true。本函数不调用 error()。
bool connectSession(DeviceSession& session) {
    const Result<SessionId> connected = session.connect();
    return connected.ok() && connected.tryError() == nullptr && connected.tryValue() != nullptr &&
           connected.tryValue()->valid();
}

/// @brief 做成一条一槽读命令。不编码线上帧，也不计算 CRC。
/// @return 尚未入队的请求。op 为 Read，firstSlot 为 0，slotCount 为 1，从站地址为 0x01。
/// payload 留空。调用方拥有这份对象。
CommandRequest oneSlotRead() {
    CommandRequest request;
    request.op = CommandOp::Read;
    request.priority = CommandPriority::Normal;
    request.firstSlot = 0;
    request.slotCount = 1;
    request.slaveAddress = 0x01;
    return request;
}

/// @brief 入队一条一槽读命令，并在成功时复制出命令代号。
/// @param scheduler 被测调度器，只在本次调用期间借用。
/// @param id 成功时写入新代号。失败时不修改。调用方拥有这份副本。
/// @return 入队成功且代号有效时为 true。本函数不调用 error()。
bool enqueueOneSlotRead(CommandScheduler& scheduler, CommandId& id) {
    const Result<CommandId> result = scheduler.enqueue(oneSlotRead());
    const CommandId* held = nullptr;
    if (!holdsCommand(result, held)) {
        return false;
    }
    id = *held;
    return true;
}

/// @brief 核对 disposeFrame 对这一对标志的丢弃决定。
/// @param discardNextFrame 调度器是否还欠一帧旧响应。取消或断连了已发出的命令时为 true。
/// @param commandStillCurrent 当前是否仍有一条在途命令可以接收这一帧。
/// @param expectDrop 期望的 dropFrame。true 表示这一帧不能写成任何命令的成功结果。
/// @return 实际 dropFrame 与 expectDrop 相同时为 true。本函数不读取端口。
bool dropMatches(bool discardNextFrame, bool commandStillCurrent, bool expectDrop) {
    return disposeFrame(discardNextFrame, commandStillCurrent).dropFrame == expectDrop;
}

/// @brief 取走的结果是指定失败，并且不是读数据。
/// @param outcome 调度器交出的结果，只在本次调用期间借用。
/// @param code 期望的失败类别。OperationCancelled 与 DeviceDisconnected 都是失败，不是读成功。
/// @return body 为 FailedCommand、code 匹配，且不是 ReadCommandResult 时为 true。
bool failedAs(const CommandOutcome& outcome, ErrorCode code) {
    if (std::get_if<ReadCommandResult>(&outcome.body) != nullptr) {
        return false;
    }
    const FailedCommand* failed = std::get_if<FailedCommand>(&outcome.body);
    return failed != nullptr && failed->code == code;
}

/// @brief 欠一帧或已经没有当前命令时丢掉这一帧；两者都不是时才留下。
/// @return 四组标志的 dropFrame 都符合上述规则时为 true。
bool disposeDropsStaleFrame() {
    if (!dropMatches(true, true, true)) {
        return fail("dispose_drops_stale_frame");
    }
    if (!dropMatches(true, false, true)) {
        return fail("dispose_drops_stale_frame");
    }
    if (!dropMatches(false, false, true)) {
        return fail("dispose_drops_stale_frame");
    }
    if (!dropMatches(false, true, false)) {
        return fail("dispose_drops_stale_frame");
    }
    return true;
}

/// @brief 取消后不再提交还在等待的下一条读。
/// @return 取消后在途数量为 0，空泵之后提交数仍为 1，且两条结果都是 OperationCancelled 时为 true。
bool cancelDoesNotSendTheRest() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("cancel_does_not_send_the_rest");
    }
    CommandScheduler scheduler(session, fake);

    CommandId first = CommandId::none();
    CommandId second = CommandId::none();
    if (!enqueueOneSlotRead(scheduler, first) || !enqueueOneSlotRead(scheduler, second)) {
        return fail("cancel_does_not_send_the_rest");
    }
    if (!first.valid() || !second.valid() || first == second) {
        return fail("cancel_does_not_send_the_rest");
    }
    if (fake.submitted.size() != 1 || scheduler.inFlightCount() != 1) {
        return fail("cancel_does_not_send_the_rest");
    }

    scheduler.cancel();
    if (scheduler.inFlightCount() != 0) {
        return fail("cancel_does_not_send_the_rest");
    }
    scheduler.pump();
    if (fake.submitted.size() != 1) {
        return fail("cancel_does_not_send_the_rest");
    }

    const std::optional<CommandOutcome> firstOutcome = scheduler.takeResult(first);
    const std::optional<CommandOutcome> secondOutcome = scheduler.takeResult(second);
    if (!firstOutcome.has_value() || firstOutcome->id != first ||
        !failedAs(*firstOutcome, ErrorCode::OperationCancelled)) {
        return fail("cancel_does_not_send_the_rest");
    }
    if (!secondOutcome.has_value() || secondOutcome->id != second ||
        !failedAs(*secondOutcome, ErrorCode::OperationCancelled)) {
        return fail("cancel_does_not_send_the_rest");
    }
    return true;
}

/// @brief 取消之后才到齐的整帧不能把该命令改成读成功。
/// @return 结果仍是 OperationCancelled，且在途数量为 0 时为 true。
bool lateFrameAfterCancelStaysCancelled() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("late_frame_after_cancel_stays_cancelled");
    }
    CommandScheduler scheduler(session, fake);

    CommandId id = CommandId::none();
    if (!enqueueOneSlotRead(scheduler, id) || !id.valid()) {
        return fail("late_frame_after_cancel_stays_cancelled");
    }
    scheduler.cancel();

    fake.queueBytes(kOneSlotResponse);
    scheduler.pump();

    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    if (!outcome.has_value() || outcome->id != id || !failedAs(*outcome, ErrorCode::OperationCancelled) ||
        scheduler.inFlightCount() != 0) {
        return fail("late_frame_after_cancel_stays_cancelled");
    }
    return true;
}

/// @brief 旧会话发出的读，其迟到帧不能完成断连后新会话上的读。
///
/// 只调用一次 pump。旧命令以 DeviceDisconnected 结束，结果里的会话代号不是新会话。
/// 新命令此时仍无结果，在途数量为 1。这次 pump 可以在丢掉旧帧之后提交新的读，因此不检查提交次数。
/// @return 上述会话、失败码和在途数量都成立时为 true。
bool lateFrameDoesNotUpdateNewSession() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("late_frame_does_not_update_new_session");
    }
    const SessionId oldSession = session.currentId();
    CommandScheduler scheduler(session, fake);

    CommandId oldId = CommandId::none();
    if (!oldSession.valid() || !enqueueOneSlotRead(scheduler, oldId) || !oldId.valid()) {
        return fail("late_frame_does_not_update_new_session");
    }

    const Result<void> disconnected = session.disconnect();
    if (!disconnected.ok() || disconnected.tryError() != nullptr) {
        return fail("late_frame_does_not_update_new_session");
    }
    if (!connectSession(session)) {
        return fail("late_frame_does_not_update_new_session");
    }
    const SessionId newSession = session.currentId();
    if (!newSession.valid() || newSession == oldSession) {
        return fail("late_frame_does_not_update_new_session");
    }

    CommandId newId = CommandId::none();
    if (!enqueueOneSlotRead(scheduler, newId) || !newId.valid() || newId == oldId) {
        return fail("late_frame_does_not_update_new_session");
    }

    fake.queueBytes(kOneSlotResponse);
    scheduler.pump();

    const std::optional<CommandOutcome> oldOutcome = scheduler.takeResult(oldId);
    if (!oldOutcome.has_value() || oldOutcome->id != oldId ||
        !failedAs(*oldOutcome, ErrorCode::DeviceDisconnected) || oldOutcome->session != oldSession ||
        oldOutcome->session == newSession) {
        return fail("late_frame_does_not_update_new_session");
    }
    if (scheduler.takeResult(newId).has_value() || scheduler.inFlightCount() != 1) {
        return fail("late_frame_does_not_update_new_session");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。未知名称时设为 false。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "dispose_drops_stale_frame") {
        return disposeDropsStaleFrame();
    }
    if (name == "cancel_does_not_send_the_rest") {
        return cancelDoesNotSendTheRest();
    }
    if (name == "late_frame_after_cancel_stays_cancelled") {
        return lateFrameAfterCancelStaysCancelled();
    }
    if (name == "late_frame_does_not_update_new_session") {
        return lateFrameDoesNotUpdateNewSession();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个取消与过期帧单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对或用例名未知时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_cancellation_tests <case>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
