#pragma once

#include "ernc/domain/types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ernc::protocol {

/// @brief 按旧版 CRC16-Modbus 计算一段字节的校验值。
///
/// 寄存器初值是 0xFFFF。每个字节先异或进寄存器，再右移 8 次；右移前最低位为 1 时，结果再异或 0xA001。
/// 证据等级：旧版源码观察，CHNB-NEW-6MIC @ fbecac9 的 calc_crc16。WIRE-CONTRACT 仍是 Needs Evidence。
/// 当前没有已批准 Golden。本函数不识别外层长度字节，也不访问串口或设备。
/// @param covered 被校验覆盖的字节，只在本次调用期间借用。长度可以为 0；空序列得到初值 0xFFFF。
/// @return 16 位校验值，范围 0x0000–0xFFFF。低 8 位是附加时写出的第一个字节。
std::uint16_t calculateCrc16(std::span<const std::uint8_t> covered) noexcept;

/// @brief 在覆盖字节后面附加小端 CRC。
///
/// 不检查 255 字节的外层长度上限。不修改调用方传入的字节。
/// @param covered 被校验覆盖的字节，只在本次调用期间借用。长度可以为 0。
/// @return 调用方拥有的新缓冲区：原字节之后依次是 CRC 低字节和高字节。空输入得到 FF FF。
/// 分配失败时抛出 std::bad_alloc。
std::vector<std::uint8_t> appendCrc16(std::span<const std::uint8_t> covered);

/// @brief 核对缓冲区末尾两字节是不是覆盖部分的小端 CRC。
///
/// 成功只表示末尾 CRC 与重新计算的结果相同。这不是设备 ACK，也不是读回 Verified。
/// 总长度小于 3 时拆不出“覆盖字节 + 2 字节 CRC”，返回 InvalidParameter。
/// 旧版 verify_crc16 对短于 3 字节的输入返回 false。
/// @param frameWithCrc 覆盖字节再加末尾 2 字节 CRC，只在本次调用期间借用。
/// @return 相符时成功。长度小于 3 时错误码为 InvalidParameter。CRC 不符时错误码为 CrcMismatch。
ernc::domain::Result<void> verifyCrc16(std::span<const std::uint8_t> frameWithCrc) noexcept;

}  // namespace ernc::protocol
