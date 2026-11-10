#pragma once

#include "ernc/domain/types.hpp"

#include <cstdint>
#include <span>
#include <variant>
#include <vector>

namespace ernc::protocol {

/// 当前设备已确认的 Modbus 从站地址。
/// 负责人于 2026-11-10 确认。请求里的从站字段默认使用它，调用方仍可传入其他地址字节。
constexpr std::uint8_t kConfirmedSlaveAddress = 0x01;

/// 一次 FC03 读取允许的最大逻辑 slot 数。
/// 旧版 CHNB-NEW-6MIC @ fbecac9 的 MAX_BULK_SLOTS_READ。63 个 slot 的响应数据是 252 字节，
/// 加上从站、功能码、字节数和 CRC 后超过 1 字节长度字段能表示的 255。
constexpr std::uint16_t kMaxFc03ReadSlots = 62;

/// @brief 一次 FC03 读请求的参数。
///
/// 本对象只描述要读的范围，不保存线字节，也不访问设备。
struct Fc03ReadRequest {
    /// 起始逻辑 slot。0 表示起始寄存器 0。它不是 Modbus 寄存器地址，线上的寄存器地址是该值乘 2。
    std::uint16_t firstSlot{0};

    /// 连续读取的 slot 数。0 和大于 kMaxFc03ReadSlots 的值会在组帧前被拒绝。每个 slot 对应 2 个寄存器。
    std::uint16_t slotCount{0};

    /// 写入请求帧的从站地址。默认是已确认的 0x01。其他值只表示这个字节被写进帧，不表示该地址已在设备上确认。
    std::uint8_t slaveAddress{kConfirmedSlaveAddress};
};

/// @brief 一帧结构合法的 FC03 正常响应里的原始数据。
///
/// 数据保持线上的字节顺序。旧版把每 4 字节按小端解释成 float、int32 或 uint32，那一步不在这里。
struct Fc03Payload {
    /// 响应中的从站地址字节。本类型不把它和请求里的地址比对。
    std::uint8_t slaveAddress{0};

    /// 功能码之后的数据区。长度是 4 的倍数，单位是字节。调用方拥有这份副本。
    std::vector<std::uint8_t> bytes;

    /// bytes 中包含的 slot 数，等于字节数除以 4。0 不会由解码成功路径产生。
    std::uint16_t slotCount{0};
};

/// @brief 一帧结构合法的 FC03 异常响应。
///
/// 异常码放在这里。ErrorCode::ModbusException 不保存具体异常码，因此解码成功时不使用那个错误码。
struct Fc03Exception {
    /// 响应中的从站地址字节。
    std::uint8_t slaveAddress{0};

    /// Modbus 异常码，例如 0x02。它不是 ErrorCode 的整数值。
    std::uint8_t code{0};
};

/// 一帧 FC03 响应的两种合法形态。当前存放的是 Fc03Payload 或 Fc03Exception。
/// CRC 错误和结构错误不放进这个 variant。
using Fc03Response = std::variant<Fc03Payload, Fc03Exception>;

/// @brief 把一次 FC03 读请求编码成可发送的长度前缀帧。
///
/// 寄存器地址和数量使用大端，依据是旧版 build_read_registers 的 struct.pack(">BBHH")。
/// CRC 使用低字节在前，依据是 append_crc16 的 struct.pack("<H")。然后调用 encodeLengthPrefixed。
/// 请求本体固定 8 字节，不随 slot 数量变长。数量为 0 或大于 62 时不产生任何线字节。
/// 证据等级：报文头和 CRC 的字节序是旧版源码观察；从站默认值 0x01 已由负责人确认。没有已批准黄金帧。
/// @param request 读取范围，只在本次调用期间借用。firstSlot 是逻辑 slot，slotCount 是 slot 数。
/// @return 成功时调用方拥有 [长度][从站][0x03][起始寄存器][寄存器数量][CRC]。
/// slot 数量非法，或寄存器范围不能放进 16 位地址时，错误码为 InvalidParameter，没有输出缓冲区。
/// 分配失败时抛出 std::bad_alloc。
ernc::domain::Result<std::vector<std::uint8_t>> encodeFc03Read(const Fc03ReadRequest& request);

/// @brief 解析一帧已经收齐的 FC03 Modbus RTU 响应。
///
/// 输入不含外层长度字节。半帧和粘连不在这里处理。先核对 CRC，再识别功能码 0x03 或异常功能码 0x83。
/// 正常响应的数据区原样返回，不按小端解释成数值。
/// @param modbusFrame 含 CRC 的 Modbus RTU 本体，只在本次调用期间借用。
/// @return CRC 和结构都合法时，结果里是 Fc03Payload 或 Fc03Exception。
/// CRC 不符时错误码为 CrcMismatch。字节数和数据区不一致，或 slot 数超过 62 时错误码为 PayloadSizeMismatch。
/// 功能码不是 0x03 或 0x83 时错误码为 InvalidParameter。短到无法拆开 CRC 时沿用 verifyCrc16 的 InvalidParameter。
ernc::domain::Result<Fc03Response> decodeFc03Response(std::span<const std::uint8_t> modbusFrame);

}  // namespace ernc::protocol
