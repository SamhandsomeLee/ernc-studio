#include "ernc/application/project/legacy_param_json_bridge.hpp"

#include "ernc/application/stage_value.hpp"

#include <cmath>
#include <limits>

namespace ernc::application {
namespace {

using ernc::domain::AccessMode;
using ernc::domain::DataType;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayout;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterDefinition;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::domain::ValueShape;
using ernc::infrastructure::LegacyParamJsonDocument;
using ernc::infrastructure::LegacyParamJsonEntry;
using ernc::infrastructure::LegacyParamJsonReport;
using ernc::infrastructure::kLegacyParamJsonVersion;

/// @brief 允许显式标量或缺省形状，后者还须由布局校验。
/// @param definition 本次调用借用的目录定义。
bool isScalarDefinition(const ParameterDefinition& definition) {
    return !definition.shape || *definition.shape == ValueShape::Scalar;
}

/// @brief 与旧版 export_params_to_json 一致：无 Set/Read 时按目录类型写 0。
/// @param definition 本次调用借用的目录定义。
std::optional<ScalarValue> defaultScalarZero(const ParameterDefinition& definition) {
    if (!definition.dataType) {
        return ScalarValue::fromFloat(0.0F);
    }
    switch (*definition.dataType) {
    case DataType::Float32:
        return ScalarValue::fromFloat(0.0F);
    case DataType::Int32:
        return ScalarValue::fromInt32(0);
    case DataType::UInt32:
        return ScalarValue::fromUInt32(0U);
    default:
        return std::nullopt;
    }
}

/// @brief 保持命令和只读限制。外部导入额外跳过固件时间参数。
/// @param definition 本次调用借用的目录定义。
/// @param source 外部文件或工程。工程中的时间 Set 与功能页一样保留固件原值。
bool shouldSkipDefinition(const ParameterDefinition& definition, StagedParameterSource source) {
    if (!isScalarDefinition(definition)) {
        return true;
    }
    if (definition.access && (*definition.access == AccessMode::ReadOnly || *definition.access == AccessMode::Command)) {
        return true;
    }
    return definition.editor == ernc::domain::EditorKind::MomentaryCommand
        || definition.editor == ernc::domain::EditorKind::ReadOnly
        || (source == StagedParameterSource::LegacyJson
            && (definition.id.text() == "running_time" || definition.id.text() == "max_time"));
}

/// @brief 按目标类型转换旧格式数值，整数保持历史四舍五入规则并在转换前检查范围。
/// @param type 目录声明或已解析布局的标量类型。
/// @param parsed 本次调用借用的文件数值，不换算单位。
Result<ScalarValue> adaptLegacyScalar(DataType type, const ScalarValue& parsed) {
    double number;
    if (const auto floating = parsed.asFloat(); floating.ok()) {
        number = floating.value();
    } else if (const auto integer = parsed.asInt32(); integer.ok()) {
        number = integer.value();
    } else {
        number = parsed.asUInt32().value();
    }
    if (!std::isfinite(number)) {
        return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
    }
    const double rounded = std::round(number);
    switch (type) {
        case DataType::Float32:
            return Result<ScalarValue>::success(ScalarValue::fromFloat(static_cast<float>(number)));
        case DataType::Int32:
            if (rounded >= std::numeric_limits<std::int32_t>::min() && rounded <= std::numeric_limits<std::int32_t>::max()) {
                return Result<ScalarValue>::success(ScalarValue::fromInt32(static_cast<std::int32_t>(rounded)));
            }
            break;
        case DataType::UInt32:
            if (rounded >= 0 && rounded <= std::numeric_limits<std::uint32_t>::max()) {
                return Result<ScalarValue>::success(ScalarValue::fromUInt32(static_cast<std::uint32_t>(rounded)));
            }
            break;
    }
    return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
}

}  // namespace

Result<LegacyParamJsonReport> importToStaged(const ParameterCatalog& catalog, const FirmwareLayout& layout,
                                             ParameterRuntimeStore& store, const LegacyParamJsonDocument& document,
                                             RuntimeTime stagedAt, const LegacyParamJsonReport* parseReport,
                                             StagedParameterSource source) {
    if (document.version != kLegacyParamJsonVersion) {
        return Result<LegacyParamJsonReport>::failure(ErrorCode::InvalidParameter);
    }
    LegacyParamJsonReport report;
    if (parseReport != nullptr) {
        report = *parseReport;
        report.appliedCount = 0;
    }
    ParameterStager stager(catalog, layout, store);
    for (const LegacyParamJsonEntry& entry : document.params) {
        const ParameterDefinition* definition = catalog.find(entry.name);
        if (definition == nullptr) {
            report.unknownNames.push_back(entry.name);
            continue;
        }
        if (shouldSkipDefinition(*definition, source)) {
            report.skippedNames.push_back(entry.name);
            continue;
        }
        auto type = definition->dataType;
        if (!type && definition->firmwarePath) {
            const auto located = layout.locate(*definition->firmwarePath);
            if (located.ok()) {
                type = located.field().scalarType;
            }
        }
        if (!type) {
            report.skippedNames.push_back(entry.name);
            continue;
        }
        const Result<ScalarValue> value = adaptLegacyScalar(*type, entry.value);
        if (!value.ok()) {
            report.skippedNames.push_back(entry.name);
            continue;
        }
        const Result<void> staged = stager.stage(definition->id, value.value(), stagedAt);
        if (!staged.ok()) {
            report.skippedNames.push_back(entry.name);
            continue;
        }
        ++report.appliedCount;
    }
    return Result<LegacyParamJsonReport>::success(std::move(report));
}

Result<std::int64_t> importToSnapshot(ParameterSnapshotService& service, const CaptureSnapshotRequest& request,
                                      const ParameterCatalog& catalog, ParameterRuntimeStore& store,
                                      const LegacyParamJsonDocument& document, RuntimeTime stagedAt) {
    (void)store;
    (void)stagedAt;
    return service.importLegacyDocument(request, catalog, document);
}

Result<LegacyParamJsonDocument> exportStagedThenDevice(const ParameterCatalog& catalog,
                                                       const ParameterRuntimeStore& store) {
    LegacyParamJsonDocument document;
    document.version = std::string(kLegacyParamJsonVersion);
    for (const ParameterDefinition& definition : catalog.entries()) {
        if (!isScalarDefinition(definition)) {
            continue;
        }
        std::optional<ScalarValue> chosen;
        if (const auto* state = store.find(definition.id)) {
            if (state->stagedValue) {
                chosen = *state->stagedValue;
            } else if (const auto* read = state->displayedRead()) {
                chosen = *read;
            }
        }
        if (!chosen) {
            chosen = defaultScalarZero(definition);
        }
        if (!chosen) {
            continue;
        }
        document.params.push_back(LegacyParamJsonEntry{definition.id.text(), *chosen});
    }
    return Result<LegacyParamJsonDocument>::success(std::move(document));
}

LegacyParamJsonDocument captureStagedParameters(const ParameterCatalog& catalog, const ParameterRuntimeStore& store) {
    LegacyParamJsonDocument document{std::string(kLegacyParamJsonVersion), {}};
    for (const ParameterDefinition& definition : catalog.entries()) {
        const auto* state = store.find(definition.id);
        if (state != nullptr && state->stagedValue && isScalarDefinition(definition)) {
            document.params.push_back({definition.id.text(), *state->stagedValue});
        }
    }
    return document;
}

LegacyParamJsonDocument captureReadParameters(const ParameterCatalog& catalog, const ParameterRuntimeStore& store) {
    LegacyParamJsonDocument document{std::string(kLegacyParamJsonVersion), {}};
    for (const ParameterDefinition& definition : catalog.entries()) {
        if (!isScalarDefinition(definition)) {
            continue;
        }
        std::optional<ScalarValue> chosen;
        if (const auto* state = store.find(definition.id)) {
            if (const auto* read = state->displayedRead()) {
                chosen = *read;
            }
        }
        if (!chosen) {
            chosen = defaultScalarZero(definition);
        }
        if (!chosen) {
            continue;
        }
        document.params.push_back({definition.id.text(), *chosen});
    }
    return document;
}

LegacyParamJsonReport restoreProjectReads(const ParameterCatalog& catalog, const FirmwareLayout& layout,
                                        ParameterRuntimeStore& store,
                                        const ernc::infrastructure::LegacyParamJsonLoad& loaded) {
    LegacyParamJsonReport report = loaded.report;
    for (const auto& entry : loaded.document.params) {
        const auto* definition = catalog.find(entry.name);
        if (definition == nullptr) {
            report.unknownNames.push_back(entry.name);
            continue;
        }
        if (!isScalarDefinition(*definition) || !definition->firmwarePath) {
            report.skippedNames.push_back(entry.name);
            continue;
        }
        const auto located = layout.locate(*definition->firmwarePath);
        if (!located.ok() || !located.field().scalarType || located.field().slotCount != 1U
            || located.field().size != 4U || (definition->dataType && definition->dataType != located.field().scalarType)) {
            report.skippedNames.push_back(entry.name);
            continue;
        }
        const auto value = adaptLegacyScalar(*located.field().scalarType, entry.value);
        if (!value.ok()) {
            report.malformedEntries.push_back(entry.name);
            continue;
        }
        if (store.restoreProjectRead(definition->id, value.value())) {
            ++report.appliedCount;
        }
    }
    return report;
}

}  // namespace ernc::application
