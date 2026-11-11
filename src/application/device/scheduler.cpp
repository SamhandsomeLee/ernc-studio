#include "scheduler.hpp"

#include "ernc/protocol/modbus_fc03.hpp"
#include "ernc/protocol/modbus_fc16.hpp"

#include <limits>
#include <utility>

namespace ernc::application {

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::ports::TransportStep;
using ernc::protocol::Fc03Exception;
using ernc::protocol::Fc03Payload;
using ernc::protocol::Fc03ReadRequest;
using ernc::protocol::Fc16Ack;
using ernc::protocol::Fc16Exception;
using ernc::protocol::Fc16WriteRequest;
using ernc::protocol::ParseStep;

/// @brief 取出失败结果里的错误码。
/// @param result 已经判定为失败的结果，只在本次调用期间借用。
/// @return 结果里的错误码。失败对象没有错误码时为 Unknown。
template <typename T>
ErrorCode codeOf(const Result<T>& result) {
    const ErrorCode* code = result.tryError();
    if (code == nullptr) {
        return ErrorCode::Unknown;
    }
    return *code;
}

/// @brief 把请求编码成一帧长度前缀字节。
/// @param request 入队请求，只在本次调用期间借用。
/// @return 编码成功时为整帧。范围或 payload 不合法时带回编码器的错误码。
Result<std::vector<std::uint8_t>> encodeRequest(const CommandRequest& request) {
    if (request.op == CommandOp::Read) {
        const Fc03ReadRequest read{request.firstSlot, request.slotCount, request.slaveAddress};
        return ernc::protocol::encodeFc03Read(read);
    }
    const Fc16WriteRequest write{request.firstSlot, request.slotCount, request.slaveAddress};
    return ernc::protocol::encodeFc16Write(write, request.payload);
}

}  // namespace

CommandScheduler::CommandScheduler(DeviceSession& session, ernc::ports::IDeviceTransport& transport)
    : session_(session), transport_(transport) {}

Result<CommandId> CommandScheduler::enqueue(const CommandRequest& request) {
    if (session_.state() != SessionState::Connected && session_.state() != SessionState::Degraded) {
        return Result<CommandId>::failure(ErrorCode::DeviceDisconnected);
    }
    auto wire = encodeRequest(request);
    if (!wire.ok()) {
        return Result<CommandId>::failure(codeOf(wire));
    }
    const std::vector<std::uint8_t>* encoded = wire.tryValue();
    if (encoded == nullptr) {
        return Result<CommandId>::failure(ErrorCode::Unknown);
    }
    const std::optional<CommandId> id = allocateId();
    if (!id.has_value()) {
        return Result<CommandId>::failure(ErrorCode::InvalidParameter);
    }
    PendingCommand command;
    command.id = *id;
    command.session = session_.currentId();
    command.request = request;
    command.wire = *encoded;
    if (inflight_.has_value()) {
        enqueueWaiting(std::move(command));
        return Result<CommandId>::success(*id);
    }
    const Result<void> sent = transport_.submit(command.wire);
    if (!sent.ok()) {
        completeFailure(command, codeOf(sent));
        return Result<CommandId>::success(*id);
    }
    inflight_ = std::move(command);
    return Result<CommandId>::success(*id);
}

void CommandScheduler::pump() {
    dropRejected();
    readAvailable();
    dropRejected();
    startNext();
}

void CommandScheduler::pump(RetryTime now, const ReadRetryLimit& limit) {
    dropRejected();
    readAvailable();
    dropRejected();
    applyReadRetry(now, limit);
    startNext();
}

std::size_t CommandScheduler::inFlightCount() const noexcept {
    return inflight_.has_value() ? 1U : 0U;
}

std::optional<CommandOutcome> CommandScheduler::takeResult(CommandId id) {
    for (auto it = finished_.begin(); it != finished_.end(); ++it) {
        if (it->id == id) {
            CommandOutcome outcome = std::move(*it);
            finished_.erase(it);
            return outcome;
        }
    }
    return std::nullopt;
}

std::optional<CommandId> CommandScheduler::allocateId() {
    if (nextRaw_ == 0) {
        return std::nullopt;
    }
    const CommandId id = CommandId::fromRaw(nextRaw_);
    if (nextRaw_ == std::numeric_limits<std::uint64_t>::max()) {
        nextRaw_ = 0;
    } else {
        ++nextRaw_;
    }
    return id;
}

void CommandScheduler::enqueueWaiting(PendingCommand command) {
    if (command.request.priority == CommandPriority::High) {
        auto place = waiting_.begin();
        while (place != waiting_.end() && place->request.priority == CommandPriority::High) {
            ++place;
        }
        waiting_.insert(place, std::move(command));
        return;
    }
    waiting_.push_back(std::move(command));
}

void CommandScheduler::dropRejected() {
    if (inflight_.has_value() && !session_.acceptResult(inflight_->session)) {
        parser_.clear();
        failInFlight(ErrorCode::DeviceDisconnected);
    }
    std::deque<PendingCommand> kept;
    while (!waiting_.empty()) {
        PendingCommand command = std::move(waiting_.front());
        waiting_.pop_front();
        if (!session_.acceptResult(command.session)) {
            completeFailure(command, ErrorCode::DeviceDisconnected);
            continue;
        }
        kept.push_back(std::move(command));
    }
    waiting_ = std::move(kept);
}

void CommandScheduler::readAvailable() {
    for (;;) {
        const TransportStep step = transport_.poll();
        if (step.kind == TransportStep::Kind::Idle) {
            return;
        }
        if (step.kind == TransportStep::Kind::Fault || step.kind == TransportStep::Kind::Closed) {
            parser_.clear();
            if (inflight_.has_value()) {
                const ErrorCode code = step.error.has_value() ? *step.error : ErrorCode::DeviceDisconnected;
                failInFlight(code);
            }
            continue;
        }
        if (!inflight_.has_value() || step.bytes.empty()) {
            continue;
        }
        parser_.append(step.bytes);
        consumeParser();
    }
}

void CommandScheduler::consumeParser() {
    for (;;) {
        const ParseStep step = parser_.pull();
        if (step.kind == ParseStep::Kind::NeedMore) {
            return;
        }
        if (step.kind == ParseStep::Kind::LengthDropped) {
            continue;
        }
        if (step.kind == ParseStep::Kind::CrcRejected) {
            if (inflight_.has_value()) {
                failInFlight(ErrorCode::CrcMismatch);
            }
            return;
        }
        if (step.kind == ParseStep::Kind::Frame && inflight_.has_value()) {
            matchFrame(step.frame);
        }
        return;
    }
}

void CommandScheduler::matchFrame(std::span<const std::uint8_t> frame) {
    if (!inflight_.has_value()) {
        return;
    }
    if (frame.size() < 2) {
        failInFlight(ErrorCode::ResponseMismatch);
        return;
    }
    const std::span<const std::uint8_t> modbus{frame.data() + 1, frame.size() - 1};
    const PendingCommand command = std::move(*inflight_);
    inflight_.reset();
    if (command.request.op == CommandOp::Read) {
        const Result<ernc::protocol::Fc03Response> decoded = ernc::protocol::decodeFc03Response(modbus);
        if (!decoded.ok()) {
            completeFailure(command, codeOf(decoded));
            return;
        }
        const ernc::protocol::Fc03Response& response = *decoded.tryValue();
        if (const auto* exception = std::get_if<Fc03Exception>(&response)) {
            completeFailure(command, ErrorCode::ModbusException, exception->code);
            return;
        }
        const auto& payload = std::get<Fc03Payload>(response);
        if (payload.slaveAddress != command.request.slaveAddress ||
            payload.slotCount != command.request.slotCount) {
            completeFailure(command, ErrorCode::ResponseMismatch);
            return;
        }
        ReadCommandResult read;
        read.slaveAddress = payload.slaveAddress;
        read.payload = payload.bytes;
        read.slotCount = payload.slotCount;
        CommandOutcome outcome;
        outcome.id = command.id;
        outcome.session = command.session;
        outcome.body = std::move(read);
        finished_.push_back(std::move(outcome));
        return;
    }
    const Fc16WriteRequest write{command.request.firstSlot, command.request.slotCount,
                                 command.request.slaveAddress};
    const Result<ernc::protocol::Fc16Response> decoded = ernc::protocol::decodeFc16Ack(write, modbus);
    if (!decoded.ok()) {
        completeFailure(command, codeOf(decoded));
        return;
    }
    const ernc::protocol::Fc16Response& response = *decoded.tryValue();
    if (const auto* exception = std::get_if<Fc16Exception>(&response)) {
        completeFailure(command, ErrorCode::ModbusException, exception->code);
        return;
    }
    const auto& ack = std::get<Fc16Ack>(response);
    WriteCommandResult written;
    written.slaveAddress = ack.slaveAddress;
    written.startRegister = ack.startRegister;
    written.registerCount = ack.registerCount;
    CommandOutcome outcome;
    outcome.id = command.id;
    outcome.session = command.session;
    outcome.body = std::move(written);
    finished_.push_back(std::move(outcome));
}

void CommandScheduler::applyReadRetry(RetryTime now, const ReadRetryLimit& limit) {
    if (!inflight_.has_value() || inflight_->request.op != CommandOp::Read) {
        return;
    }
    if (inflight_->attempt == 0) {
        inflight_->attempt = 1;
        inflight_->sentAt = now;
    }
    const ReadRetryStep step = decideReadRetry(limit, inflight_->sentAt, now, inflight_->attempt);
    if (step.decision == ReadRetryDecision::Waiting) {
        return;
    }
    if (step.decision == ReadRetryDecision::Resubmit) {
        parser_.clear();
        const Result<void> sent = transport_.submit(inflight_->wire);
        if (!sent.ok()) {
            failInFlight(codeOf(sent));
            return;
        }
        inflight_->attempt = static_cast<std::uint8_t>(inflight_->attempt + 1);
        inflight_->sentAt = now;
        return;
    }
    parser_.clear();
    failInFlight(step.terminal);
}

void CommandScheduler::startNext() {
    while (!inflight_.has_value()) {
        if (waiting_.empty()) {
            return;
        }
        PendingCommand command = std::move(waiting_.front());
        waiting_.pop_front();
        if (!session_.acceptResult(command.session)) {
            completeFailure(command, ErrorCode::DeviceDisconnected);
            continue;
        }
        const Result<void> sent = transport_.submit(command.wire);
        if (!sent.ok()) {
            completeFailure(command, codeOf(sent));
            continue;
        }
        inflight_ = std::move(command);
    }
}

void CommandScheduler::completeFailure(const PendingCommand& command, ErrorCode code,
                                       std::optional<std::uint8_t> exceptionCode) {
    FailedCommand failed;
    failed.code = code;
    failed.exceptionCode = exceptionCode;
    CommandOutcome outcome;
    outcome.id = command.id;
    outcome.session = command.session;
    outcome.body = std::move(failed);
    finished_.push_back(std::move(outcome));
}

void CommandScheduler::failInFlight(ErrorCode code, std::optional<std::uint8_t> exceptionCode) {
    if (!inflight_.has_value()) {
        return;
    }
    const PendingCommand command = std::move(*inflight_);
    inflight_.reset();
    completeFailure(command, code, exceptionCode);
}

}  // namespace ernc::application
