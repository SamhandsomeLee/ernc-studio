#pragma once

#include "ernc/domain/types.hpp"
#include "ernc/protocol/modbus_fc03.hpp"

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace ernc::protocol {

/// 一次 FC16 写入允许的最大逻辑 slot 数。
/// 旧版 CHNB-NEW-6MIC @ fbecac9 的 MAX_BULK_SLOTS_WRITE。
/// Modbus 本体是 9 + 4N 字节；62 个 slot 时本体为 257 字节，超过长度字段能表示的 255。
constexpr std::uint16_t kMaxFc16WriteSlots = 61;

/// @brief 一次 FC16 写请求的地址范围。
///
/// 数据字节由 encodeFc16Write 的另一个参数传入。本对象不保存这些字节，也不访问设备。
struct Fc16WriteRequest {
    /// 起始逻辑 slot。0 表示起始寄存器 0。线上的寄存器地址是该值乘 2。
    std::uint16_t firstSlot{0};

    /// 连续写入的 slot 数。0 和大于 kMaxFc16WriteSlots 的值会在组帧前被拒绝。每个 slot 对应 2 个寄存器、4 个数据字节。
    std::uint16_t slotCount{0};

    /// 写入请求帧的从站地址。默认是已确认的 0x01。其他值只表示这个字节被写进帧，不表示该地址已在设备上确认。
    std::uint8_t slaveAddress{kConfirmedSlaveAddress};
};

/// @brief 与请求一致的 FC16 ACK 回显。
///
/// 只记录从站、起始寄存器和寄存器数量。没有 Verified 标志，也不更新 deviceValue。
struct Fc16Ack {
    /// ACK 中的从站地址字节。它已经和请求里的从站地址比对过。
    std::uint8_t slaveAddress{0};

    /// ACK 中的起始寄存器，大端解码后的数值。它不是逻辑 slot。
    std::uint16_t startRegister{0};

    /// ACK 中的寄存器数量，大端解码后的数值。1 个 slot 对应 2。
    std::uint16_t registerCount{0};
};

/// @brief 一帧结构合法的 FC16 异常响应。
///
/// 异常码放在这里。ErrorCode::ModbusException 不保存具体异常码。
struct Fc16Exception {
    /// 响应中的从站地址字节。异常帧里没有寄存器地址，因此不和请求的 slot 比对。
    std::uint8_t slaveAddress{0};

    /// Modbus 异常码。它不是 ErrorCode 的整数值。
    std::uint8_t code{0};
};

/// 一帧 FC16 响应的两种合法形态：地址和数量都匹配的 ACK，或带异常码的异常应答。
using Fc16Response = std::variant<Fc16Ack, Fc16Exception>;

/// @brief 把一次 FC16 写请求编码成可发送的长度前缀帧。
///
/// 起始寄存器和寄存器数量使用大端，依据是旧版 build_write_registers 的 struct.pack(">BBHH")。
/// CRC 使用低字节在前。数据区按 payload 的原顺序写入，不把 float 或整数转换成小端。
/// 证据等级：报文头和 CRC 的字节序是旧版源码观察；从站默认值 0x01 已由负责人确认。没有已批准黄金帧。
/// @param request 写入范围，只在本次调用期间借用。firstSlot 是逻辑 slot，slotCount 是 slot 数。
/// @param payload 数据区字节，只在本次调用期间借用。长度必须是 slotCount 乘 4。顺序保持不变。
/// @return 成功时调用方拥有长度前缀帧。数量或寄存器范围非法时错误码为 InvalidParameter，没有输出缓冲区。
/// 数据长度和 slot 数不一致时错误码为 PayloadSizeMismatch。分配失败时抛出 std::bad_alloc。
ernc::domain::Result<std::vector<std::uint8_t>> encodeFc16Write(const Fc16WriteRequest& request,
                                                                std::span<const std::uint8_t> payload);

/// @brief 核对一帧 FC16 ACK 的从站、起始寄存器和寄存器数量。
///
/// 输入不含外层长度字节。成功只表示回显字段和 request 一致，不是读回 Verified，也不修改 RuntimeStore。
/// 功能码 0x90 且带一个异常码时，结果里保留异常码，不把它收成没有代码的错误。
/// @param request 这次写入的范围，只在本次调用期间借用。用它计算期望的寄存器地址和数量。
/// @param modbusFrame 含 CRC 的 Modbus RTU 本体，只在本次调用期间借用。
/// @return 回显一致时为 Fc16Ack。异常帧合法时为 Fc16Exception。
/// 从站、起始寄存器或数量不一致时错误码为 ResponseMismatch。CRC 不符时为 CrcMismatch。
/// 帧长和 FC16 布局不一致时为 PayloadSizeMismatch。请求范围本身非法，或功能码不是 0x10 或 0x90 时为 InvalidParameter。
ernc::domain::Result<Fc16Response> decodeFc16Ack(const Fc16WriteRequest& request,
                                                 std::span<const std::uint8_t> modbusFrame);

}  // namespace ernc::protocol
