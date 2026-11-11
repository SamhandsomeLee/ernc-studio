#include "session.hpp"

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ernc::application::DeviceSession;
using ernc::application::SessionState;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::SessionId;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;

/// @brief 供 DeviceSession 使用的假端口。不打开串口，也不调用 QSerialPort。
///
/// 新建时为关闭。测试先构造本对象，再构造 DeviceSession，然后把 session 指到该会话。
class FakeTransport final : public IDeviceTransport {
public:
    /// @brief 构造为关闭，队列为空，下一次 open 不会失败。
    FakeTransport() = default;

    /// @brief 打开假端口，并在已绑定会话时记下当时的状态和代号。
    /// @return failNextOpen 为真时返回 SerialOpenFailed，消费该标志，端口保持关闭。
    /// 否则进入打开并成功。成功只表示端口被收下，还没有设备响应。
    Result<void> open() override {
        if (session != nullptr) {
            stateDuringOpen = session->state();
            idDuringOpen = session->currentId();
        }
        if (failNextOpen) {
            failNextOpen = false;
            return Result<void>::failure(ErrorCode::SerialOpenFailed);
        }
        open_ = true;
        return Result<void>::success();
    }

    /// @brief 关闭假端口。
    /// @return 已经关闭时成功，不排步骤。
    /// 打开时先记下会话状态和代号，再关闭，并只排一个 Kind::Closed。
    Result<void> close() override {
        if (!open_) {
            return Result<void>::success();
        }
        if (session != nullptr) {
            stateDuringClose = session->state();
            idDuringClose = session->currentId();
        }
        open_ = false;
        TransportStep step{};
        step.kind = TransportStep::Kind::Closed;
        queued_.push_back(std::move(step));
        ++closedQueued;
        return Result<void>::success();
    }

    /// @brief 收下一段线字节。本假端口不保存字节，也不排 Bytes。
    /// @param bytes 调用方的线字节，只在本次调用期间借用。空序列会被拒绝。
    /// @return 关闭时为 DeviceDisconnected。空序列为 InvalidParameter。否则成功。
    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        if (!open_) {
            return Result<void>::failure(ErrorCode::DeviceDisconnected);
        }
        if (bytes.empty()) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
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

    /// @brief 报告假端口是否处于打开。
    /// @return 打开成功之后为 true。新建时和打开失败后为 false。
    bool isOpen() const noexcept { return open_; }

    /// 测试在 DeviceSession 构造之后写入。open 和 close 用它读会话。空指针表示尚未绑定。本对象不拥有该会话。
    DeviceSession* session{nullptr};

    /// 为真时，下一次 open 返回 SerialOpenFailed 并清掉本标志，不进入打开。
    bool failNextOpen{false};

    /// 最近一次 open 在 session 非空时读到的状态。还没读过时为 Disconnected。
    SessionState stateDuringOpen{SessionState::Disconnected};

    /// 最近一次 open 在 session 非空时读到的代号。还没读过时为 none()。
    SessionId idDuringOpen{SessionId::none()};

    /// 最近一次从打开状态 close 时读到的状态。还没发生时为 Disconnected。
    SessionState stateDuringClose{SessionState::Disconnected};

    /// 最近一次从打开状态 close 时读到的代号。还没发生时为 none()。
    SessionId idDuringClose{SessionId::none()};

    /// 从打开状态 close 时排入的 Closed 步数。每次这样的 close 加一。
    std::size_t closedQueued{0};

private:
    /// 当前是否打开。新建时为 false。失败的 open 不把它改成 true。
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

/// @brief 代号是“没有会话”。
/// @param id 被检查的代号，按值传入。
/// @return raw() 为 0 且 valid() 为 false 时为 true。
bool idIsNone(SessionId id) {
    return id.raw() == 0 && !id.valid();
}

/// @brief poll 步骤为 Idle，字节为空，无错误码。
/// @param step 被检查的步骤，按值传入。调用方仍拥有自己的那一份。
/// @return 满足 Idle 契约时为 true。
bool isIdleStep(const TransportStep& step) {
    return step.kind == TransportStep::Kind::Idle && step.bytes.empty() && !step.error.has_value();
}

/// @brief 没有载荷的结果被标为成功，且读不到错误码。
/// @param result 被测返回值，只在本次调用期间借用。本函数不调用 error()。
/// @return ok() 为 true 且 tryError() 为空时为 true。
bool succeeded(const Result<void>& result) {
    return result.ok() && result.tryError() == nullptr;
}

/// @brief 没有载荷的结果失败，且错误码与期望相同。
/// @param result 被测返回值，只在本次调用期间借用。本函数不调用 error()。
/// @param code 期望的失败类别。
/// @return ok() 为 false 且 tryError() 指向 code 时为 true。
bool failedAs(const Result<void>& result, ErrorCode code) {
    const ErrorCode* error = result.tryError();
    return !result.ok() && error != nullptr && *error == code;
}

/// @brief 带代号的结果成功，并交出内部代号。
/// @param result 被测返回值，只在本次调用期间借用。本函数不调用 error()。
/// @param id 成功时指向结果内部的代号。指针不转移所有权，只在 result 存活期间有效。
/// @return ok() 为 true、tryError() 为空且代号指针非空时为 true。
bool holdsId(const Result<SessionId>& result, const SessionId*& id) {
    id = result.tryValue();
    return result.ok() && result.tryError() == nullptr && id != nullptr;
}

/// @brief open 期间读到 Connecting 和有效代号；connect 成功后接受该代号。
/// @return 状态、代号和 Pending 都符合时为 true。
bool connectSeesConnecting() {
    FakeTransport transport;
    DeviceSession session{transport};
    transport.session = &session;

    const Result<SessionId> connected = session.connect();
    const SessionId* id = nullptr;
    if (!holdsId(connected, id)) {
        return fail("connect_sees_connecting");
    }
    if (transport.stateDuringOpen != SessionState::Connecting || !transport.idDuringOpen.valid()) {
        return fail("connect_sees_connecting");
    }
    if (*id != transport.idDuringOpen || session.state() != SessionState::Connected) {
        return fail("connect_sees_connecting");
    }
    if (!session.acceptResult(*id) || session.pendingCount() != 0) {
        return fail("connect_sees_connecting");
    }
    return true;
}

/// @brief 打开失败会作废这次代号；下一次 connect 使用新代号。
/// @return 失败码、旧代号拒绝和新代号接受都符合时为 true。
bool openFailureRetiresId() {
    FakeTransport transport;
    DeviceSession session{transport};
    transport.session = &session;
    transport.failNextOpen = true;

    const Result<SessionId> failed = session.connect();
    const ErrorCode* error = failed.tryError();
    if (failed.ok() || error == nullptr || *error != ErrorCode::SerialOpenFailed) {
        return fail("open_failure_retires_id");
    }
    if (transport.isOpen() || transport.stateDuringOpen != SessionState::Connecting) {
        return fail("open_failure_retires_id");
    }
    const SessionId retired = transport.idDuringOpen;
    if (!retired.valid() || session.state() != SessionState::Disconnected || !idIsNone(session.currentId())) {
        return fail("open_failure_retires_id");
    }
    if (session.acceptResult(retired)) {
        return fail("open_failure_retires_id");
    }

    const Result<SessionId> connected = session.connect();
    const SessionId* fresh = nullptr;
    if (!holdsId(connected, fresh) || fresh->raw() == retired.raw()) {
        return fail("open_failure_retires_id");
    }
    if (session.acceptResult(retired) || !session.acceptResult(*fresh)) {
        return fail("open_failure_retires_id");
    }
    return true;
}

/// @brief 断连清掉 Pending 和旧代号；再次连接得到另一个代号。
/// @return 两次代号、Pending 和接受判断都符合时为 true。
bool disconnectDropsPendingAndOldId() {
    FakeTransport transport;
    DeviceSession session{transport};
    transport.session = &session;

    const Result<SessionId> connected = session.connect();
    const SessionId* first = nullptr;
    if (!holdsId(connected, first)) {
        return fail("disconnect_drops_pending_and_old_id");
    }
    const SessionId oldId = *first;
    if (!succeeded(session.notePending()) || session.pendingCount() != 1) {
        return fail("disconnect_drops_pending_and_old_id");
    }

    if (!succeeded(session.disconnect())) {
        return fail("disconnect_drops_pending_and_old_id");
    }
    if (session.state() != SessionState::Disconnected || session.pendingCount() != 0) {
        return fail("disconnect_drops_pending_and_old_id");
    }
    if (!idIsNone(session.currentId()) || session.acceptResult(oldId)) {
        return fail("disconnect_drops_pending_and_old_id");
    }

    const Result<SessionId> again = session.connect();
    const SessionId* second = nullptr;
    if (!holdsId(again, second) || second->raw() == oldId.raw()) {
        return fail("disconnect_drops_pending_and_old_id");
    }
    if (session.acceptResult(oldId) || !session.acceptResult(*second) || session.pendingCount() != 0) {
        return fail("disconnect_drops_pending_and_old_id");
    }
    return true;
}

/// @brief close 看到 Disconnecting，会话取走唯一的 Closed，之后 poll 为 Idle。
/// @return 断开成功且端口里不再留着 Closed 时为 true。
bool disconnectDrainsClosed() {
    FakeTransport transport;
    DeviceSession session{transport};
    transport.session = &session;

    const Result<SessionId> connected = session.connect();
    const SessionId* id = nullptr;
    if (!holdsId(connected, id) || !id->valid()) {
        return fail("disconnect_drains_closed");
    }
    if (!succeeded(session.disconnect())) {
        return fail("disconnect_drains_closed");
    }
    if (transport.stateDuringClose != SessionState::Disconnecting || transport.idDuringClose.valid()) {
        return fail("disconnect_drains_closed");
    }
    if (transport.closedQueued != 1 || !isIdleStep(transport.poll())) {
        return fail("disconnect_drains_closed");
    }
    if (session.state() != SessionState::Disconnected) {
        return fail("disconnect_drains_closed");
    }
    return true;
}

/// @brief Degraded 保留原代号；断连后该代号失效，断开状态下不能再标记降级。
/// @return 降级、Pending 和随后的拒绝都符合时为 true。
bool degradedKeepsId() {
    FakeTransport transport;
    DeviceSession session{transport};
    transport.session = &session;

    const Result<SessionId> connected = session.connect();
    const SessionId* id = nullptr;
    if (!holdsId(connected, id)) {
        return fail("degraded_keeps_id");
    }
    const SessionId kept = *id;
    if (!succeeded(session.markDegraded())) {
        return fail("degraded_keeps_id");
    }
    if (session.state() != SessionState::Degraded || session.currentId() != kept || !session.acceptResult(kept)) {
        return fail("degraded_keeps_id");
    }
    if (!succeeded(session.notePending())) {
        return fail("degraded_keeps_id");
    }
    if (!succeeded(session.disconnect()) || session.pendingCount() != 0 || session.acceptResult(kept)) {
        return fail("degraded_keeps_id");
    }
    if (session.state() != SessionState::Disconnected || !failedAs(session.markDegraded(), ErrorCode::InvalidParameter)) {
        return fail("degraded_keeps_id");
    }
    return true;
}

/// @brief 已经 Connected 时再次 connect 被拒绝，代号不变。
/// @return 第二次为 InvalidParameter 且状态仍是第一次的 Connected 时为 true。
bool secondConnectRejected() {
    FakeTransport transport;
    DeviceSession session{transport};
    transport.session = &session;

    const Result<SessionId> connected = session.connect();
    const SessionId* id = nullptr;
    if (!holdsId(connected, id)) {
        return fail("second_connect_rejected");
    }
    const SessionId kept = *id;
    const Result<SessionId> again = session.connect();
    const ErrorCode* error = again.tryError();
    if (again.ok() || error == nullptr || *error != ErrorCode::InvalidParameter) {
        return fail("second_connect_rejected");
    }
    if (session.state() != SessionState::Connected || session.currentId() != kept) {
        return fail("second_connect_rejected");
    }
    return true;
}

/// @brief 尚未连接时不能登记 Pending。
/// @return 错误码为 InvalidParameter 且计数仍为 0 时为 true。
bool pendingWhileDisconnected() {
    FakeTransport transport;
    DeviceSession session{transport};
    transport.session = &session;

    if (!failedAs(session.notePending(), ErrorCode::InvalidParameter) || session.pendingCount() != 0) {
        return fail("pending_while_disconnected");
    }
    return true;
}

/// @brief 尚未连接时 disconnect 成功，并停在没有代号的 Disconnected。
/// @return 成功且 currentId() 为 none() 时为 true。
bool disconnectWhileDisconnected() {
    FakeTransport transport;
    DeviceSession session{transport};
    transport.session = &session;

    if (!succeeded(session.disconnect())) {
        return fail("disconnect_while_disconnected");
    }
    if (session.state() != SessionState::Disconnected || !idIsNone(session.currentId())) {
        return fail("disconnect_while_disconnected");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。调用方拥有这个标志。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "connect_sees_connecting") {
        return connectSeesConnecting();
    }
    if (name == "open_failure_retires_id") {
        return openFailureRetiresId();
    }
    if (name == "disconnect_drops_pending_and_old_id") {
        return disconnectDropsPendingAndOldId();
    }
    if (name == "disconnect_drains_closed") {
        return disconnectDrainsClosed();
    }
    if (name == "degraded_keeps_id") {
        return degradedKeepsId();
    }
    if (name == "second_connect_rejected") {
        return secondConnectRejected();
    }
    if (name == "pending_while_disconnected") {
        return pendingWhileDisconnected();
    }
    if (name == "disconnect_while_disconnected") {
        return disconnectWhileDisconnected();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个设备会话单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_device_session_tests <case>\n";
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
