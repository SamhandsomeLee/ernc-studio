#pragma once

#include "ernc/ports/device_transport.hpp"
#include "faults/fault_sequence.hpp"
#include "slave_image.hpp"

#include <vector>

namespace ernc::simulator {

/// @brief 把虚拟从站接到字节端口。
///
/// 持有调用方的 SlaveImage 引用，不拥有它。submit 把请求交给 exchange。
/// 从站给出响应时，poll 交还那一帧 Bytes。没有响应时不产生 Bytes。
/// 不创建线程，也不使用 QSerialPort。
class MemorySlaveTransport final : public ernc::ports::IDeviceTransport {
public:
    /// @brief 绑定一块从站内存。初始为关闭，不施加故障。
    /// @param image 从站内存，只借用。调用方保证它在本对象存活期间有效。
    /// @param faults 可选的故障来源。为空时每次请求都走正常 exchange。调用方拥有它，并保证它在本对象存活期间有效。
    explicit MemorySlaveTransport(SlaveImage& image, FaultSource* faults = nullptr);

    /// @brief 打开内存端口。
    /// @return 进入打开并成功。成功只表示随后可以 submit。
    ernc::domain::Result<void> open() override;

    /// @brief 关闭内存端口。
    /// @return 已经关闭时成功且不产生步骤。从打开状态关闭时排入一次 Closed。
    ernc::domain::Result<void> close() override;

    /// @brief 把一帧请求交给从站。
    /// @param bytes 带长度前缀的请求，只在本次调用期间借用。空序列被拒绝。
    /// @return 已打开且字节非空时成功。成功只表示请求已被从站处理，不表示一定有响应。
    /// 未打开时为 DeviceDisconnected。空序列时为 InvalidParameter。
    ernc::domain::Result<void> submit(std::span<const std::uint8_t> bytes) override;

    /// @brief 取走从站响应或关闭步骤。
    /// @return 没有步骤时为 Idle。Bytes 是从站响应帧的副本。
    ernc::ports::TransportStep poll() override;

private:
    /// 调用方拥有的从站内存。
    SlaveImage& image_;

    /// 当前是否打开。新建时为 false。
    bool open_{false};

    /// poll 尚未取走的步骤。
    std::vector<ernc::ports::TransportStep> queued_;

    /// 可选故障来源。为空时不改变正常响应。本对象不拥有它。
    FaultSource* faults_{nullptr};

    /// Delay 还要空闲的 poll 次数。0 表示没有延迟。
    std::uint8_t delayRemaining_{0};

    /// Delay 到期后才交还的正常响应。为空表示没有暂存帧。
    std::vector<std::uint8_t> held_;
};

}  // namespace ernc::simulator
