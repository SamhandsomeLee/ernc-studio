#include "retry_policy.hpp"
#include "scheduler.hpp"
#include "session.hpp"

#include <chrono>
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
using ernc::application::CommandRequest;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::FailedCommand;
using ernc::application::ReadCommandResult;
using ernc::application::ReadRetryDecision;
using ernc::application::ReadRetryLimit;
using ernc::application::ReadRetryStep;
using ernc::application::RetryTime;
using ernc::application::decideReadRetry;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::SessionId;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;

/// 测试夹具里的一次等待长度。100 毫秒只用于推进模拟时刻，不是设备超时基线。
constexpr std::chrono::milliseconds kFixtureTimeout{100};

/// 模拟时钟的起点。后续用例在它之上加上 kFixtureTimeout。
const RetryTime kStart = RetryTime{std::chrono::milliseconds{0}};

/// 长度前缀的一槽 FC03 响应。数据区是 0x78 0x56 0x34 0x12。CRC 是字面量，不在测试里重算。
constexpr std::uint8_t kOneSlotResponse[] = {0x09, 0x01, 0x03, 0x04, 0x78, 0x56, 0x34, 0x12, 0x94, 0x4E};

/// kOneSlotResponse 的数据区。四个字节保持线上顺序。
constexpr std::uint8_t kSlotData[] = {0x78, 0x56, 0x34, 0x12};

/// @brief 供读重试使用的假端口。不打开串口。
class Fake final : public IDeviceTransport {
public:
    /// @brief 构造为关闭。提交记录和步骤队列都为空。
    Fake() = default;

    /// @brief 打开假端口。
    /// @return 进入打开并成功。成功只表示端口被收下。
    Result<void> open() override {
        open_ = true;
        return Result<void>::success();
    }

    /// @brief 关闭假端口。
    /// @return 已经关闭时成功且不排步骤。从打开状态关闭时只排一个 Closed。
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
    /// @param bytes 线字节，只在本次调用期间借用。函数保存自己的副本。
    /// @return 关闭时为 DeviceDisconnected。否则成功。成功不是设备响应。
    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        if (!open_) {
            return Result<void>::failure(ErrorCode::DeviceDisconnected);
        }
        submitted.emplace_back(bytes.begin(), bytes.end());
        return Result<void>::success();
    }

    /// @brief 取走队列中的下一步。
    /// @return 没有步骤时为 Idle。否则弹出队首。
    TransportStep poll() override {
        if (queued_.empty()) {
            return TransportStep{};
        }
        TransportStep step = std::move(queued_.front());
        queued_.erase(queued_.begin());
        return step;
    }

    /// @brief 排入一段已收到的线字节。
    /// @param bytes 线字节，只在本次调用期间借用。函数复制进 Kind::Bytes。
    void queueBytes(std::span<const std::uint8_t> bytes) {
        TransportStep step;
        step.kind = TransportStep::Kind::Bytes;
        step.bytes.assign(bytes.begin(), bytes.end());
        queued_.push_back(std::move(step));
    }

    /// 每次 submit 收下的整帧。关闭时的调用不会追加。
    std::vector<std::vector<std::uint8_t>> submitted;

private:
    /// 当前是否打开。新建时为 false。
    bool open_{false};

    /// poll 待弹出的步骤。
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

/// @brief 打开会话。
/// @param session 已绑定假端口的会话，只在本次调用期间借用。
/// @return connect 成功且代号有效时为 true。
bool connectSession(DeviceSession& session) {
    const Result<SessionId> connected = session.connect();
    return connected.ok() && connected.tryError() == nullptr && connected.tryValue() != nullptr &&
           connected.tryValue()->valid();
}

/// @brief 入队一条一槽读命令。
/// @param scheduler 被测调度器，只在本次调用期间借用。
/// @param id 成功时写入新代号。失败时不修改。调用方拥有这份副本。
/// @return 入队成功且代号有效时为 true。
bool enqueueOneRead(CommandScheduler& scheduler, CommandId& id) {
    CommandRequest request;
    request.op = CommandOp::Read;
    request.firstSlot = 0;
    request.slotCount = 1;
    request.slaveAddress = 0x01;
    const Result<CommandId> result = scheduler.enqueue(request);
    const CommandId* held = result.tryValue();
    if (!result.ok() || result.tryError() != nullptr || held == nullptr || !held->valid()) {
        return false;
    }
    id = *held;
    return true;
}

/// @brief 结果是指定错误码的失败，不是读数据。
/// @param outcome 调度器交出的结果，只在本次调用期间借用。
/// @param code 期望的失败类别。
/// @return body 为 FailedCommand 且 code 匹配时为 true。
bool failedWith(const CommandOutcome& outcome, ErrorCode code) {
    if (std::get_if<ReadCommandResult>(&outcome.body) != nullptr) {
        return false;
    }
    const FailedCommand* failed = std::get_if<FailedCommand>(&outcome.body);
    return failed != nullptr && failed->code == code;
}

/// @brief 截止时间之前保持等待。
/// @return 决定为 Waiting 时为 true。
bool waitingBeforeDeadline() {
    const ReadRetryLimit limit{kFixtureTimeout, 2};
    const ReadRetryStep step = decideReadRetry(limit, kStart, kStart + std::chrono::milliseconds{99}, 1);
    if (step.decision != ReadRetryDecision::Waiting) {
        return fail("waiting_before_deadline");
    }
    return true;
}

/// @brief 第一次发送超时后还可以重发。
/// @return 决定为 Resubmit 时为 true。
bool resubmitBeforeLastAttempt() {
    const ReadRetryLimit limit{kFixtureTimeout, 2};
    const ReadRetryStep step = decideReadRetry(limit, kStart, kStart + kFixtureTimeout, 1);
    if (step.decision != ReadRetryDecision::Resubmit) {
        return fail("resubmit_before_last_attempt");
    }
    return true;
}

/// @brief 只允许一次发送时，超时终态是 ResponseTimeout。
/// @return 决定为 Finished 且错误码匹配时为 true。
bool singleAttemptTimeout() {
    const ReadRetryLimit limit{kFixtureTimeout, 1};
    const ReadRetryStep step = decideReadRetry(limit, kStart, kStart + kFixtureTimeout, 1);
    if (step.decision != ReadRetryDecision::Finished || step.terminal != ErrorCode::ResponseTimeout) {
        return fail("single_attempt_timeout");
    }
    return true;
}

/// @brief 重发过的最后一次超时终态是 RetryExhausted。
/// @return 决定为 Finished 且错误码匹配时为 true。
bool exhaustedAfterResend() {
    const ReadRetryLimit limit{kFixtureTimeout, 2};
    const ReadRetryStep step = decideReadRetry(limit, kStart, kStart + kFixtureTimeout, 2);
    if (step.decision != ReadRetryDecision::Finished || step.terminal != ErrorCode::RetryExhausted) {
        return fail("exhausted_after_resend");
    }
    return true;
}

/// @brief 次数或等待长度为 0 时不重发。
/// @return 两份无效上限都是 Finished 且错误码为 InvalidParameter 时为 true。
bool invalidLimit() {
    const ReadRetryStep zeroAttempts = decideReadRetry(ReadRetryLimit{kFixtureTimeout, 0}, kStart, kStart, 1);
    const ReadRetryStep zeroTimeout =
        decideReadRetry(ReadRetryLimit{std::chrono::milliseconds{0}, 1}, kStart, kStart, 1);
    if (zeroAttempts.decision != ReadRetryDecision::Finished ||
        zeroAttempts.terminal != ErrorCode::InvalidParameter ||
        zeroTimeout.decision != ReadRetryDecision::Finished ||
        zeroTimeout.terminal != ErrorCode::InvalidParameter) {
        return fail("invalid_limit");
    }
    return true;
}

/// @brief 一次发送没有响应时结束，并且只 submit 过一次。
/// @return 结果为 ResponseTimeout、在途为 0、只提交一帧时为 true。
bool schedulerTimesOutOnce() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("scheduler_times_out_once");
    }
    CommandScheduler scheduler(session, fake);
    CommandId id = CommandId::none();
    if (!enqueueOneRead(scheduler, id)) {
        return fail("scheduler_times_out_once");
    }
    const ReadRetryLimit limit{kFixtureTimeout, 1};
    scheduler.pump(kStart, limit);
    scheduler.pump(kStart + kFixtureTimeout, limit);
    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    if (!outcome.has_value() || !failedWith(*outcome, ErrorCode::ResponseTimeout) ||
        scheduler.inFlightCount() != 0 || fake.submitted.size() != 1) {
        return fail("scheduler_times_out_once");
    }
    return true;
}

/// @brief 第一次超时重发同一帧；第二次超时以 RetryExhausted 结束。
/// @return 代号不变、两帧相同、最终在途为 0 时为 true。
bool schedulerResubmitsSameFrame() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("scheduler_resubmits_same_frame");
    }
    CommandScheduler scheduler(session, fake);
    CommandId id = CommandId::none();
    if (!enqueueOneRead(scheduler, id)) {
        return fail("scheduler_resubmits_same_frame");
    }
    const ReadRetryLimit limit{kFixtureTimeout, 2};
    scheduler.pump(kStart, limit);
    scheduler.pump(kStart + kFixtureTimeout, limit);
    if (scheduler.inFlightCount() != 1 || scheduler.takeResult(id).has_value() || fake.submitted.size() != 2 ||
        !sameBytes(fake.submitted[0], fake.submitted[1])) {
        return fail("scheduler_resubmits_same_frame");
    }
    scheduler.pump(kStart + kFixtureTimeout + kFixtureTimeout, limit);
    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    if (!outcome.has_value() || outcome->id != id || !failedWith(*outcome, ErrorCode::RetryExhausted) ||
        scheduler.inFlightCount() != 0 || fake.submitted.size() != 2) {
        return fail("scheduler_resubmits_same_frame");
    }
    return true;
}

/// @brief 截止时间之前的完整读响应直接成功，不再 submit。
/// @return payload 为字面量数据区且只提交一帧时为 true。
bool responseCancelsRetry() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("response_cancels_retry");
    }
    CommandScheduler scheduler(session, fake);
    CommandId id = CommandId::none();
    if (!enqueueOneRead(scheduler, id)) {
        return fail("response_cancels_retry");
    }
    const ReadRetryLimit limit{kFixtureTimeout, 2};
    scheduler.pump(kStart, limit);
    fake.queueBytes(kOneSlotResponse);
    scheduler.pump(kStart + std::chrono::milliseconds{50}, limit);
    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    const ReadCommandResult* read = outcome.has_value() ? std::get_if<ReadCommandResult>(&outcome->body) : nullptr;
    if (read == nullptr || !sameBytes(read->payload, kSlotData) || fake.submitted.size() != 1 ||
        scheduler.inFlightCount() != 0) {
        return fail("response_cancels_retry");
    }
    return true;
}

/// @brief 在途写命令到了读截止时间也不再提交。
/// @return 仍只提交一帧且在途数量保持 1 时为 true。
bool writeIgnoresReadDeadline() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("write_ignores_read_deadline");
    }
    CommandScheduler scheduler(session, fake);
    CommandRequest request;
    request.op = CommandOp::Write;
    request.firstSlot = 0;
    request.slotCount = 1;
    request.payload = {0x78, 0x56, 0x34, 0x12};
    request.slaveAddress = 0x01;
    const Result<CommandId> enqueued = scheduler.enqueue(request);
    if (!enqueued.ok() || enqueued.tryError() != nullptr) {
        return fail("write_ignores_read_deadline");
    }
    const ReadRetryLimit limit{kFixtureTimeout, 2};
    scheduler.pump(kStart, limit);
    scheduler.pump(kStart + kFixtureTimeout, limit);
    if (fake.submitted.size() != 1 || scheduler.inFlightCount() != 1) {
        return fail("write_ignores_read_deadline");
    }
    return true;
}

/// @brief 超时结束之后的迟到帧不把这条命令改成成功。
/// @return 取走的结果仍是 ResponseTimeout，并且不是读数据时为 true。
bool lateFrameKeepsTimeout() {
    Fake fake;
    DeviceSession session(fake);
    if (!connectSession(session)) {
        return fail("late_frame_keeps_timeout");
    }
    CommandScheduler scheduler(session, fake);
    CommandId id = CommandId::none();
    if (!enqueueOneRead(scheduler, id)) {
        return fail("late_frame_keeps_timeout");
    }
    const ReadRetryLimit limit{kFixtureTimeout, 1};
    scheduler.pump(kStart, limit);
    scheduler.pump(kStart + kFixtureTimeout, limit);
    fake.queueBytes(kOneSlotResponse);
    scheduler.pump(kStart + kFixtureTimeout + std::chrono::milliseconds{1}, limit);
    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    if (!outcome.has_value() || !failedWith(*outcome, ErrorCode::ResponseTimeout) ||
        scheduler.inFlightCount() != 0) {
        return fail("late_frame_keeps_timeout");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "waiting_before_deadline") {
        return waitingBeforeDeadline();
    }
    if (name == "resubmit_before_last_attempt") {
        return resubmitBeforeLastAttempt();
    }
    if (name == "single_attempt_timeout") {
        return singleAttemptTimeout();
    }
    if (name == "exhausted_after_resend") {
        return exhaustedAfterResend();
    }
    if (name == "invalid_limit") {
        return invalidLimit();
    }
    if (name == "scheduler_times_out_once") {
        return schedulerTimesOutOnce();
    }
    if (name == "scheduler_resubmits_same_frame") {
        return schedulerResubmitsSameFrame();
    }
    if (name == "response_cancels_retry") {
        return responseCancelsRetry();
    }
    if (name == "write_ignores_read_deadline") {
        return writeIgnoresReadDeadline();
    }
    if (name == "late_frame_keeps_timeout") {
        return lateFrameKeepsTimeout();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个读重试单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_retry_policy_tests <case>\n";
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
