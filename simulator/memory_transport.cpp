#include "memory_transport.hpp"

#include "faults/fault_apply.hpp"

namespace ernc::simulator {

MemorySlaveTransport::MemorySlaveTransport(SlaveImage& image, FaultSource* faults)
    : image_(image), faults_(faults) {}

ernc::domain::Result<void> MemorySlaveTransport::open() {
    open_ = true;
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> MemorySlaveTransport::close() {
    using ernc::ports::TransportStep;
    if (!open_) {
        return ernc::domain::Result<void>::success();
    }
    open_ = false;
    TransportStep step;
    step.kind = TransportStep::Kind::Closed;
    queued_.push_back(std::move(step));
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> MemorySlaveTransport::submit(std::span<const std::uint8_t> bytes) {
    using ernc::domain::ErrorCode;
    using ernc::domain::Result;
    using ernc::ports::TransportStep;
    if (!open_) {
        return Result<void>::failure(ErrorCode::DeviceDisconnected);
    }
    if (bytes.empty()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (faults_ != nullptr) {
        const FaultReply fault = applyFault(image_, bytes, faults_->next());
        if (fault.kind == FaultKind::Disconnect) {
            open_ = false;
            TransportStep closed;
            closed.kind = TransportStep::Kind::Closed;
            queued_.push_back(std::move(closed));
            return Result<void>::success();
        }
        if (fault.delayPolls > 0 && !fault.frame.empty()) {
            held_ = fault.frame;
            delayRemaining_ = fault.delayPolls;
            return Result<void>::success();
        }
        if (fault.deliversBytes && !fault.frame.empty()) {
            TransportStep step;
            step.kind = TransportStep::Kind::Bytes;
            step.bytes = fault.frame;
            queued_.push_back(std::move(step));
        }
        return Result<void>::success();
    }
    const SlaveReply reply = exchange(image_, bytes);
    if (reply.responded) {
        TransportStep step;
        step.kind = TransportStep::Kind::Bytes;
        step.bytes = reply.frame;
        queued_.push_back(std::move(step));
    }
    return Result<void>::success();
}

ernc::ports::TransportStep MemorySlaveTransport::poll() {
    if (delayRemaining_ > 0) {
        --delayRemaining_;
        return ernc::ports::TransportStep{};
    }
    if (!held_.empty()) {
        ernc::ports::TransportStep step;
        step.kind = ernc::ports::TransportStep::Kind::Bytes;
        step.bytes = std::move(held_);
        held_.clear();
        return step;
    }
    if (queued_.empty()) {
        return ernc::ports::TransportStep{};
    }
    ernc::ports::TransportStep step = std::move(queued_.front());
    queued_.erase(queued_.begin());
    return step;
}

}  // namespace ernc::simulator
