#include "ernc/protocol/crc16.hpp"

#include <cstdint>
#include <span>
#include <vector>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_protocol must not include Qt"
#endif

namespace ernc::protocol {
namespace {

/// 反射 CRC-16/MODBUS 的异或常数。初值 0xFFFF，最终不再异或。
/// 来源是 CHNB-NEW-6MIC @ fbecac9 的 calc_crc16。Needs Evidence：没有已批准黄金帧。
constexpr std::uint16_t kCrcPolynomial = 0xA001;

/// 验证时至少要有 1 个覆盖字节和 2 个 CRC 字节。短于这个长度不是 CRC 比对失败。
constexpr std::size_t kMinimumFrameBytes = 3;

}  // namespace

std::uint16_t calculateCrc16(std::span<const std::uint8_t> covered) noexcept {
    std::uint16_t crc = 0xFFFF;
    for (const std::uint8_t byte : covered) {
        crc = static_cast<std::uint16_t>(crc ^ byte);
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x0001U) != 0U) {
                crc = static_cast<std::uint16_t>((crc >> 1U) ^ kCrcPolynomial);
            } else {
                crc = static_cast<std::uint16_t>(crc >> 1U);
            }
        }
    }
    return crc;
}

std::vector<std::uint8_t> appendCrc16(std::span<const std::uint8_t> covered) {
    const std::uint16_t crc = calculateCrc16(covered);
    std::vector<std::uint8_t> framed;
    framed.reserve(covered.size() + 2);
    framed.insert(framed.end(), covered.begin(), covered.end());
    framed.push_back(static_cast<std::uint8_t>(crc & 0x00FFU));
    framed.push_back(static_cast<std::uint8_t>((crc >> 8U) & 0x00FFU));
    return framed;
}

ernc::domain::Result<void> verifyCrc16(std::span<const std::uint8_t> frameWithCrc) noexcept {
    if (frameWithCrc.size() < kMinimumFrameBytes) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const std::span<const std::uint8_t> covered = frameWithCrc.first(frameWithCrc.size() - 2);
    const auto low = static_cast<std::uint16_t>(frameWithCrc[frameWithCrc.size() - 2]);
    const auto high = static_cast<std::uint16_t>(frameWithCrc[frameWithCrc.size() - 1]);
    const auto received = static_cast<std::uint16_t>(low | (high << 8U));
    if (calculateCrc16(covered) != received) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::CrcMismatch);
    }
    return ernc::domain::Result<void>::success();
}

}  // namespace ernc::protocol
