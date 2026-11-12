#include "ernc/application/simulator_mirror_seed.hpp"

#include "ernc/domain/array_parameter.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/protocol/typed_codec.hpp"

#include <cmath>
#include <cstring>
#include <string_view>

namespace ernc::application {
namespace {

using ernc::domain::AccessMode;
using ernc::domain::DataType;
using ernc::domain::EditorKind;
using ernc::domain::FieldLayout;
using ernc::domain::LayoutResolve;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterDefinition;
using ernc::domain::ScalarValue;
using ernc::domain::ValueShape;

/// 6MIC 剖面通道数；与 firmware/proj_config.json 宏一致，非运行时从 JSON 加载。
constexpr std::int32_t kReferenceNum = 12;
constexpr std::int32_t kErrorNum = 6;
constexpr std::int32_t kSpeakerNum = 6;

bool startsWith(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

std::optional<ScalarValue> scalarFromDouble(DataType type, double number) {
    switch (type) {
    case DataType::Float32:
        return ScalarValue::fromFloat(static_cast<float>(number));
    case DataType::Int32:
        return ScalarValue::fromInt32(static_cast<std::int32_t>(std::lround(number)));
    case DataType::UInt32:
        if (number < 0) {
            return std::nullopt;
        }
        return ScalarValue::fromUInt32(static_cast<std::uint32_t>(std::lround(number)));
    }
    return std::nullopt;
}

bool decodedIsZero(std::span<const std::uint8_t> fourBytes, DataType type) {
    const ernc::domain::Result<ScalarValue> decoded =
        ernc::protocol::decodeScalar(fourBytes, type == DataType::Int32   ? ScalarValue::Kind::Int32
                                                    : type == DataType::UInt32 ? ScalarValue::Kind::UInt32
                                                                               : ScalarValue::Kind::Float32);
    if (!decoded.ok()) {
        return true;
    }
    if (const auto floating = decoded.value().asFloat(); floating.ok()) {
        return floating.value() == 0.0F;
    }
    if (const auto integer = decoded.value().asInt32(); integer.ok()) {
        return integer.value() == 0;
    }
    if (const auto unsignedValue = decoded.value().asUInt32(); unsignedValue.ok()) {
        return unsignedValue.value() == 0U;
    }
    return true;
}

/// 多槽/目录 array 的演示值；下标从 0 起，保证不为 0。
std::optional<ScalarValue> arraySlotDemo(std::string_view id, std::uint32_t slotIndex, DataType type,
                                         std::uint32_t slotCount) {
    (void)slotCount;
    const double base = 0.12 + static_cast<double>(slotIndex + 1U) * 0.08;

    if (id == "SpkOrderAmpThre") {
        return scalarFromDouble(type, 0.15 + static_cast<double>((slotIndex % 3U) + 1U) * 0.1);
    }
    if (id == "SpkState" || id == "MicState") {
        return scalarFromDouble(type, (slotIndex < static_cast<std::uint32_t>(kSpeakerNum) ? 1.0 : 0.6)
                                         + static_cast<double>(slotIndex) * 0.05);
    }
    if (id == "RNC_param.LeakyRNCFre") {
        return scalarFromDouble(type, 80.0 + static_cast<double>(slotIndex) * 12.0);
    }
    if (id == "RNC_param.LeakyRNC") {
        return scalarFromDouble(type, 0.02 + static_cast<double>(slotIndex + 1U) * 0.005);
    }
    if (id == "ErrPowRNC") {
        return scalarFromDouble(type, 0.001 + static_cast<double>((slotIndex % 40U) + 1U) * 0.002);
    }
    if (id == "Input_threshold" || id == "ENC_Mic_Gain" || id == "SPEAKER_Gain" || id == "ENC_SPEAKER_Gain") {
        return scalarFromDouble(type, 0.08 + static_cast<double>((slotIndex % static_cast<std::uint32_t>(kReferenceNum))
                                                                  + 1U)
                                         * 0.04);
    }
    if (id == "ENC_param.SpeakerFre") {
        const std::uint32_t col = slotIndex % 4U;
        const std::uint32_t row = slotIndex / 4U;
        return scalarFromDouble(type, 120.0 + static_cast<double>(row) * 80.0 + static_cast<double>(col) * 15.0);
    }
    if (id == "ENC_param.SpeakerFreGain") {
        return scalarFromDouble(type, 0.5 + static_cast<double>((slotIndex % 4U) + 1U) * 0.15);
    }
    if (startsWith(id, "ENC_param.") && (id.find("Amp") != std::string_view::npos || id.find("Step") != std::string_view::npos
                                          || id.find("Leaky") != std::string_view::npos || id.find("Tacho") != std::string_view::npos)) {
        return scalarFromDouble(type, 0.12 + static_cast<double>((slotIndex % 10U) + 1U) * 0.07);
    }
    if (id == "Can_Msg") {
        return scalarFromDouble(type, 1.0);
    }
    if (ernc::domain::isRegionLikeFirmwarePath(id)) {
        return scalarFromDouble(type, 0.02 + static_cast<double>((slotIndex % 60U) + 1U) * 0.003);
    }
    return scalarFromDouble(type, base);
}

std::size_t writeSlot(std::span<std::uint8_t> bytes, std::uint32_t slot, const ScalarValue& value) {
    const std::size_t offset = static_cast<std::size_t>(slot) * 4U;
    if (offset + 4U > bytes.size()) {
        return 0U;
    }
    const std::array<std::uint8_t, 4> encoded = ernc::protocol::encodeScalar(value);
    std::memcpy(bytes.data() + offset, encoded.data(), 4U);
    return 1U;
}

std::optional<ScalarValue> demoSlotValue(const ParameterDefinition& definition, const FieldLayout& field,
                                         std::uint32_t slotIndex) {
    if (!field.scalarType) {
        return std::nullopt;
    }
    const DataType type = *field.scalarType;
    const std::string_view id = definition.id.text();

    if (definition.wireLevels.has_value()) {
        return scalarFromDouble(type, definition.wireLevels->on);
    }

    if (definition.editor == EditorKind::MomentaryCommand) {
        return scalarFromDouble(type, 0.0);
    }

    if (id == "RNC_param.refernum") {
        return scalarFromDouble(type, static_cast<double>(kReferenceNum));
    }
    if (id == "RNC_param.errnum") {
        return scalarFromDouble(type, static_cast<double>(kErrorNum));
    }
    if (id == "RNC_param.spknum") {
        return scalarFromDouble(type, static_cast<double>(kSpeakerNum));
    }
    if (id == "start_fre_num") {
        return scalarFromDouble(type, 7.0);
    }
    if (id == "first_fre_num" || id == "second_fre_num" || id == "third_fre_num" || id == "forth_fre_num") {
        return scalarFromDouble(type, 100.0);
    }
    if (id == "Step") {
        return scalarFromDouble(type, 0.002);
    }
    if (id == "running_time") {
        return scalarFromDouble(type, 120.0);
    }
    if (id == "max_time") {
        return scalarFromDouble(type, 3600.0);
    }
    if (id == "init_param_flag") {
        return scalarFromDouble(type, 1.0);
    }
    if (id == "ENCEchoGainThreshold" || id == "HornRefThreshold" || id == "MiuThreshold" || id == "Output_Threshold") {
        return scalarFromDouble(type, 0.05);
    }
    if (field.slotCount > 1U || (definition.shape && *definition.shape == ValueShape::Array)) {
        if (const std::optional<ScalarValue> arrayValue = arraySlotDemo(id, slotIndex, type, field.slotCount)) {
            return arrayValue;
        }
    }

    if (startsWith(id, "RNC_param.alpha")) {
        return scalarFromDouble(type, 0.05);
    }
    if (id == "RNC_param.eta" || id == "RNC_param.delta") {
        return scalarFromDouble(type, 0.001);
    }
    if (startsWith(id, "ENC_param.")) {
        if (id.find("Fre") != std::string_view::npos) {
            return scalarFromDouble(type, 50.0 + static_cast<double>(slotIndex) * 5.0);
        }
        if (id.find("Speed") != std::string_view::npos || id.find("Shaft") != std::string_view::npos) {
            return scalarFromDouble(type, 800.0 + static_cast<double>(slotIndex) * 50.0);
        }
        if (id.find("Amp") != std::string_view::npos || id.find("Step") != std::string_view::npos
            || id.find("Leaky") != std::string_view::npos || id.find("Delta") != std::string_view::npos
            || id.find("ErrWei") != std::string_view::npos || id.find("Tacho") != std::string_view::npos) {
            return scalarFromDouble(type, 0.1 + static_cast<double>((slotIndex % 5U) + 1U) * 0.05);
        }
        if (id.find("Order") != std::string_view::npos) {
            return scalarFromDouble(type, static_cast<double>((slotIndex % 3U) + 1U));
        }
    }

    if (startsWith(id, "enc_order.")) {
        return scalarFromDouble(type, static_cast<double>((slotIndex % 3U) + 1U));
    }

    if (id.find("Gain") != std::string_view::npos || id == "ACC_Gain") {
        return scalarFromDouble(type, 1.0);
    }
    if (id == "HornPower" || id == "horn_control") {
        return scalarFromDouble(type, 1.0);
    }
    if (id == "HornTime" || id == "HornOFFContinueTime") {
        return scalarFromDouble(type, 3.0);
    }
    if (startsWith(id, "K_") || startsWith(id, "B_")) {
        return scalarFromDouble(type, 1.0);
    }

    if (definition.access == AccessMode::ReadOnly) {
        return scalarFromDouble(type, type == DataType::Float32 ? 0.01 : 1.0);
    }

    switch (type) {
    case DataType::Float32:
        return ScalarValue::fromFloat(1.0F);
    case DataType::Int32:
        return ScalarValue::fromInt32(1);
    case DataType::UInt32:
        return ScalarValue::fromUInt32(1U);
    }
    return std::nullopt;
}

void patchZeroMultiSlotFields(const ernc::domain::ParameterCatalog& catalog,
                              const ernc::domain::FirmwareLayout& layout, std::span<std::uint8_t> bytes) {
    for (const ParameterDefinition& definition : catalog.entries()) {
        if (!definition.firmwarePath || definition.firmwarePath->empty()) {
            continue;
        }
        const LayoutResolve resolved = layout.locate(*definition.firmwarePath);
        if (!resolved.ok() || resolved.tryField() == nullptr) {
            continue;
        }
        const FieldLayout& field = resolved.field();
        if (!field.scalarType || field.slotCount <= 1U) {
            continue;
        }
        for (std::uint32_t index = 0U; index < field.slotCount; ++index) {
            const std::uint32_t slot = field.slot + index;
            const std::size_t offset = static_cast<std::size_t>(slot) * 4U;
            if (offset + 4U > bytes.size()) {
                break;
            }
            if (!decodedIsZero({bytes.data() + offset, 4U}, *field.scalarType)) {
                continue;
            }
            const std::optional<ScalarValue> replacement =
                arraySlotDemo(definition.id.text(), index, *field.scalarType, field.slotCount);
            if (replacement) {
                writeSlot(bytes, slot, *replacement);
            }
        }
    }
}

}  // namespace

void patchSimulatorMirrorArrayZeros(const ernc::domain::ParameterCatalog& catalog,
                                    const ernc::domain::FirmwareLayout& layout,
                                    std::span<std::uint8_t> bytes) {
    patchZeroMultiSlotFields(catalog, layout, bytes);
}

SimulatorMirrorSeedReport seedSimulatorMirrorDemoProfile(const ernc::domain::ParameterCatalog& catalog,
                                                         const ernc::domain::FirmwareLayout& layout,
                                                         std::span<std::uint8_t> bytes) {
    SimulatorMirrorSeedReport report;
    for (const ParameterDefinition& definition : catalog.entries()) {
        if (!definition.firmwarePath || definition.firmwarePath->empty()) {
            continue;
        }
        const LayoutResolve resolved = layout.locate(*definition.firmwarePath);
        if (!resolved.ok() || resolved.tryField() == nullptr) {
            ++report.skippedEntries;
            continue;
        }
        const FieldLayout& field = resolved.field();
        if (!field.scalarType || field.slotCount == 0U || field.size != field.slotCount * 4U) {
            ++report.skippedEntries;
            continue;
        }
        for (std::uint32_t index = 0U; index < field.slotCount; ++index) {
            const std::optional<ScalarValue> value = demoSlotValue(definition, field, index);
            if (!value) {
                ++report.skippedEntries;
                break;
            }
            report.slotsWritten += writeSlot(bytes, field.slot + index, *value);
        }
    }
    patchZeroMultiSlotFields(catalog, layout, bytes);
    return report;
}

}  // namespace ernc::application
