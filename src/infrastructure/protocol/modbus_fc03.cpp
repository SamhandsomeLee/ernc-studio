#include "ernc/protocol/modbus_fc03.hpp"

#include "ernc/protocol/crc16.hpp"
#include "ernc/protocol/wire_encoder.hpp"

#include <cstdint>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_protocol must not include Qt"
#endif

namespace ernc::protocol {
namespace {

/// FC03 读保持寄存器。旧版 ModbusFunction.READ_HOLDING_REGISTERS。
constexpr std::uint8_t kReadFunction = 0x03;

/// FC03 异常响应的功能码，等于 kReadFunction 的最高位置 1。
constexpr std::uint8_t kReadExceptionFunction = 0x83;

/// 一个逻辑 slot 在 FC03 数据区占据的字节数。寄存器换算仍是每个 slot 两个 16 位寄存器。
constexpr std::uint16_t kBytesPerSlot = 4;

/// @brief 判断起始寄存器和寄存器数量能否放进帧里的两个大端 16 位字段。
/// @param firstSlot 逻辑 slot。寄存器起点是它乘 2。
/// @param slotCount 已确认落在 1 到 62 之间的 slot 数。
/// @return 起点和最后一个寄存器都不超过 65535 时为 true。
bool registersFit(std::uint16_t firstSlot, std::uint16_t slotCount) noexcept {
    const std::uint32_t start = static_cast<std::uint32_t>(firstSlot) * 2U;
    const std::uint32_t registerCount = static_cast<std::uint32_t>(slotCount) * 2U;
    return start <= 0xFFFFU && start + registerCount - 1U <= 0xFFFFU;
}

}  // namespace

ernc::domain::Result<std::vector<std::uint8_t>> encodeFc03Read(const Fc03ReadRequest& request) {
    if (request.slotCount == 0 || request.slotCount > kMaxFc03ReadSlots || !registersFit(request.firstSlot, request.slotCount)) {
        return ernc::domain::Result<std::vector<std::uint8_t>>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const auto start = static_cast<std::uint16_t>(static_cast<std::uint32_t>(request.firstSlot) * 2U);
    const auto registerCount = static_cast<std::uint16_t>(static_cast<std::uint32_t>(request.slotCount) * 2U);
    const std::uint8_t header[] = {
        request.slaveAddress,
        kReadFunction,
        static_cast<std::uint8_t>((start >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(start & 0xFFU),
        static_cast<std::uint8_t>((registerCount >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(registerCount & 0xFFU),
    };
    const std::vector<std::uint8_t> modbus = appendCrc16(header);
    return encodeLengthPrefixed(modbus);
}

ernc::domain::Result<Fc03Response> decodeFc03Response(std::span<const std::uint8_t> modbusFrame) {
    const ernc::domain::Result<void> crc = verifyCrc16(modbusFrame);
    if (!crc.ok()) {
        return ernc::domain::Result<Fc03Response>::failure(crc.error());
    }
    const std::span<const std::uint8_t> pdu = modbusFrame.first(modbusFrame.size() - 2);
    if (pdu.size() < 2) {
        return ernc::domain::Result<Fc03Response>::failure(ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    const std::uint8_t slave = pdu[0];
    const std::uint8_t function = pdu[1];
    if (function == kReadExceptionFunction) {
        if (pdu.size() != 3) {
            return ernc::domain::Result<Fc03Response>::failure(ernc::domain::ErrorCode::PayloadSizeMismatch);
        }
        return ernc::domain::Result<Fc03Response>::success(Fc03Response{Fc03Exception{slave, pdu[2]}});
    }
    if (function != kReadFunction) {
        return ernc::domain::Result<Fc03Response>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (pdu.size() < 3) {
        return ernc::domain::Result<Fc03Response>::failure(ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    const std::size_t byteCount = pdu[2];
    const std::span<const std::uint8_t> data = pdu.subspan(3);
    if (data.size() != byteCount || byteCount < kBytesPerSlot || byteCount % kBytesPerSlot != 0) {
        return ernc::domain::Result<Fc03Response>::failure(ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    const std::size_t slots = byteCount / kBytesPerSlot;
    if (slots > kMaxFc03ReadSlots) {
        return ernc::domain::Result<Fc03Response>::failure(ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    Fc03Payload payload;
    payload.slaveAddress = slave;
    payload.bytes.assign(data.begin(), data.end());
    payload.slotCount = static_cast<std::uint16_t>(slots);
    return ernc::domain::Result<Fc03Response>::success(Fc03Response{std::move(payload)});
}

}  // namespace ernc::protocol
