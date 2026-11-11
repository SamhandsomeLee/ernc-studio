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
using ernc::application::WriteCommandResult;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::SessionId;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;

/// 长度前缀的一槽 FC03 响应，CRC 已含在末尾两个字节。数据区是 0x78 0x56 0x34 0x12。
/// 它对应 Read、firstSlot 0、slotCount 1、从站 0x01。期望字节是字面量，不由编码器生成。
constexpr std::uint8_t kOneSlotResponse[] = {0x09, 0x01, 0x03, 0x04, 0x78, 0x56, 0x34, 0x12, 0x94, 0x4E};

/// kOneSlotResponse 的数据区。四个字节保持线上顺序，不是参数 deviceValue。
constexpr std::uint8_t kSlotData[] = {0x78, 0x56, 0x34, 0x12};

/// 长度前缀的一槽 FC16 ACK。起始寄存器 0，寄存器数量 2。CRC 已含在末尾，不在测试里重算。
constexpr std::uint8_t kOneSlotAck[] = {0x08, 0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x41, 0xC8};

/// 半帧用例按前 4 字节和后 6 字节切开。这里只固定字面量长度。
static_assert(sizeof(kOneSlotResponse) == 10);
static_assert(sizeof(kSlotData) == 4);
static_assert(sizeof(kOneSlotAck) == 9);

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

/// @brief 两个缓冲区的长度和每个字节都相同。
/// @param actual 被测输出，只在本次调用期间借用。
/// @param expected 字面量期望，只在本次调用期间借用。
/// @return 完全一致时为 true。
bool sameBytes(std::span<const std::uint8_t> actual, std::span<const std::uint8_t> expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (actual[index] != expected[index]) {
            return false;
        }
    }
    return true;
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
    return connected.ok() && connected.tryError() == nullptr && connected.tryValue() != nullptr;
}

/// @brief 做成一条读命令。payload 留空，从站地址是已确认的 0x01。
/// @param firstSlot 起始逻辑 slot。0 表示起始寄存器 0。
/// @param slotCount 连续 slot 数。0 会在入队时被拒绝。
/// @param priority 等待队列中的优先级。High 不能越过已经交给端口的命令。
/// @return 尚未入队的请求。调用方拥有这份对象。
CommandRequest readRequest(std::uint16_t firstSlot, std::uint16_t slotCount, CommandPriority priority) {
    CommandRequest request;
    request.op = CommandOp::Read;
    request.priority = priority;
    request.firstSlot = firstSlot;
    request.slotCount = slotCount;
    request.slaveAddress = 0x01;
    return request;
}

/// @brief 入队一条读命令，并在成功时复制出命令代号。
/// @param scheduler 被测调度器，只在本次调用期间借用。
/// @param firstSlot 起始逻辑 slot。0 表示起始寄存器 0。
/// @param slotCount 连续 slot 数。
/// @param priority 等待队列优先级。
/// @param id 成功时写入新代号。失败时不修改。调用方拥有这份副本。
/// @return 入队成功且代号有效时为 true。本函数不调用 error()。
bool enqueueRead(CommandScheduler& scheduler, std::uint16_t firstSlot, std::uint16_t slotCount,
                 CommandPriority priority, CommandId& id) {
    const Result<CommandId> result = scheduler.enqueue(readRequest(firstSlot, slotCount, priority));
    const CommandId* held = nullptr;
    if (!holdsCommand(result, held)) {
        return false;
    }
    id = *held;
    return true;
}

/// @brief 长度前缀帧的第 3、4 字节等于给定的大端起始寄存器。
/// @param frame 一次 submit 收下的整帧，只在本次调用期间借用。下标 0 是长度字节。
/// @param high 起始寄存器高字节。
/// @param low 起始寄存器低字节。slot 0 为 0x00，slot 1 为 0x02，slot 2 为 0x04。
/// @return 帧长至少 5 且这两个下标匹配时为 true。
bool startRegisterIs(std::span<const std::uint8_t> frame, std::uint8_t high, std::uint8_t low) {
    return frame.size() > 4 && frame[3] == high && frame[4] == low;
}

/// @brief 取走的结果是读数据，且数据区与字面量相同。
/// @param outcome 调度器交出的结果，只在本次调用期间借用。
/// @param expected 期望数据区，只在本次调用期间借用。不由编码器生成。
/// @return body 为 ReadCommandResult 且逐字节相同时为 true。
bool readPayloadIs(const CommandOutcome& outcome, std::span<const std::uint8_t> expected) {
    const ReadCommandResult* read = std::get_if<ReadCommandResult>(&outcome.body);
    if (read == nullptr) {
        return false;
    }
    return sameBytes(read->payload, expected);
}

/// @brief 取走的结果是断连失败。
/// @param outcome 调度器交出的结果，只在本次调用期间借用。
/// @return body 为 FailedCommand 且 code 为 DeviceDisconnected 时为 true。
bool failedDisconnected(const CommandOutcome& outcome) {
    const FailedCommand* failed = std::get_if<FailedCommand>(&outcome.body);
    return failed != nullptr && failed->code == ErrorCode::DeviceDisconnected;
}

/// @brief 取走的结果是写回显，不是读数据。
/// @param outcome 调度器交出的结果，只在本次调用期间借用。
/// @return body 不是 ReadCommandResult，而是 WriteCommandResult，且起始寄存器为 0、寄存器数量为 2 时为 true。
/// 这只表示 ACK 回显，不是读回 Verified。
bool writeAckIsUnverified(const CommandOutcome& outcome) {
    if (std::get_if<ReadCommandResult>(&outcome.body) != nullptr) {
        return false;
    }
    const WriteCommandResult* ack = std::get_if<WriteCommandResult>(&outcome.body);
    return ack != nullptr && ack->startRegister == 0 && ack->registerCount == 2;
}

/// @brief 两条读都入队，但只有第一条交给端口。
/// @return 两条结果成功、代号不同、只提交一帧且第一条尚无结果时为 true。
bool twoEnqueuesOneSubmit() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("two_enqueues_one_submit");
    }
    CommandScheduler scheduler(session, fake);

    CommandId first = CommandId::none();
    CommandId second = CommandId::none();
    if (!enqueueRead(scheduler, 0, 1, CommandPriority::Normal, first) ||
        !enqueueRead(scheduler, 1, 1, CommandPriority::Normal, second)) {
        return fail("two_enqueues_one_submit");
    }
    if (!first.valid() || !second.valid() || first == second || first.raw() == second.raw()) {
        return fail("two_enqueues_one_submit");
    }
    if (fake.submitted.size() != 1 || scheduler.inFlightCount() != 1) {
        return fail("two_enqueues_one_submit");
    }
    if (scheduler.takeResult(first).has_value()) {
        return fail("two_enqueues_one_submit");
    }
    return true;
}

/// @brief 第一帧结束后提交下一条。第二帧第 3、4 字节是 slot 1 的起始寄存器 0x00 0x02。
/// @return 数据区为 kSlotData、已提交两帧且仍有一条在途时为 true。
bool responseReleasesNext() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("response_releases_next");
    }
    CommandScheduler scheduler(session, fake);

    CommandId first = CommandId::none();
    CommandId second = CommandId::none();
    if (!enqueueRead(scheduler, 0, 1, CommandPriority::Normal, first) ||
        !enqueueRead(scheduler, 1, 1, CommandPriority::Normal, second)) {
        return fail("response_releases_next");
    }
    fake.queueBytes(kOneSlotResponse);
    scheduler.pump();

    const std::optional<CommandOutcome> outcome = scheduler.takeResult(first);
    if (!outcome.has_value() || outcome->id != first || !readPayloadIs(*outcome, kSlotData)) {
        return fail("response_releases_next");
    }
    if (fake.submitted.size() != 2 || !startRegisterIs(fake.submitted[1], 0x00, 0x02)) {
        return fail("response_releases_next");
    }
    if (scheduler.inFlightCount() != 1) {
        return fail("response_releases_next");
    }
    return true;
}

/// @brief 在途命令未结束时 High 只排在等待队列里。响应后下一条是 slot 2，起始寄存器 0x00 0x04。
/// @return 第二帧低字节为 0x04 时为 true。0x02 表示错误地先发了 slot 1。
bool highWaitsBehindInflight() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("high_waits_behind_inflight");
    }
    CommandScheduler scheduler(session, fake);

    CommandId slot0 = CommandId::none();
    CommandId slot1 = CommandId::none();
    CommandId slot2 = CommandId::none();
    if (!enqueueRead(scheduler, 0, 1, CommandPriority::Normal, slot0) ||
        !enqueueRead(scheduler, 1, 1, CommandPriority::Normal, slot1) ||
        !enqueueRead(scheduler, 2, 1, CommandPriority::High, slot2)) {
        return fail("high_waits_behind_inflight");
    }
    if (!slot0.valid() || !slot1.valid() || !slot2.valid()) {
        return fail("high_waits_behind_inflight");
    }
    fake.queueBytes(kOneSlotResponse);
    scheduler.pump();
    if (fake.submitted.size() != 2 || !startRegisterIs(fake.submitted[1], 0x00, 0x04)) {
        return fail("high_waits_behind_inflight");
    }
    return true;
}

/// @brief 断连后在途和等待中的读都以 DeviceDisconnected 结束，不再提交第二帧。
/// @return 在途数量为 0、提交数仍为 1，且两条结果都是该失败码时为 true。
bool disconnectDropsInflight() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("disconnect_drops_inflight");
    }
    CommandScheduler scheduler(session, fake);

    CommandId first = CommandId::none();
    CommandId second = CommandId::none();
    if (!enqueueRead(scheduler, 0, 1, CommandPriority::Normal, first) ||
        !enqueueRead(scheduler, 1, 1, CommandPriority::Normal, second)) {
        return fail("disconnect_drops_inflight");
    }
    const Result<void> disconnected = session.disconnect();
    if (!disconnected.ok() || disconnected.tryError() != nullptr) {
        return fail("disconnect_drops_inflight");
    }
    scheduler.pump();
    if (scheduler.inFlightCount() != 0 || fake.submitted.size() != 1) {
        return fail("disconnect_drops_inflight");
    }
    const std::optional<CommandOutcome> firstOutcome = scheduler.takeResult(first);
    const std::optional<CommandOutcome> secondOutcome = scheduler.takeResult(second);
    if (!firstOutcome.has_value() || !failedDisconnected(*firstOutcome) || !secondOutcome.has_value() ||
        !failedDisconnected(*secondOutcome)) {
        return fail("disconnect_drops_inflight");
    }
    return true;
}

/// @brief slot 数为 0 的读在入队时被拒绝，不占用在途名额。
/// @return 错误码为 InvalidParameter、在途数量为 0 且没有提交时为 true。
bool emptyReadRejected() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("empty_read_rejected");
    }
    CommandScheduler scheduler(session, fake);

    const Result<CommandId> rejected = scheduler.enqueue(readRequest(0, 0, CommandPriority::Normal));
    const ErrorCode* error = rejected.tryError();
    if (rejected.ok() || error == nullptr || *error != ErrorCode::InvalidParameter) {
        return fail("empty_read_rejected");
    }
    if (scheduler.inFlightCount() != 0 || !fake.submitted.empty()) {
        return fail("empty_read_rejected");
    }
    return true;
}

/// @brief FC16 ACK 只形成写回显。起始寄存器 0，寄存器数量 2，不能变成读结果。
/// @return body 为 WriteCommandResult 且不是 ReadCommandResult 时为 true。
bool writeAckIsNotVerified() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("write_ack_is_not_verified");
    }
    CommandScheduler scheduler(session, fake);

    CommandRequest request;
    request.op = CommandOp::Write;
    request.priority = CommandPriority::Normal;
    request.firstSlot = 0;
    request.slotCount = 1;
    request.slaveAddress = 0x01;
    request.payload = {0x78, 0x56, 0x34, 0x12};
    const Result<CommandId> result = scheduler.enqueue(request);
    const CommandId* held = nullptr;
    if (!holdsCommand(result, held)) {
        return fail("write_ack_is_not_verified");
    }
    const CommandId id = *held;
    fake.queueBytes(kOneSlotAck);
    scheduler.pump();

    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    if (!outcome.has_value() || !writeAckIsUnverified(*outcome)) {
        return fail("write_ack_is_not_verified");
    }
    return true;
}

/// @brief 只喂入响应前四个字节时命令仍在途；补上后六个字节后才交出数据区。
/// @return 半帧期间没有结果，补齐后 payload 为 kSlotData 且在途数量为 0 时为 true。
bool halfFrameStaysInflight() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("half_frame_stays_inflight");
    }
    CommandScheduler scheduler(session, fake);

    CommandId id = CommandId::none();
    if (!enqueueRead(scheduler, 0, 1, CommandPriority::Normal, id)) {
        return fail("half_frame_stays_inflight");
    }
    fake.queueBytes(std::span<const std::uint8_t>(kOneSlotResponse, 4));
    scheduler.pump();
    if (scheduler.inFlightCount() != 1 || scheduler.takeResult(id).has_value()) {
        return fail("half_frame_stays_inflight");
    }
    fake.queueBytes(std::span<const std::uint8_t>(kOneSlotResponse + 4, sizeof(kOneSlotResponse) - 4));
    scheduler.pump();

    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    if (!outcome.has_value() || !readPayloadIs(*outcome, kSlotData) || scheduler.inFlightCount() != 0) {
        return fail("half_frame_stays_inflight");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "two_enqueues_one_submit") {
        return twoEnqueuesOneSubmit();
    }
    if (name == "response_releases_next") {
        return responseReleasesNext();
    }
    if (name == "high_waits_behind_inflight") {
        return highWaitsBehindInflight();
    }
    if (name == "disconnect_drops_inflight") {
        return disconnectDropsInflight();
    }
    if (name == "empty_read_rejected") {
        return emptyReadRejected();
    }
    if (name == "write_ack_is_not_verified") {
        return writeAckIsNotVerified();
    }
    if (name == "half_frame_stays_inflight") {
        return halfFrameStaysInflight();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个命令调度器单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_scheduler_tests <case>\n";
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
