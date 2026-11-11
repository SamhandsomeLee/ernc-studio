#include "retry_policy.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "write_verification.hpp"

#include <chrono>
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
using ernc::application::ReadRetryLimit;
using ernc::application::RetryTime;
using ernc::application::WriteCommandResult;
using ernc::application::WritePhase;
using ernc::application::WriteVerification;
using ernc::application::compareWriteReadBack;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::SessionId;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;

/// 长度前缀的一槽 FC16 ACK。起始寄存器 0，寄存器数量 2。
/// CRC 已含在末尾两个字节。这是字面量，测试不调用 encodeFc16Write，也不重算 CRC。
constexpr std::uint8_t kOneSlotAck[] = {0x08, 0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x41, 0xC8};

/// 长度前缀的一槽 FC03 响应，CRC 已含在末尾两个字节。数据区是 0x78 0x56 0x34 0x12。
/// 它对应 firstSlot 0、slotCount 1、从站 0x01。期望字节是字面量，不调用 encodeFc03Read。
constexpr std::uint8_t kOneSlotResponse[] = {0x09, 0x01, 0x03, 0x04, 0x78, 0x56, 0x34, 0x12, 0x94, 0x4E};

/// kOneSlotResponse 的数据区。四个字节保持线上顺序，不是参数 deviceValue。
constexpr std::uint8_t kSlotData[] = {0x78, 0x56, 0x34, 0x12};

/// 对照失败用例的写入字节。最后一字节与读回字节不同，读回不得覆盖它。
constexpr std::uint8_t kMismatchTarget[] = {0x11, 0x22, 0x33, 0x44};

/// 对照失败用例的读回字节。最后一字节是 0x55。
constexpr std::uint8_t kMismatchActual[] = {0x11, 0x22, 0x33, 0x55};

/// 测试夹具里的一次等待长度。100 毫秒只用于推进模拟时刻，不是设备超时基线。
constexpr std::chrono::milliseconds kFixtureTimeout{100};

/// 这些字面量的长度是固定的。功能码在长度前缀帧的下标 2：FC16 为 0x10，FC03 为 0x03。
static_assert(sizeof(kOneSlotAck) == 9);
static_assert(sizeof(kOneSlotResponse) == 10);
static_assert(sizeof(kSlotData) == 4);
static_assert(sizeof(kMismatchTarget) == 4);
static_assert(sizeof(kMismatchActual) == 4);
static_assert(kOneSlotAck[2] == 0x10);
static_assert(kOneSlotResponse[2] == 0x03);

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

/// @brief 长度前缀帧的功能码位于下标 2。
/// @param frame 一次 submit 收下的整帧，只在本次调用期间借用。下标 0 是长度字节。
/// @param functionCode 功能码。FC16 为 0x10，FC03 为 0x03。期望值是字面量，不由编码器生成。
/// @return 帧长大于 2 且该下标等于 functionCode 时为 true。
bool functionCodeAt(std::span<const std::uint8_t> frame, std::uint8_t functionCode) {
    return frame.size() > 2 && frame[2] == functionCode;
}

/// @brief 入队一条一槽写命令。payload 是 kSlotData 的副本。
/// @param scheduler 被测调度器，只在本次调用期间借用。
/// @param readBack 为 true 时，匹配 ACK 之后还要读同一范围。为 false 时 ACK 本身结束命令。
/// @param id 成功时写入新代号。失败时不修改。调用方拥有这份副本。
/// @return 入队成功且代号有效时为 true。本函数不调用 error()。
bool enqueueOneSlotWrite(CommandScheduler& scheduler, bool readBack, CommandId& id) {
    CommandRequest request;
    request.op = CommandOp::Write;
    request.priority = CommandPriority::Normal;
    request.firstSlot = 0;
    request.slotCount = 1;
    request.payload.assign(kSlotData, kSlotData + sizeof(kSlotData));
    request.readBack = readBack;
    request.slaveAddress = 0x01;
    const Result<CommandId> result = scheduler.enqueue(request);
    const CommandId* held = nullptr;
    if (!holdsCommand(result, held)) {
        return false;
    }
    id = *held;
    return true;
}

/// @brief 读回写命令的当前阶段等于期望值。
/// @param scheduler 被测调度器，只在本次调用期间借用。
/// @param id 入队时的代号，按值传入。
/// @param phase 期望阶段。Sent、Acked、Verifying 仍在途。
/// @return phaseOf 有值且等于 phase 时为 true。已经结束或未要求读回时为 false。
bool phaseIs(const CommandScheduler& scheduler, CommandId id, WritePhase phase) {
    const std::optional<WritePhase> current = scheduler.phaseOf(id);
    return current.has_value() && *current == phase;
}

/// @brief 取走的结果是写回显，不是读回对照。
/// @param outcome 调度器交出的结果，只在本次调用期间借用。
/// @return body 为 WriteCommandResult，起始寄存器为 0，寄存器数量为 2，且不是 WriteVerification 时为 true。
/// 这只表示 ACK 回显，不是读回 Verified。
bool writeEchoOnly(const CommandOutcome& outcome) {
    if (std::get_if<WriteVerification>(&outcome.body) != nullptr) {
        return false;
    }
    const WriteCommandResult* ack = std::get_if<WriteCommandResult>(&outcome.body);
    return ack != nullptr && ack->startRegister == 0 && ack->registerCount == 2;
}

/// @brief 取走的结果是读回对照，且两份字节都是 kSlotData。
/// @param outcome 调度器交出的结果，只在本次调用期间借用。
/// @return body 为 WriteVerification、阶段为 Verified，且 target 与 actual 都等于 kSlotData 时为 true。
bool verifiedSlotData(const CommandOutcome& outcome) {
    const WriteVerification* verification = std::get_if<WriteVerification>(&outcome.body);
    if (verification == nullptr || verification->phase != WritePhase::Verified) {
        return false;
    }
    return sameBytes(verification->target, kSlotData) && sameBytes(verification->actual, kSlotData);
}

/// @brief 取走的结果是写 ACK 超时，不是读回对照。
/// @param outcome 调度器交出的结果，只在本次调用期间借用。
/// @return body 为 FailedCommand、错误码为 ResponseTimeout，且不是 WriteVerification 时为 true。
bool responseTimeoutOnly(const CommandOutcome& outcome) {
    if (std::get_if<WriteVerification>(&outcome.body) != nullptr) {
        return false;
    }
    const FailedCommand* failed = std::get_if<FailedCommand>(&outcome.body);
    return failed != nullptr && failed->code == ErrorCode::ResponseTimeout;
}

/// @brief 相同的四字节对照为 Verified，target 与 actual 都保留 kSlotData。
/// @return 阶段为 Verified 且两份字节都等于 kSlotData 时为 true。
bool compareMatch() {
    const WriteVerification compared = compareWriteReadBack(kSlotData, kSlotData);
    if (compared.phase != WritePhase::Verified) {
        return fail("compare_match");
    }
    if (!sameBytes(compared.target, kSlotData) || !sameBytes(compared.actual, kSlotData)) {
        return fail("compare_match");
    }
    return true;
}

/// @brief 最后一字节不同为 Mismatch。两份原文都保留，读回不覆盖写入目标。
/// @return 阶段为 Mismatch，target 与 actual 分别等于给定字面量，且二者不相等时为 true。
bool compareMismatch() {
    const WriteVerification compared = compareWriteReadBack(kMismatchTarget, kMismatchActual);
    if (compared.phase != WritePhase::Mismatch) {
        return fail("compare_mismatch");
    }
    if (!sameBytes(compared.target, kMismatchTarget) || !sameBytes(compared.actual, kMismatchActual)) {
        return fail("compare_mismatch");
    }
    if (sameBytes(compared.target, compared.actual)) {
        return fail("compare_mismatch");
    }
    return true;
}

/// @brief 未要求读回时，FC16 ACK 只形成写回显。phaseOf 为空。
/// @return 结果为起始寄存器 0、寄存器数量 2 的 WriteCommandResult，且只提交了一帧 FC16 时为 true。
bool ackWithoutReadback() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("ack_without_readback");
    }
    CommandScheduler scheduler(session, fake);

    CommandId id = CommandId::none();
    if (!enqueueOneSlotWrite(scheduler, false, id)) {
        return fail("ack_without_readback");
    }
    if (scheduler.phaseOf(id).has_value()) {
        return fail("ack_without_readback");
    }

    fake.queueBytes(kOneSlotAck);
    scheduler.pump();
    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    if (!outcome.has_value() || outcome->id != id || !writeEchoOnly(*outcome)) {
        return fail("ack_without_readback");
    }
    if (fake.submitted.size() != 1 || !functionCodeAt(fake.submitted[0], 0x10)) {
        return fail("ack_without_readback");
    }
    return true;
}

/// @brief 要求读回时，ACK 只把阶段停在 Acked，同一次无时钟 pump 不提交 FC03。
/// @return 入队后为 Sent 且已提交 FC16；收下 ACK 后仍为 Acked、提交数仍为 1、且没有结果时为 true。
bool ackStopsBeforeRead() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("ack_stops_before_read");
    }
    CommandScheduler scheduler(session, fake);

    CommandId id = CommandId::none();
    if (!enqueueOneSlotWrite(scheduler, true, id)) {
        return fail("ack_stops_before_read");
    }
    if (!phaseIs(scheduler, id, WritePhase::Sent) || scheduler.inFlightCount() != 1) {
        return fail("ack_stops_before_read");
    }
    if (fake.submitted.size() != 1 || !functionCodeAt(fake.submitted[0], 0x10)) {
        return fail("ack_stops_before_read");
    }

    fake.queueBytes(kOneSlotAck);
    scheduler.pump();
    if (!phaseIs(scheduler, id, WritePhase::Acked) || fake.submitted.size() != 1) {
        return fail("ack_stops_before_read");
    }
    if (scheduler.takeResult(id).has_value()) {
        return fail("ack_stops_before_read");
    }
    return true;
}

/// @brief 下一次没有新字节的 pump 才提交读回；读响应对照通过后为 Verified。
///
/// 本用例自己走完 Sent、Acked、Verifying，不依赖别的用例留下的调度器。
/// @return 第二次 pump 为 Verifying 且第二帧功能码为 0x03；第三次 pump 后两份字节都是 kSlotData，在途数量为 0 时为 true。
bool nextPumpVerifiesWithRead() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("next_pump_verifies_with_read");
    }
    CommandScheduler scheduler(session, fake);

    CommandId id = CommandId::none();
    if (!enqueueOneSlotWrite(scheduler, true, id)) {
        return fail("next_pump_verifies_with_read");
    }
    fake.queueBytes(kOneSlotAck);
    scheduler.pump();
    if (!phaseIs(scheduler, id, WritePhase::Acked)) {
        return fail("next_pump_verifies_with_read");
    }

    scheduler.pump();
    if (!phaseIs(scheduler, id, WritePhase::Verifying) || scheduler.inFlightCount() != 1) {
        return fail("next_pump_verifies_with_read");
    }
    if (fake.submitted.size() != 2 || !functionCodeAt(fake.submitted[1], 0x03)) {
        return fail("next_pump_verifies_with_read");
    }

    fake.queueBytes(kOneSlotResponse);
    scheduler.pump();
    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    if (!outcome.has_value() || outcome->id != id || !verifiedSlotData(*outcome)) {
        return fail("next_pump_verifies_with_read");
    }
    if (scheduler.inFlightCount() != 0) {
        return fail("next_pump_verifies_with_read");
    }
    return true;
}

/// @brief 写 ACK 等待超时后结束为 ResponseTimeout，并且不再提交 FC16。
///
/// 第一次带时钟的 pump 只开始计时。下一次时刻为起点加 100 毫秒时才超时。100 毫秒不是设备基线。
/// @return 第二次 pump 的结果为 ResponseTimeout、不是 WriteVerification，提交数仍为 1 且在途数量为 0 时为 true。
bool writeAckTimeoutDoesNotResend() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("write_ack_timeout_does_not_resend");
    }
    CommandScheduler scheduler(session, fake);

    CommandId id = CommandId::none();
    if (!enqueueOneSlotWrite(scheduler, true, id)) {
        return fail("write_ack_timeout_does_not_resend");
    }

    const RetryTime start = RetryTime{std::chrono::milliseconds{0}};
    const ReadRetryLimit once{kFixtureTimeout, 1};
    scheduler.pump(start, once);
    if (!phaseIs(scheduler, id, WritePhase::Sent) || scheduler.inFlightCount() != 1) {
        return fail("write_ack_timeout_does_not_resend");
    }
    if (fake.submitted.size() != 1 || scheduler.takeResult(id).has_value()) {
        return fail("write_ack_timeout_does_not_resend");
    }

    scheduler.pump(start + kFixtureTimeout, once);
    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    if (!outcome.has_value() || outcome->id != id || !responseTimeoutOnly(*outcome)) {
        return fail("write_ack_timeout_does_not_resend");
    }
    if (fake.submitted.size() != 1 || scheduler.inFlightCount() != 0) {
        return fail("write_ack_timeout_does_not_resend");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "compare_match") {
        return compareMatch();
    }
    if (name == "compare_mismatch") {
        return compareMismatch();
    }
    if (name == "ack_without_readback") {
        return ackWithoutReadback();
    }
    if (name == "ack_stops_before_read") {
        return ackStopsBeforeRead();
    }
    if (name == "next_pump_verifies_with_read") {
        return nextPumpVerifiesWithRead();
    }
    if (name == "write_ack_timeout_does_not_resend") {
        return writeAckTimeoutDoesNotResend();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个写回读单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对或用例名未知时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_write_verification_tests <case>\n";
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
