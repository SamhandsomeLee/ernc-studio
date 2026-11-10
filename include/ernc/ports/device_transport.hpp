#pragma once

#include "ernc/domain/types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace ernc::ports {

/// @brief 从端口取走的一步传输结果。
///
/// Idle 表示当前没有事件。Bytes、Fault、Closed 才是异步响应。
/// 这三种都不是 Modbus ACK，也不是读回 Verified。
/// 对象按值交给调用方，调用方拥有其中的字节副本。
struct TransportStep {
    /// 这一步是空闲、收到字节、传输故障，还是本地关闭完成。
    enum class Kind {
        /// 队列里没有事件。bytes 为空，error 为空。
        Idle,

        /// 端口交还一段已经收到的线字节。
        Bytes,

        /// 打开之后发生传输故障，或对端断开。本地端口同时回到关闭。
        Fault,

        /// 调用方主动 close 之后的那一次关闭完成。
        Closed
    };

    /// 本步类别。Idle 以外才表示发生了一件传输事件。
    Kind kind{Kind::Idle};

    /// Bytes 时的线字节副本。其他类别为空。调用方拥有这份副本。
    std::vector<std::uint8_t> bytes;

    /// Fault 时的失败类别。其他类别为空。它不是 Modbus 异常码。
    std::optional<ernc::domain::ErrorCode> error;
};

/// @brief 设备字节端口。模拟器和串口适配器以后实现它。
///
/// 只接收和交还线字节。不拆帧、不算 CRC、不识别 Modbus，也不保存 CommandId 或 ParameterId。
/// open、close、submit、poll 不加内部锁，同一时刻只由一个所有者线程调用。
/// 接口不创建线程，也不继承 QObject。头文件不出现 QSerialPort。
/// 具体对象用 std::unique_ptr 持有。本类型禁止复制和移动，避免把端口对象切成两份。
///
/// 状态只有关闭和打开。新建时为关闭。
/// open 成功进入打开，失败码为 SerialOpenFailed。重复 open 返回 InvalidParameter，状态不变。
/// close 在已关闭时再次调用仍成功，并且不再产生 Closed。
/// 从打开状态 close 成功后，随后的 poll 先得到一次 Closed，然后回到关闭。
/// submit 在关闭状态返回 DeviceDisconnected，且不产生 Bytes。
/// submit 的空序列返回 InvalidParameter。
/// submit 成功只表示整段字节已被收下，不表示设备已经响应。
/// 已打开但整段没有被收下时返回 SerialIoFailed，且不产生 Bytes。
/// Fault 使用 DeviceDisconnected 表示对端断开，使用 SerialIoFailed 表示打开之后的传输失败。
/// 产生 Fault 后本地回到关闭，之后的 submit 按关闭状态处理。
class IDeviceTransport {
public:
    /// @brief 建立关闭状态的端口基类。派生类构造自己的端点。
    IDeviceTransport() = default;

    /// @brief 释放派生类资源。具体端口须在析构前结束自己的打开状态。
    virtual ~IDeviceTransport() = default;

    /// @brief 禁止复制。端口的打开状态只有一份。
    IDeviceTransport(const IDeviceTransport&) = delete;

    /// @brief 禁止复制赋值。
    IDeviceTransport& operator=(const IDeviceTransport&) = delete;

    /// @brief 禁止移动。所有者通过 unique_ptr 移交具体对象。
    IDeviceTransport(IDeviceTransport&&) = delete;

    /// @brief 禁止移动赋值。
    IDeviceTransport& operator=(IDeviceTransport&&) = delete;

    /// @brief 打开本地端点。
    /// @return 成功时进入打开状态。此时还没有设备响应。
    /// 重复打开时为 InvalidParameter，状态保持打开。端点打不开时为 SerialOpenFailed，状态保持关闭。
    virtual ernc::domain::Result<void> open() = 0;

    /// @brief 关闭本地端点。
    /// @return 成功表示调用方侧不再发送。已关闭时再次调用仍成功。
    /// 从打开状态关闭时，之后第一次取到的非空闲步骤是一次 Closed。
    virtual ernc::domain::Result<void> close() = 0;

    /// @brief 收下一段待发送的线字节。
    /// @param bytes 调用方已经组好的线字节，只在本次调用期间借用。空序列会被拒绝。
    /// 端口不检查这些字节是不是 Modbus。具体类若在返回后仍要使用它们，须自行复制。
    /// @return 已打开且整段被收下时成功。成功不产生响应事件。
    /// 关闭状态时为 DeviceDisconnected。空序列时为 InvalidParameter。
    /// 已打开但整段未被收下时为 SerialIoFailed。这三种失败都不产生 Bytes。
    virtual ernc::domain::Result<void> submit(std::span<const std::uint8_t> bytes) = 0;

    /// @brief 取走队列中的下一步。
    /// @return 没有事件时为 Idle。Bytes 时 bytes 为端口自己的副本且 error 为空。
    /// Fault 时 error 有值且 bytes 为空。Closed 时两者都空。
    /// 返回值按值交给调用方。
    virtual TransportStep poll() = 0;
};

}  // namespace ernc::ports
