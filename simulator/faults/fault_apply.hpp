#pragma once

#include "slave_image.hpp"

#include "faults/fault_sequence.hpp"

#include <cstdint>
#include <vector>

namespace ernc::simulator {

/// @brief 一次故障作用后的交还结果。
///
/// countsAsSuccess 为 true 只表示主机可以把它当作从站的正常响应。
/// Drop、BadFrame、BadCrc、Disconnect 都不是成功。
struct FaultReply {
    /// 这次实际施加的故障。
    FaultKind kind{FaultKind::None};

    /// 主机这次能否拿到字节。Delay 在放出之前为 false。
    bool deliversBytes{false};

    /// 这些字节是不是从站的正常成功响应。坏帧和坏 CRC 即使有字节也是 false。
    bool countsAsSuccess{false};

    /// 要交还或暂存的帧。调用方拥有这份副本。没有字节时为空。
    std::vector<std::uint8_t> frame;

    /// Delay 时为 1，表示先空闲一拍 poll。其他故障为 0。
    std::uint8_t delayPolls{0};
};

/// @brief 按一种故障处理一帧请求。
///
/// Drop 和 Disconnect 不调用 exchange，因此镜像不变。Delay、BadFrame、BadCrc 会先让从站处理请求。
/// 不睡眠，也不读取系统钟。
/// @param image 从站内存。只有故障允许请求到达时，合法 FC16 才会修改它。
/// @param request 带长度前缀的请求，只在本次调用期间借用。
/// @param kind 这次要施加的故障。
/// @return 调用方拥有的故障结果。countsAsSuccess 为 false 时不能当成从站成功。
FaultReply applyFault(SlaveImage& image, std::span<const std::uint8_t> request, FaultKind kind);

}  // namespace ernc::simulator
