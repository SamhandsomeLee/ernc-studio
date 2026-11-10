#pragma once

#include "ernc/domain/types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ernc::protocol {

/// @brief 给已经组好的 Modbus RTU 字节加上 1 字节长度前缀。
///
/// 长度等于后续字节数，不把长度字节自己算进去，也不把结果补齐到 256 字节。
/// 输入应为已经含 CRC 的 Modbus 本体。本函数不检查功能码，也不调用 verifyCrc16。
/// 证据等级：旧版源码观察，CHNB-NEW-6MIC @ fbecac9 的 build_packet 与 MAX_MODBUS_PAYLOAD。
/// WIRE-CONTRACT 仍是 Needs Evidence。当前没有已批准 Golden。不访问串口或设备。
/// @param modbusFrame 被前缀的 Modbus RTU 字节，只在本次调用期间借用。长度 0 到 255 都可编码。
/// @return 成功时调用方拥有新缓冲区：第 1 字节是长度，后面原样复制输入。总长度是输入长度加 1。
/// 输入长于 255 字节时失败，错误码为 FrameLengthInvalid，不返回半截缓冲区。
/// 分配失败时抛出 std::bad_alloc。
ernc::domain::Result<std::vector<std::uint8_t>> encodeLengthPrefixed(
    std::span<const std::uint8_t> modbusFrame);

}  // namespace ernc::protocol
