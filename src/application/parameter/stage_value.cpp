#include "ernc/application/stage_value.hpp"

#include "ernc/domain/validation.hpp"

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_application must not include Qt"
#endif

namespace ernc::application {

ParameterStager::ParameterStager(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                                 ernc::domain::ParameterRuntimeStore& store)
    : catalog_{catalog}, layout_{layout}, store_{store} {}

ernc::domain::Result<void> ParameterStager::stage(const ernc::domain::ParameterId& id, const ernc::domain::ScalarValue& value,
                                                  ernc::domain::RuntimeTime stagedAt) {
    const ernc::domain::ParameterDefinition* definition = catalog_.find(id.text());
    if (definition == nullptr || !definition->firmwarePath || definition->firmwarePath->empty()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<void> valid = ernc::domain::validateStagedValue(*definition, value);
    if (!valid.ok()) {
        return valid;
    }
    const ernc::domain::LayoutResolve located = layout_.locate(*definition->firmwarePath);
    if (!located.ok()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::FieldLayout& field = located.field();
    if (!field.scalarType || field.slotCount != 1U || field.size != 4U) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (definition->dataType && *definition->dataType != *field.scalarType) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    store_.stage(id, value, stagedAt);
    return ernc::domain::Result<void>::success();
}

}  // namespace ernc::application
