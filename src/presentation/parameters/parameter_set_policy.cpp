#include "parameters/parameter_set_policy.hpp"

#include "ernc/domain/catalog.hpp"

namespace ernc::presentation {
namespace {

using ernc::domain::AccessMode;
using ernc::domain::DataType;
using ernc::domain::EditorKind;
using ernc::domain::ParameterDefinition;
using ernc::domain::ValueShape;

bool isFrozenTimeField(std::string_view id) noexcept {
    return id == "running_time" || id == "max_time";
}

}  // namespace

bool parameterSetColumnEditable(const ParameterDefinition& definition) noexcept {
    if (definition.editor) {
        const EditorKind kind = *definition.editor;
        return kind == EditorKind::Toggle || kind == EditorKind::Numeric;
    }
    if (isFrozenTimeField(definition.id.text())) {
        return false;
    }
    if (!definition.access) {
        return false;
    }
    const AccessMode access = *definition.access;
    if (access != AccessMode::ReadWrite && access != AccessMode::WriteOnly) {
        return false;
    }
    if (definition.shape && *definition.shape == ValueShape::Region) {
        return false;
    }
    if (!definition.dataType) {
        return false;
    }
    switch (*definition.dataType) {
    case DataType::Float32:
    case DataType::Int32:
    case DataType::UInt32:
        return true;
    default:
        return false;
    }
}

bool parameterSetUsesNumericEditor(const ParameterDefinition& definition) noexcept {
    if (definition.editor && *definition.editor == EditorKind::Numeric) {
        return true;
    }
    if (definition.editor && *definition.editor == EditorKind::Toggle) {
        return false;
    }
    return parameterSetColumnEditable(definition);
}

}  // namespace ernc::presentation
