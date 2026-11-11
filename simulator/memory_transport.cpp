#include "memory_transport.hpp"

namespace ernc::simulator {

MemorySlaveTransport::MemorySlaveTransport(SlaveImage& image) : image_(image) {}

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
    if (queued_.empty()) {
        return ernc::ports::TransportStep{};
    }
    ernc::ports::TransportStep step = std::move(queued_.front());
    queued_.erase(queued_.begin());
    return step;
}

}  // namespace ernc::simulator
