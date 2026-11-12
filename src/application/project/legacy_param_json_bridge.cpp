#include "ernc/application/project/legacy_param_json_bridge.hpp"

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
using ernc::infrastructure::LegacyParamJsonDocument;
using ernc::infrastructure::LegacyParamJsonEntry;
using ernc::infrastructure::LegacyParamJsonReport;
using ernc::infrastructure::kLegacyParamJsonVersion;

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

}  // namespace

void appendUnique(std::vector<std::string>& target, const std::vector<std::string>& source) {
    for (const std::string& item : source) {
        if (std::find(target.begin(), target.end(), item) == target.end()) {
            target.push_back(item);
        }
    }
}

Result<LegacyParamJsonReport> importToStaged(const ParameterCatalog& catalog, const FirmwareLayout& layout,
                                             ParameterRuntimeStore& store, const LegacyParamJsonDocument& document,
                                             RuntimeTime stagedAt, const LegacyParamJsonReport* parseReport) {
    if (document.version != kLegacyParamJsonVersion) {
        return Result<LegacyParamJsonReport>::failure(ErrorCode::InvalidParameter);
    }
    LegacyParamJsonReport report;
    if (parseReport != nullptr) {
        appendUnique(report.skippedNames, parseReport->skippedNames);
        appendUnique(report.unknownNames, parseReport->unknownNames);
    }
    ParameterStager stager(catalog, layout, store);
    for (const LegacyParamJsonEntry& entry : document.params) {
        const ParameterDefinition* definition = catalog.find(entry.name);
        if (definition == nullptr) {
            report.unknownNames.push_back(entry.name);
            continue;
        }
        if (shouldSkipDefinition(*definition)) {
            report.skippedNames.push_back(entry.name);
            continue;
        }
        const Result<ParameterId> id = ParameterId::parse(entry.name);
        if (!id.ok()) {
            report.malformedEntries.push_back(entry.name);
            continue;
        }
        const Result<ScalarValue> value = adaptLegacyScalar(*definition, entry.value);
        if (!value.ok()) {
            report.skippedNames.push_back(entry.name);
            continue;
        }
        const Result<void> staged = stager.stage(id.value(), value.value(), stagedAt);
        if (!staged.ok()) {
            if (staged.error() == ErrorCode::ReadOnlyViolation) {
                report.skippedNames.push_back(entry.name);
            } else {
                report.skippedNames.push_back(entry.name);
            }
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
        const auto* state = store.find(definition.id);
        if (state == nullptr) {
            continue;
        }
        std::optional<ScalarValue> chosen;
        if (state->stagedValue) {
            chosen = *state->stagedValue;
        } else if (state->deviceValue) {
            chosen = *state->deviceValue;
        }
        if (!chosen) {
            continue;
        }
        document.params.push_back(LegacyParamJsonEntry{definition.id.text(), *chosen});
    }
    return Result<LegacyParamJsonDocument>::success(std::move(document));
}

}  // namespace ernc::application
