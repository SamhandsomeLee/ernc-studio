#include "ernc/protocol/wire_encoder.hpp"

#include <cstdint>
#include <span>
#include <vector>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_protocol must not include Qt"
#endif

namespace ernc::protocol {
namespace {

/// 1 字节长度字段能表示的最大后续字节数。
/// 来源是 CHNB-NEW-6MIC @ fbecac9 的 MAX_MODBUS_PAYLOAD。Needs Evidence：没有已批准黄金帧。
constexpr std::size_t kMaxModbusPayload = 255;

}  // namespace

ernc::domain::Result<std::vector<std::uint8_t>> encodeLengthPrefixed(
    std::span<const std::uint8_t> modbusFrame) {
    if (modbusFrame.size() > kMaxModbusPayload) {
        return ernc::domain::Result<std::vector<std::uint8_t>>::failure(
            ernc::domain::ErrorCode::FrameLengthInvalid);
    }
    std::vector<std::uint8_t> wire;
    wire.reserve(modbusFrame.size() + 1);
    wire.push_back(static_cast<std::uint8_t>(modbusFrame.size()));
    wire.insert(wire.end(), modbusFrame.begin(), modbusFrame.end());
    return ernc::domain::Result<std::vector<std::uint8_t>>::success(std::move(wire));
}

}  // namespace ernc::protocol
