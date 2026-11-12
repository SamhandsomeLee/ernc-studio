#include "ernc/application/simulator_can_msg_pulse.hpp"

#include "ernc/domain/types.hpp"
#include "ernc/protocol/typed_codec.hpp"
#include "slave_image.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace ernc::application {
namespace {

using ernc::domain::FieldLayout;
using ernc::domain::FirmwareLayout;
using ernc::domain::LayoutResolve;
using ernc::domain::ScalarValue;

const std::array<std::uint8_t, 4> encodedFloatLevel(bool high) {
    const ScalarValue value = ScalarValue::fromFloat(high ? 1.0F : 0.0F);
    return ernc::protocol::encodeScalar(value);
}

}  // namespace

bool writeCanMsgMirrorLevel(ernc::simulator::SlaveImage& image, const FirmwareLayout& layout, bool high) {
    const LayoutResolve resolved = layout.locate("Can_Msg");
    if (!resolved.ok() || resolved.tryField() == nullptr) {
        return false;
    }
    const FieldLayout& field = resolved.field();
    if (!field.scalarType || field.slotCount == 0U || !image.contains(static_cast<std::uint16_t>(field.slot),
                                                                    static_cast<std::uint16_t>(field.slotCount))) {
        return false;
    }
    const std::array<std::uint8_t, 4> encoded = encodedFloatLevel(high);
    std::vector<std::uint8_t> chunk(static_cast<std::size_t>(field.slotCount) * 4U);
    for (std::uint32_t index = 0U; index < field.slotCount; ++index) {
        std::memcpy(chunk.data() + static_cast<std::size_t>(index) * 4U, encoded.data(), 4U);
    }
    return image.write(static_cast<std::uint16_t>(field.slot), chunk);
}

}  // namespace ernc::application
