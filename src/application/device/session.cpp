#include "session.hpp"

#include <limits>

namespace ernc::application {

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::SessionId;
using ernc::ports::TransportStep;

/// @brief 取出失败结果里的错误码。
/// @param result 已经判定为失败的结果，只在本次调用期间借用。
/// @return 结果里的错误码。失败对象没有错误码时为 Unknown。
ErrorCode codeOf(const Result<void>& result) {
    const ErrorCode* code = result.tryError();
    if (code == nullptr) {
        return ErrorCode::Unknown;
    }
    return *code;
}

}  // namespace

DeviceSession::DeviceSession(ernc::ports::IDeviceTransport& transport) : transport_(transport) {}

SessionState DeviceSession::state() const noexcept {
    return state_;
}

SessionId DeviceSession::currentId() const noexcept {
    return currentId_;
}

Result<SessionId> DeviceSession::connect() {
    if (state_ != SessionState::Disconnected) {
        return Result<SessionId>::failure(ErrorCode::InvalidParameter);
    }
    if (nextRaw_ == 0) {
        return Result<SessionId>::failure(ErrorCode::InvalidParameter);
    }
    const SessionId id = SessionId::fromRaw(nextRaw_);
    if (nextRaw_ == std::numeric_limits<std::uint64_t>::max()) {
        nextRaw_ = 0;
    } else {
        ++nextRaw_;
    }
    currentId_ = id;
    state_ = SessionState::Connecting;
    const Result<void> opened = transport_.open();
    // open 期间若重入了 disconnect，状态已不再是 Connecting，不能再标成已连接。
    if (state_ != SessionState::Connecting) {
        return Result<SessionId>::failure(ErrorCode::OperationCancelled);
    }
    if (!opened.ok()) {
        currentId_ = SessionId::none();
        state_ = SessionState::Disconnected;
        return Result<SessionId>::failure(codeOf(opened));
    }
    state_ = SessionState::Connected;
    return Result<SessionId>::success(id);
}

Result<void> DeviceSession::disconnect() {
    if (state_ == SessionState::Disconnected || state_ == SessionState::Disconnecting) {
        return Result<void>::success();
    }
    state_ = SessionState::Disconnecting;
    pending_ = 0;
    currentId_ = SessionId::none();
    const Result<void> closed = transport_.close();
    for (;;) {
        const TransportStep step = transport_.poll();
        if (step.kind == TransportStep::Kind::Idle) {
            break;
        }
    }
    state_ = SessionState::Disconnected;
    if (!closed.ok()) {
        return Result<void>::failure(codeOf(closed));
    }
    return Result<void>::success();
}

Result<void> DeviceSession::markDegraded() {
    if (state_ != SessionState::Connected) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    state_ = SessionState::Degraded;
    return Result<void>::success();
}

Result<void> DeviceSession::notePending() {
    if (state_ != SessionState::Connected && state_ != SessionState::Degraded) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    ++pending_;
    return Result<void>::success();
}

bool DeviceSession::acceptResult(SessionId id) const noexcept {
    if (state_ != SessionState::Connected && state_ != SessionState::Degraded) {
        return false;
    }
    return id.valid() && id == currentId_;
}

std::size_t DeviceSession::pendingCount() const noexcept {
    return pending_;
}

}  // namespace ernc::application
