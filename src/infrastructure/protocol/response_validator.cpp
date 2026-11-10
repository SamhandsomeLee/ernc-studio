#include "ernc/protocol/response_validator.hpp"

#include "ernc/protocol/crc16.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_protocol must not include Qt"
#endif

namespace ernc::protocol {
namespace {

/// FC03 正常功能码。
constexpr std::uint8_t kReadFunction = 0x03;

/// FC03 异常功能码。
constexpr std::uint8_t kReadExceptionFunction = 0x83;

/// FC16 正常功能码。
constexpr std::uint8_t kWriteFunction = 0x10;

/// FC16 异常功能码。
constexpr std::uint8_t kWriteExceptionFunction = 0x90;

/// 旧版 decode_modbus_rtu 在短于 5 字节时不把帧当成可识别响应。
constexpr std::uint8_t kMinimumInnerBytes = 5;

/// 正常 ACK 去掉 CRC 后的字节数：从站、功能码、起始寄存器、寄存器数量。
constexpr std::size_t kAckPduBytes = 6;

/// 异常帧去掉 CRC 后的字节数：从站、异常功能码、异常码。
constexpr std::size_t kExceptionPduBytes = 3;

/// @brief 命令范围能否作为一次合法期望。
/// @param command 当前期望，只在本次调用期间借用。
/// @return 数量在该命令的上限内，且寄存器范围放得进 16 位地址时为 true。
bool expectationOk(const CommandExpectation& command) noexcept {
    if (command.slotCount == 0) {
        return false;
    }
    if (command.kind == CommandKind::ReadHolding && command.slotCount > kMaxFc03ReadSlots) {
        return false;
    }
    if (command.kind == CommandKind::WriteMultiple && command.slotCount > kMaxFc16WriteSlots) {
        return false;
    }
    if (command.kind != CommandKind::ReadHolding && command.kind != CommandKind::WriteMultiple) {
        return false;
    }
    const std::uint32_t start = static_cast<std::uint32_t>(command.firstSlot) * 2U;
    const std::uint32_t registerCount = static_cast<std::uint32_t>(command.slotCount) * 2U;
    return start <= 0xFFFFU && start + registerCount - 1U <= 0xFFFFU;
}

/// @brief 取出命令对应的正常功能码和异常功能码。
/// @param kind 读或写。
/// @param normal 输出正常功能码。
/// @param exception 输出异常功能码。
void functionsFor(CommandKind kind, std::uint8_t& normal, std::uint8_t& exception) noexcept {
    if (kind == CommandKind::WriteMultiple) {
        normal = kWriteFunction;
        exception = kWriteExceptionFunction;
        return;
    }
    normal = kReadFunction;
    exception = kReadExceptionFunction;
}

}  // namespace

ResponseVerdict ResponseVerdict::accept(AcceptedResponse response) {
    ResponseVerdict verdict;
    verdict.accepted_ = true;
    verdict.response_ = std::move(response);
    return verdict;
}

ResponseVerdict ResponseVerdict::reject(ernc::domain::ErrorCode code) {
    ResponseVerdict verdict;
    verdict.error_ = code;
    return verdict;
}

ResponseVerdict ResponseVerdict::rejectException(std::uint8_t code) {
    ResponseVerdict verdict;
    verdict.error_ = ernc::domain::ErrorCode::ModbusException;
    verdict.exceptionCode_ = code;
    return verdict;
}

bool ResponseVerdict::accepted() const noexcept {
    return accepted_;
}

const AcceptedResponse* ResponseVerdict::response() const noexcept {
    return response_ ? &*response_ : nullptr;
}

const ernc::domain::ErrorCode* ResponseVerdict::error() const noexcept {
    return accepted_ ? nullptr : &error_;
}

const std::uint8_t* ResponseVerdict::exceptionCode() const noexcept {
    return exceptionCode_ ? &*exceptionCode_ : nullptr;
}

ResponseVerdict validateResponse(const CommandExpectation& command, std::span<const std::uint8_t> wire) {
    if (!expectationOk(command)) {
        return ResponseVerdict::reject(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (wire.empty()) {
        return ResponseVerdict::reject(ernc::domain::ErrorCode::FrameLengthInvalid);
    }
    const std::size_t declared = wire[0];
    if (declared < kMinimumInnerBytes || wire.size() != declared + 1) {
        return ResponseVerdict::reject(ernc::domain::ErrorCode::FrameLengthInvalid);
    }
    const std::span<const std::uint8_t> inner = wire.subspan(1);
    const ernc::domain::Result<void> crc = verifyCrc16(inner);
    if (!crc.ok()) {
        return ResponseVerdict::reject(crc.error());
    }
    const std::span<const std::uint8_t> pdu = inner.first(inner.size() - 2);
    if (pdu.size() < 2) {
        return ResponseVerdict::reject(ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    const std::uint8_t slave = pdu[0];
    const std::uint8_t function = pdu[1];
    std::uint8_t normal = 0;
    std::uint8_t exception = 0;
    functionsFor(command.kind, normal, exception);
    if (function == exception) {
        if (pdu.size() != kExceptionPduBytes) {
            return ResponseVerdict::reject(ernc::domain::ErrorCode::PayloadSizeMismatch);
        }
        if (slave != command.slaveAddress) {
            return ResponseVerdict::reject(ernc::domain::ErrorCode::ResponseMismatch);
        }
        return ResponseVerdict::rejectException(pdu[2]);
    }
    if (function != normal || slave != command.slaveAddress) {
        return ResponseVerdict::reject(ernc::domain::ErrorCode::ResponseMismatch);
    }
    if (command.kind == CommandKind::ReadHolding) {
        if (pdu.size() < 3) {
            return ResponseVerdict::reject(ernc::domain::ErrorCode::PayloadSizeMismatch);
        }
        const std::size_t byteCount = pdu[2];
        const std::span<const std::uint8_t> data = pdu.subspan(3);
        const std::size_t expectedBytes = static_cast<std::size_t>(command.slotCount) * 4U;
        if (byteCount != expectedBytes || data.size() != byteCount) {
            return ResponseVerdict::reject(ernc::domain::ErrorCode::PayloadSizeMismatch);
        }
        AcceptedRead accepted;
        accepted.slaveAddress = slave;
        accepted.payload.assign(data.begin(), data.end());
        accepted.slotCount = command.slotCount;
        return ResponseVerdict::accept(AcceptedResponse{std::move(accepted)});
    }
    if (pdu.size() != kAckPduBytes) {
        return ResponseVerdict::reject(ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    const auto start = static_cast<std::uint16_t>((static_cast<std::uint16_t>(pdu[2]) << 8U) | pdu[3]);
    const auto registerCount = static_cast<std::uint16_t>((static_cast<std::uint16_t>(pdu[4]) << 8U) | pdu[5]);
    const auto expectedStart = static_cast<std::uint16_t>(static_cast<std::uint32_t>(command.firstSlot) * 2U);
    const auto expectedCount = static_cast<std::uint16_t>(static_cast<std::uint32_t>(command.slotCount) * 2U);
    if (start != expectedStart || registerCount != expectedCount) {
        return ResponseVerdict::reject(ernc::domain::ErrorCode::ResponseMismatch);
    }
    return ResponseVerdict::accept(AcceptedResponse{AcceptedWriteAck{slave, start, registerCount}});
}

}  // namespace ernc::protocol
