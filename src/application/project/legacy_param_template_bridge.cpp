#include "ernc/application/project/legacy_param_template_bridge.hpp"

#include "ernc/application/stage_value.hpp"

#include <algorithm>
#include <cmath>

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
using ernc::infrastructure::LegacyExportTemplate;
using ernc::infrastructure::LegacyParamJsonDocument;
using ernc::infrastructure::LegacyParamJsonEntry;
using ernc::infrastructure::LegacyParamJsonReport;
using ernc::infrastructure::LegacyParamsFile;
using ernc::infrastructure::kLegacyParamJsonVersion;
using ernc::infrastructure::serializeLegacyParamsFile;

bool isScalarDefinition(const ParameterDefinition& definition) {
    if (!definition.dataType.has_value()) {
        return false;
    }
    if (definition.shape.has_value() && *definition.shape != ValueShape::Scalar) {
        return false;
    }
    return true;
}

bool shouldSkipDefinition(const ParameterDefinition& definition) {
    if (!isScalarDefinition(definition)) {
        return true;
    }
    if (definition.access && (*definition.access == AccessMode::ReadOnly || *definition.access == AccessMode::Command)) {
        return true;
    }
    if (definition.editor && *definition.editor == ernc::domain::EditorKind::MomentaryCommand) {
        return true;
    }
    return false;
}

Result<ScalarValue> adaptLegacyScalar(const ParameterDefinition& definition, const ScalarValue& parsed) {
    if (!definition.dataType) {
        return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
    }
    const Result<float> asFloat = parsed.asFloat();
    if (!asFloat.ok()) {
        return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
    }
    switch (*definition.dataType) {
        case DataType::Float32:
            return Result<ScalarValue>::success(ScalarValue::fromFloat(asFloat.value()));
        case DataType::Int32:
            return Result<ScalarValue>::success(ScalarValue::fromInt32(static_cast<std::int32_t>(std::lround(asFloat.value()))));
        case DataType::UInt32: {
            const double rounded = std::lround(asFloat.value());
            if (rounded < 0) {
                return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
            }
            return Result<ScalarValue>::success(ScalarValue::fromUInt32(static_cast<std::uint32_t>(rounded)));
        }
    }
    return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
}

std::optional<ScalarValue> resolveStagedThenDevice(const ParameterCatalog& catalog, const ParameterRuntimeStore& store,
                                                 std::string_view name) {
    const ParameterDefinition* definition = catalog.find(name);
    if (definition == nullptr || shouldSkipDefinition(*definition)) {
        return std::nullopt;
    }
    const auto* state = store.find(definition->id);
    if (state == nullptr) {
        return std::nullopt;
    }
    if (state->stagedValue) {
        return *state->stagedValue;
    }
    if (state->deviceValue) {
        return *state->deviceValue;
    }
    return std::nullopt;
}

double scalarToExportDouble(const ScalarValue& value) {
    const Result<float> asFloat = value.asFloat();
    if (asFloat.ok()) {
        return static_cast<double>(asFloat.value());
    }
    const Result<std::int32_t> asInt = value.asInt32();
    if (asInt.ok()) {
        return static_cast<double>(asInt.value());
    }
    const Result<std::uint32_t> asUInt = value.asUInt32();
    if (asUInt.ok()) {
        return static_cast<double>(asUInt.value());
    }
    return 0.0;
}

}  // namespace

Result<std::string> exportParamsWithTemplate(const ParameterCatalog& catalog, const ParameterRuntimeStore& store,
                                             const LegacyExportTemplate& template_) {
    if (template_.rowLimit == 0U) {
        return Result<std::string>::failure(ErrorCode::InvalidParameter);
    }
    std::vector<double> rows;
    rows.reserve(template_.rowLimit);
    std::size_t linesCount = 0;
    for (const std::string& name : template_.names) {
        if (linesCount >= template_.rowLimit) {
            break;
        }
        const std::optional<ScalarValue> held = resolveStagedThenDevice(catalog, store, name);
        rows.push_back(held ? scalarToExportDouble(*held) : 0.0);
        ++linesCount;
    }
    while (linesCount < template_.rowLimit) {
        rows.push_back(0.0);
        ++linesCount;
    }
    return serializeLegacyParamsFile(rows);
}

Result<LegacyParamJsonReport> importParamsToStaged(const ParameterCatalog& catalog, const FirmwareLayout& layout,
                                                   ParameterRuntimeStore& store, const LegacyExportTemplate& template_,
                                                   const LegacyParamsFile& paramsFile, RuntimeTime stagedAt) {
    if (paramsFile.values.size() != template_.rowLimit) {
        return Result<LegacyParamJsonReport>::failure(ErrorCode::InvalidParameter);
    }
    LegacyParamJsonReport report;
    ParameterStager stager(catalog, layout, store);
    const std::size_t bound = std::min(template_.names.size(), template_.rowLimit);
    for (std::size_t index = 0; index < bound; ++index) {
        const std::string& name = template_.names[index];
        const ParameterDefinition* definition = catalog.find(name);
        if (definition == nullptr) {
            report.unknownNames.push_back(name);
            continue;
        }
        if (shouldSkipDefinition(*definition)) {
            report.skippedNames.push_back(name);
            continue;
        }
        const Result<ParameterId> id = ParameterId::parse(name);
        if (!id.ok()) {
            report.malformedEntries.push_back(name);
            continue;
        }
        const ScalarValue parsed = ScalarValue::fromFloat(static_cast<float>(paramsFile.values[index]));
        const Result<ScalarValue> value = adaptLegacyScalar(*definition, parsed);
        if (!value.ok()) {
            report.skippedNames.push_back(name);
            continue;
        }
        const Result<void> staged = stager.stage(id.value(), value.value(), stagedAt);
        if (!staged.ok()) {
            report.skippedNames.push_back(name);
            continue;
        }
        ++report.appliedCount;
    }
    return Result<LegacyParamJsonReport>::success(std::move(report));
}

Result<std::int64_t> importParamsToSnapshot(ParameterSnapshotService& service, const CaptureSnapshotRequest& request,
                                            const ParameterCatalog& catalog, const ParameterRuntimeStore& store,
                                            const LegacyExportTemplate& template_, const LegacyParamsFile& paramsFile) {
    (void)store;
    if (paramsFile.values.size() != template_.rowLimit) {
        return Result<std::int64_t>::failure(ErrorCode::InvalidParameter);
    }
    LegacyParamJsonDocument document;
    document.version = std::string(kLegacyParamJsonVersion);
    const std::size_t bound = std::min(template_.names.size(), template_.rowLimit);
    for (std::size_t index = 0; index < bound; ++index) {
        const std::string& name = template_.names[index];
        const ParameterDefinition* definition = catalog.find(name);
        if (definition == nullptr || shouldSkipDefinition(*definition)) {
            continue;
        }
        document.params.push_back(
            LegacyParamJsonEntry{name, ScalarValue::fromFloat(static_cast<float>(paramsFile.values[index]))});
    }
    return service.importLegacyDocument(request, catalog, document, kLegacyParamsImportSnapshotSource);
}

}  // namespace ernc::application
