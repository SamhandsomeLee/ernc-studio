#include "ernc/protocol/modbus_fc16.hpp"

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

/// FC16 写多个寄存器。旧版 ModbusFunction.WRITE_MULTIPLE_REGISTERS。
constexpr std::uint8_t kWriteFunction = 0x10;

/// FC16 异常响应的功能码，等于 kWriteFunction 的最高位置 1。
constexpr std::uint8_t kWriteExceptionFunction = 0x90;

/// 一个逻辑 slot 在 FC16 数据区占据的字节数，也是两个 16 位寄存器。
constexpr std::uint16_t kBytesPerSlot = 4;

/// 正常 ACK 去掉 CRC 后的字节数：从站、功能码、起始寄存器、寄存器数量。
constexpr std::size_t kAckPduBytes = 6;

/// @brief 判断起始寄存器和寄存器数量能否放进两个大端 16 位字段。
/// @param firstSlot 逻辑 slot。寄存器起点是它乘 2。
/// @param slotCount 已确认落在 1 到 61 之间的 slot 数。
/// @return 起点和最后一个寄存器都不超过 65535 时为 true。
bool registersFit(std::uint16_t firstSlot, std::uint16_t slotCount) noexcept {
    const std::uint32_t start = static_cast<std::uint32_t>(firstSlot) * 2U;
    const std::uint32_t registerCount = static_cast<std::uint32_t>(slotCount) * 2U;
    return start <= 0xFFFFU && start + registerCount - 1U <= 0xFFFFU;
}

/// @brief 请求范围能否用来组帧或核对 ACK。
/// @param request 写入范围，只在本次调用期间借用。
/// @return 数量为 1 到 61 且寄存器范围放得下时为 true。
bool requestRangeOk(const Fc16WriteRequest& request) noexcept {
    return request.slotCount != 0 && request.slotCount <= kMaxFc16WriteSlots && registersFit(request.firstSlot, request.slotCount);
}

}  // namespace

ernc::domain::Result<std::vector<std::uint8_t>> encodeFc16Write(const Fc16WriteRequest& request,
                                                                std::span<const std::uint8_t> payload) {
    if (!requestRangeOk(request)) {
        return ernc::domain::Result<std::vector<std::uint8_t>>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const std::size_t byteCount = static_cast<std::size_t>(request.slotCount) * kBytesPerSlot;
    if (payload.size() != byteCount) {
        return ernc::domain::Result<std::vector<std::uint8_t>>::failure(ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    const auto start = static_cast<std::uint16_t>(static_cast<std::uint32_t>(request.firstSlot) * 2U);
    const auto registerCount = static_cast<std::uint16_t>(static_cast<std::uint32_t>(request.slotCount) * 2U);
    std::vector<std::uint8_t> body;
    body.reserve(7 + payload.size());
    body.push_back(request.slaveAddress);
    body.push_back(kWriteFunction);
    body.push_back(static_cast<std::uint8_t>((start >> 8U) & 0xFFU));
    body.push_back(static_cast<std::uint8_t>(start & 0xFFU));
    body.push_back(static_cast<std::uint8_t>((registerCount >> 8U) & 0xFFU));
    body.push_back(static_cast<std::uint8_t>(registerCount & 0xFFU));
    body.push_back(static_cast<std::uint8_t>(byteCount));
    body.insert(body.end(), payload.begin(), payload.end());
    return encodeLengthPrefixed(appendCrc16(body));
}

ernc::domain::Result<Fc16Response> decodeFc16Ack(const Fc16WriteRequest& request, std::span<const std::uint8_t> modbusFrame) {
    if (!requestRangeOk(request)) {
        return ernc::domain::Result<Fc16Response>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<void> crc = verifyCrc16(modbusFrame);
    if (!crc.ok()) {
        return ernc::domain::Result<Fc16Response>::failure(crc.error());
    }
    const std::span<const std::uint8_t> pdu = modbusFrame.first(modbusFrame.size() - 2);
    if (pdu.size() < 2) {
        return ernc::domain::Result<Fc16Response>::failure(ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    const std::uint8_t slave = pdu[0];
    const std::uint8_t function = pdu[1];
    if (function == kWriteExceptionFunction) {
        if (pdu.size() != 3) {
            return ernc::domain::Result<Fc16Response>::failure(ernc::domain::ErrorCode::PayloadSizeMismatch);
        }
        return ernc::domain::Result<Fc16Response>::success(Fc16Response{Fc16Exception{slave, pdu[2]}});
    }
    if (function != kWriteFunction) {
        return ernc::domain::Result<Fc16Response>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (pdu.size() != kAckPduBytes) {
        return ernc::domain::Result<Fc16Response>::failure(ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    const auto start = static_cast<std::uint16_t>((static_cast<std::uint16_t>(pdu[2]) << 8U) | pdu[3]);
    const auto registerCount = static_cast<std::uint16_t>((static_cast<std::uint16_t>(pdu[4]) << 8U) | pdu[5]);
    const auto expectedStart = static_cast<std::uint16_t>(static_cast<std::uint32_t>(request.firstSlot) * 2U);
    const auto expectedCount = static_cast<std::uint16_t>(static_cast<std::uint32_t>(request.slotCount) * 2U);
    if (slave != request.slaveAddress || start != expectedStart || registerCount != expectedCount) {
        return ernc::domain::Result<Fc16Response>::failure(ernc::domain::ErrorCode::ResponseMismatch);
    }
    return ernc::domain::Result<Fc16Response>::success(Fc16Response{Fc16Ack{slave, start, registerCount}});
}

}  // namespace ernc::protocol
