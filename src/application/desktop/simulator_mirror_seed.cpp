#include "ernc/application/simulator_mirror_seed.hpp"

#include "ernc/application/simulator_device_stack.hpp"
#include "ernc/domain/types.hpp"
#include "ernc/infrastructure/legacy_param_json.hpp"
#include "ernc/protocol/typed_codec.hpp"

#include <algorithm>
#include <cstring>
#include <optional>

namespace ernc::application {
namespace {

using ernc::domain::DataType;
using ernc::domain::FieldLayout;
using ernc::domain::FirmwareLayout;
using ernc::domain::LayoutResolve;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterDefinition;
using ernc::domain::ScalarValue;
using ernc::domain::ValueShape;

bool isScalarDefinition(const ParameterDefinition& definition) {
    return !definition.shape || *definition.shape == ValueShape::Scalar;
}

bool legacyEntryIsZero(const ernc::infrastructure::LegacyParamJsonEntry& entry) {
    if (const auto floating = entry.value.asFloat(); floating.ok()) {
        return floating.value() == 0.0F;
    }
    if (const auto integer = entry.value.asInt32(); integer.ok()) {
        return integer.value() == 0;
    }
    if (const auto unsignedValue = entry.value.asUInt32(); unsignedValue.ok()) {
        return unsignedValue.value() == 0U;
    }
    return false;
}

std::optional<ScalarValue> zeroForType(DataType type) {
    switch (type) {
    case DataType::Float32:
        return ScalarValue::fromFloat(0.0F);
    case DataType::Int32:
        return ScalarValue::fromInt32(0);
    case DataType::UInt32:
        return ScalarValue::fromUInt32(0U);
    }
    return std::nullopt;
}

std::optional<ScalarValue> resolveZero(const ParameterDefinition& definition, const FieldLayout& field) {
    if (field.scalarType) {
        return zeroForType(*field.scalarType);
    }
    if (definition.dataType) {
        return zeroForType(*definition.dataType);
    }
    return std::nullopt;
}

/// @brief 把一个标量编码写入 field 覆盖的全部 slot。
/// @param bytes 镜像缓冲，只借用。
/// @param field 已解析布局字段，slot 单位为 4 字节。
/// @param value 要广播的标量原值。
/// @return 实际写入的 slot 数。
std::size_t writeScalarAcrossField(std::span<std::uint8_t> bytes, const FieldLayout& field,
                                   const ScalarValue& value) {
    if (field.slotCount == 0U || !field.scalarType) {
        return 0U;
    }
    const std::array<std::uint8_t, 4> encoded = ernc::protocol::encodeScalar(value);
    std::size_t written = 0U;
    for (std::uint32_t index = 0U; index < field.slotCount; ++index) {
        const std::size_t offset = (static_cast<std::size_t>(field.slot) + index) * 4U;
        if (offset + 4U > bytes.size()) {
            break;
        }
        std::memcpy(bytes.data() + offset, encoded.data(), 4U);
        ++written;
    }
    return written;
}

SimulatorMirrorSeedReport seedFromCatalogValue(const ParameterCatalog& catalog, const FirmwareLayout& layout,
                                               std::span<std::uint8_t> bytes,
                                               const auto& valueForDefinition) {
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
        const std::optional<ScalarValue> value = valueForDefinition(definition, field);
        if (!value) {
            ++report.skippedEntries;
            continue;
        }
        report.slotsWritten += writeScalarAcrossField(bytes, field, *value);
    }
    return report;
}

}  // namespace

SimulatorMirrorSeedReport seedSimulatorMirrorDefaults(const ParameterCatalog& catalog, const FirmwareLayout& layout,
                                                        std::span<std::uint8_t> bytes) {
    return seedFromCatalogValue(
        catalog, layout, bytes, [](const ParameterDefinition& definition, const FieldLayout& field) {
            return resolveZero(definition, field);
        });
}

SimulatorMirrorSeedReport seedSimulatorMirrorFromLegacy(
    const ParameterCatalog& catalog, const FirmwareLayout& layout, std::span<std::uint8_t> bytes,
    const ernc::infrastructure::LegacyParamJsonDocument& document) {
    if (document.version != ernc::infrastructure::kLegacyParamJsonVersion) {
        return {};
    }
    SimulatorMirrorSeedReport report;
    for (const ernc::infrastructure::LegacyParamJsonEntry& entry : document.params) {
        const ParameterDefinition* definition = catalog.find(entry.name);
        if (definition == nullptr || !definition->firmwarePath || definition->firmwarePath->empty()) {
            ++report.skippedEntries;
            continue;
        }
        const LayoutResolve resolved = layout.locate(*definition->firmwarePath);
        if (!resolved.ok() || resolved.tryField() == nullptr) {
            ++report.skippedEntries;
            continue;
        }
        const FieldLayout& field = resolved.field();
        if (!field.scalarType || field.slotCount == 0U) {
            ++report.skippedEntries;
            continue;
        }
        if (field.slotCount > 1U && legacyEntryIsZero(entry)) {
            ++report.skippedEntries;
            continue;
        }
        if (!isScalarDefinition(*definition) && field.slotCount <= 1U) {
            ++report.skippedEntries;
            continue;
        }
        std::optional<DataType> type = definition->dataType;
        if (!type) {
            type = field.scalarType;
        }
        ernc::domain::Result<ScalarValue> adapted = ernc::domain::Result<ScalarValue>::failure(
            ernc::domain::ErrorCode::InvalidParameter);
        if (type) {
            double number = 0.0;
            if (const auto floating = entry.value.asFloat(); floating.ok()) {
                number = floating.value();
            } else if (const auto integer = entry.value.asInt32(); integer.ok()) {
                number = static_cast<double>(integer.value());
            } else if (const auto unsignedValue = entry.value.asUInt32(); unsignedValue.ok()) {
                number = static_cast<double>(unsignedValue.value());
            }
            switch (*type) {
            case DataType::Float32:
                adapted = ernc::domain::Result<ScalarValue>::success(ScalarValue::fromFloat(static_cast<float>(number)));
                break;
            case DataType::Int32:
                adapted = ernc::domain::Result<ScalarValue>::success(
                    ScalarValue::fromInt32(static_cast<std::int32_t>(number)));
                break;
            case DataType::UInt32:
                adapted = ernc::domain::Result<ScalarValue>::success(
                    ScalarValue::fromUInt32(static_cast<std::uint32_t>(number)));
                break;
            }
        }
        if (!adapted.ok() || adapted.tryValue() == nullptr) {
            ++report.skippedEntries;
            continue;
        }
        report.slotsWritten += writeScalarAcrossField(bytes, field, adapted.value());
    }
    return report;
}

std::vector<std::uint8_t> makeDemoSimulatorImage(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    const std::uint32_t slots = maxLogicalSlotForCatalog(catalog, layout);
    std::vector<std::uint8_t> mirror = makeZeroSimulatorImage(slots);
    if (!mirror.empty()) {
        seedSimulatorMirrorDemoProfile(catalog, layout, mirror);
    }
    return mirror;
}

std::vector<std::uint8_t> makeDefaultSimulatorImage(const ParameterCatalog& catalog,
                                                    const FirmwareLayout& layout) {
    const std::uint32_t slots = maxLogicalSlotForCatalog(catalog, layout);
    std::vector<std::uint8_t> mirror = makeZeroSimulatorImage(slots);
    if (!mirror.empty()) {
        seedSimulatorMirrorDefaults(catalog, layout, mirror);
    }
    return mirror;
}

}  // namespace ernc::application
