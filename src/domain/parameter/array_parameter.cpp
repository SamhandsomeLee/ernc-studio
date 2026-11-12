#include "ernc/domain/array_parameter.hpp"

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

bool startsWithPath(std::string_view haystack, std::string_view prefix) noexcept {
    return haystack.size() >= prefix.size() && haystack.substr(0, prefix.size()) == prefix;
}

}  // namespace

bool isRegionLikeFirmwarePath(const std::string_view firmwarePath) noexcept {
    return firmwarePath == "check_data" || startsWithPath(firmwarePath, "check_data.")
        || firmwarePath == "ERNC_TF" || startsWithPath(firmwarePath, "ERNC_TF.");
}

bool isCurveMatrixFirmwarePath(const std::string_view firmwarePath) noexcept {
    if (!startsWithPath(firmwarePath, "rnc_dicergence.")) {
        return false;
    }
    return firmwarePath.find("rnc_diverge_threshold") != std::string_view::npos
        || firmwarePath.find("RNCSpkFAdapMax") != std::string_view::npos;
}

ScalarValue::Kind scalarKindForField(const FieldLayout& field) noexcept {
    if (!field.scalarType.has_value()) {
        return ScalarValue::Kind::Float32;
    }
    if (*field.scalarType == DataType::Int32) {
        return ScalarValue::Kind::Int32;
    }
    if (*field.scalarType == DataType::UInt32) {
        return ScalarValue::Kind::UInt32;
    }
    return ScalarValue::Kind::Float32;
}

bool shouldUseCompactArrayUi(const ParameterDefinition* definition, const FirmwareLayout& layout) {
    if (definition == nullptr || !definition->firmwarePath || definition->firmwarePath->empty()) {
        return false;
    }
    const std::string_view path = *definition->firmwarePath;
    if (isRegionLikeFirmwarePath(path) || isCurveMatrixFirmwarePath(path)) {
        return false;
    }
    if (definition->shape && *definition->shape == ValueShape::Region) {
        return false;
    }
    if (definition->editor && *definition->editor == EditorKind::RegionEditor) {
        return false;
    }
    const LayoutResolve resolved = layout.locate(path);
    if (!resolved.ok()) {
        return false;
    }
    const FieldLayout& field = resolved.field();
    if (!field.scalarType.has_value() || field.slotCount <= 1U) {
        return false;
    }
    return true;
}

}  // namespace ernc::domain
