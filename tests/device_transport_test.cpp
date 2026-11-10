#include "ernc/ports/device_transport.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;

/// @brief 测试用设备端口。实现 IDeviceTransport 契约并可排队 poll 事件。
class FakeDeviceTransport final : public IDeviceTransport {
public:
    /// @brief 构造为关闭状态，无已提交字节，无排队步骤。
    FakeDeviceTransport() = default;

    /// @brief 打开本地端点。
    /// @return 关闭时成功并进入打开。重复打开时为 InvalidParameter，状态保持打开。
    Result<void> open() override {
        if (open_) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        open_ = true;
        return Result<void>::success();
    }

    /// @brief 关闭本地端点。
    /// @return 已关闭时仍成功且不产生 Closed。打开时成功、进入关闭并排队一次 Closed。
    Result<void> close() override {
        if (!open_) {
            return Result<void>::success();
        }
        open_ = false;
        TransportStep step{};
        step.kind = TransportStep::Kind::Closed;
        queued_.push_back(std::move(step));
        return Result<void>::success();
    }

    /// @brief 收下一段待发送线字节，只在本次调用期间借用 bytes。
    /// @param bytes 非空时被整段复制进内部缓冲；空序列被拒绝且不改动已保存字节。
    /// @return 关闭时为 DeviceDisconnected。空序列 InvalidParameter。拒绝标志消费一次 SerialIoFailed。
    /// 已打开且收下时成功，不产生 poll 事件。
    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        if (!open_) {
            return Result<void>::failure(ErrorCode::DeviceDisconnected);
        }
        if (bytes.empty()) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        if (refuseNextSubmit_) {
            refuseNextSubmit_ = false;
            return Result<void>::failure(ErrorCode::SerialIoFailed);
        }
        submitted_.assign(bytes.begin(), bytes.end());
        return Result<void>::success();
    }

    /// @brief 取走队列中的下一步。
    /// @return 队列为空时 Idle。否则弹出队首一步并按值返回。
    TransportStep poll() override {
        if (queued_.empty()) {
            return TransportStep{};
        }
        TransportStep step = std::move(queued_.front());
        queued_.erase(queued_.begin());
        return step;
    }

    /// @brief 让下一次 submit 返回 SerialIoFailed，不替换已保存字节，不产生 poll 事件。
    void refuseNextSubmit() { refuseNextSubmit_ = true; }

    /// @brief 仅在打开时由测试调用。把一段字节作为 Bytes 步骤入队。
    /// @param bytes 线字节副本，只在本次调用期间借用。
    void queueBytes(std::span<const std::uint8_t> bytes) {
        TransportStep step{};
        step.kind = TransportStep::Kind::Bytes;
        step.bytes.assign(bytes.begin(), bytes.end());
        queued_.push_back(std::move(step));
    }

    /// @brief 入队 Fault 并立即强制本 Fake 回到关闭。
    /// @param code Fault 步骤携带的错误码。
    void queueFault(ErrorCode code) {
        TransportStep step{};
        step.kind = TransportStep::Kind::Fault;
        step.error = code;
        queued_.push_back(std::move(step));
        open_ = false;
    }

    /// @brief 借出最近一次成功 submit 保存的字节副本。
    /// @return 指向内部缓冲的只读视图，生存期与本对象相同。
    std::span<const std::uint8_t> submittedBytes() const { return submitted_; }

private:
    /// 当前是否为打开状态。新建时为 false。
    bool open_{false};

    /// 最近一次成功 submit 复制的线字节。拒绝或失败提交不改动此缓冲。
    std::vector<std::uint8_t> submitted_;

    /// poll 待弹出的步骤 FIFO。
    std::vector<TransportStep> queued_;

    /// 为 true 时下一次 submit 返回 SerialIoFailed 并被消费。
    bool refuseNextSubmit_{false};
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

/// @brief poll 步骤为 Idle，字节为空，无错误码。
/// @param step 被检查的步骤，按值传入。
/// @return 满足 Idle 契约时为 true。
bool isIdleStep(const TransportStep& step) {
    return step.kind == TransportStep::Kind::Idle && step.bytes.empty() && !step.error.has_value();
}

/// @brief 关闭状态下 submit 返回 DeviceDisconnected，poll 保持 Idle。
/// @return 契约成立时为 true。
bool closedSubmit() {
    FakeDeviceTransport transport;
    const std::uint8_t payload[] = {0x01, 0x03};
    const Result<void> submitted = transport.submit(payload);
    const ErrorCode* error = submitted.tryError();
    if (submitted.ok() || error == nullptr || *error != ErrorCode::DeviceDisconnected) {
        return fail("closed_submit");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("closed_submit");
    }
    return true;
}

/// @brief 打开后 submit 空序列返回 InvalidParameter，poll 为 Idle。
/// @return 契约成立时为 true。
bool emptySubmit() {
    FakeDeviceTransport transport;
    const Result<void> opened = transport.open();
    if (!opened.ok() || opened.tryError() != nullptr) {
        return fail("empty_submit");
    }
    const Result<void> submitted = transport.submit({});
    const ErrorCode* error = submitted.tryError();
    if (submitted.ok() || error == nullptr || *error != ErrorCode::InvalidParameter) {
        return fail("empty_submit");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("empty_submit");
    }
    return true;
}

/// @brief 重复 open 为 InvalidParameter，随后 submit 仍可在打开状态下成功。
/// @return 契约成立时为 true。
bool duplicateOpen() {
    FakeDeviceTransport transport;
    const Result<void> firstOpen = transport.open();
    if (!firstOpen.ok() || firstOpen.tryError() != nullptr) {
        return fail("duplicate_open");
    }
    const Result<void> secondOpen = transport.open();
    const ErrorCode* openError = secondOpen.tryError();
    if (secondOpen.ok() || openError == nullptr || *openError != ErrorCode::InvalidParameter) {
        return fail("duplicate_open");
    }
    const std::uint8_t payload[] = {0x11};
    const Result<void> submitted = transport.submit(payload);
    if (!submitted.ok() || submitted.tryError() != nullptr) {
        return fail("duplicate_open");
    }
    return true;
}

/// @brief 成功 submit 后内部保留恰好相同的字节，poll 为 Idle。
/// @return 契约成立时为 true。
bool submitStoresBytes() {
    FakeDeviceTransport transport;
    const Result<void> opened = transport.open();
    if (!opened.ok() || opened.tryError() != nullptr) {
        return fail("submit_stores_bytes");
    }
    const std::uint8_t payload[] = {0x01, 0x03};
    const Result<void> submitted = transport.submit(payload);
    if (!submitted.ok() || submitted.tryError() != nullptr) {
        return fail("submit_stores_bytes");
    }
    if (!sameBytes(transport.submittedBytes(), payload)) {
        return fail("submit_stores_bytes");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("submit_stores_bytes");
    }
    return true;
}

/// @brief 排队字节后第一次 poll 为 Bytes，第二次为 Idle。
/// @return 契约成立时为 true。
bool pollBytesThenIdle() {
    FakeDeviceTransport transport;
    const Result<void> opened = transport.open();
    if (!opened.ok() || opened.tryError() != nullptr) {
        return fail("poll_bytes_then_idle");
    }
    const std::uint8_t queued[] = {0xAA, 0xBB};
    transport.queueBytes(queued);
    const TransportStep first = transport.poll();
    if (first.kind != TransportStep::Kind::Bytes || first.error.has_value() ||
        !sameBytes(first.bytes, queued)) {
        return fail("poll_bytes_then_idle");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("poll_bytes_then_idle");
    }
    return true;
}

/// @brief 未打开时 close 成功，连续 close 与 poll 均为 Idle。
/// @return 契约成立时为 true。
bool closeWhileClosed() {
    FakeDeviceTransport transport;
    const Result<void> firstClose = transport.close();
    if (!firstClose.ok() || firstClose.tryError() != nullptr) {
        return fail("close_while_closed");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("close_while_closed");
    }
    const Result<void> secondClose = transport.close();
    if (!secondClose.ok() || secondClose.tryError() != nullptr) {
        return fail("close_while_closed");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("close_while_closed");
    }
    return true;
}

/// @brief close 后 poll 先得到 Closed，再 Idle；此后 submit 为 DeviceDisconnected。
/// @return 契约成立时为 true。
bool closeEmitsOneClosed() {
    FakeDeviceTransport transport;
    const Result<void> opened = transport.open();
    if (!opened.ok() || opened.tryError() != nullptr) {
        return fail("close_emits_one_closed");
    }
    const Result<void> closed = transport.close();
    if (!closed.ok() || closed.tryError() != nullptr) {
        return fail("close_emits_one_closed");
    }
    const TransportStep firstPoll = transport.poll();
    if (firstPoll.kind != TransportStep::Kind::Closed || !firstPoll.bytes.empty() ||
        firstPoll.error.has_value()) {
        return fail("close_emits_one_closed");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("close_emits_one_closed");
    }
    const std::uint8_t payload[] = {0x01};
    const Result<void> submitted = transport.submit(payload);
    const ErrorCode* submitError = submitted.tryError();
    if (submitted.ok() || submitError == nullptr || *submitError != ErrorCode::DeviceDisconnected) {
        return fail("close_emits_one_closed");
    }
    return true;
}

/// @brief refuseNextSubmit 使下一次 submit 失败且不改动已保存字节。
/// @return 契约成立时为 true。
bool refusedSubmit() {
    FakeDeviceTransport transport;
    const Result<void> opened = transport.open();
    if (!opened.ok() || opened.tryError() != nullptr) {
        return fail("refused_submit");
    }
    const std::uint8_t first[] = {0x22};
    const Result<void> firstSubmit = transport.submit(first);
    if (!firstSubmit.ok() || firstSubmit.tryError() != nullptr) {
        return fail("refused_submit");
    }
    transport.refuseNextSubmit();
    const std::uint8_t second[] = {0x33, 0x44};
    const Result<void> refused = transport.submit(second);
    const ErrorCode* refuseError = refused.tryError();
    if (refused.ok() || refuseError == nullptr || *refuseError != ErrorCode::SerialIoFailed) {
        return fail("refused_submit");
    }
    if (!sameBytes(transport.submittedBytes(), first)) {
        return fail("refused_submit");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("refused_submit");
    }
    return true;
}

/// @brief queueFault 后 poll 为 Fault 再 Idle，端口关闭后 submit 为 DeviceDisconnected。
/// @return 契约成立时为 true。
bool faultThenSubmit() {
    FakeDeviceTransport transport;
    const Result<void> opened = transport.open();
    if (!opened.ok() || opened.tryError() != nullptr) {
        return fail("fault_then_submit");
    }
    transport.queueFault(ErrorCode::DeviceDisconnected);
    const TransportStep faultStep = transport.poll();
    if (faultStep.kind != TransportStep::Kind::Fault || !faultStep.bytes.empty() ||
        !faultStep.error.has_value() || *faultStep.error != ErrorCode::DeviceDisconnected) {
        return fail("fault_then_submit");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("fault_then_submit");
    }
    const std::uint8_t payload[] = {0x01};
    const Result<void> submitted = transport.submit(payload);
    const ErrorCode* submitError = submitted.tryError();
    if (submitted.ok() || submitError == nullptr || *submitError != ErrorCode::DeviceDisconnected) {
        return fail("fault_then_submit");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "closed_submit") {
        return closedSubmit();
    }
    if (name == "empty_submit") {
        return emptySubmit();
    }
    if (name == "duplicate_open") {
        return duplicateOpen();
    }
    if (name == "submit_stores_bytes") {
        return submitStoresBytes();
    }
    if (name == "poll_bytes_then_idle") {
        return pollBytesThenIdle();
    }
    if (name == "close_while_closed") {
        return closeWhileClosed();
    }
    if (name == "close_emits_one_closed") {
        return closeEmitsOneClosed();
    }
    if (name == "refused_submit") {
        return refusedSubmit();
    }
    if (name == "fault_then_submit") {
        return faultThenSubmit();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个设备传输端口单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_device_transport_tests <case>\n";
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
