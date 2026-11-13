#include "parameters/parameter_status_display.hpp"

#include "ernc/domain/array_parameter.hpp"
#include "ernc/domain/array_runtime_store.hpp"

namespace ernc::presentation {
namespace {

bool useCompactArray(const ernc::domain::ParameterDefinition& definition,
                     const ernc::domain::FirmwareLayout* layout) noexcept {
    return layout != nullptr && ernc::domain::shouldUseCompactArrayUi(&definition, *layout);
}

QString compactArrayStatusText(const ernc::domain::ParameterId& id, const ernc::domain::ArrayRuntimeStore& arrayStore) {
    const ernc::domain::ArrayRuntimeMeta* meta = arrayStore.meta(id);
    const bool anyRead = arrayStore.hasAnyRead(id);
    if (meta != nullptr && meta->stale && anyRead) {
        return QStringLiteral("数据已过期");
    }
    if (arrayStore.dirtyCount(id) > 0U) {
        return QStringLiteral("待写入");
    }
    if (meta != nullptr && !meta->stale && anyRead) {
        return QStringLiteral("已读取");
    }
    if (anyRead) {
        return QStringLiteral("已读取");
    }
    return QStringLiteral("未读取");
}

} // namespace

QString parameterStatusText(const ernc::domain::ParameterRuntimeState* state) {
    if (state == nullptr) {
        return QStringLiteral("未读取");
    }
    if (state->projectReadValue) {
        return QStringLiteral("工程历史读数");
    }
    if (state->stale && state->deviceValue.has_value()) {
        return QStringLiteral("数据已过期");
    }
    if (state->dirty) {
        return QStringLiteral("待写入");
    }
    if (state->deviceValue.has_value() && !state->stale) {
        return QStringLiteral("已读取");
    }
    return QStringLiteral("未读取");
}

QString parameterRowStatusText(const ernc::domain::ParameterDefinition& definition,
                               const ernc::domain::FirmwareLayout* layout,
                               const ernc::domain::ParameterRuntimeStore* scalarStore,
                               const ernc::domain::ArrayRuntimeStore* arrayStore) {
    if (useCompactArray(definition, layout) && arrayStore != nullptr) {
        return compactArrayStatusText(definition.id, *arrayStore);
    }
    const ernc::domain::ParameterRuntimeState* state =
        scalarStore == nullptr ? nullptr : scalarStore->find(definition.id);
    return parameterStatusText(state);
}

bool parameterRowReadObserved(const ernc::domain::ParameterDefinition& definition,
                              const ernc::domain::FirmwareLayout* layout,
                              const ernc::domain::ParameterRuntimeStore* scalarStore,
                              const ernc::domain::ArrayRuntimeStore* arrayStore) {
    if (useCompactArray(definition, layout) && arrayStore != nullptr) {
        const ernc::domain::ArrayRuntimeMeta* meta = arrayStore->meta(definition.id);
        return arrayStore->hasAnyRead(definition.id) && meta != nullptr && !meta->stale;
    }
    const ernc::domain::ParameterRuntimeState* state =
        scalarStore == nullptr ? nullptr : scalarStore->find(definition.id);
    return state != nullptr && state->deviceValue.has_value() && !state->stale;
}

bool parameterRowHasPendingWrite(const ernc::domain::ParameterDefinition& definition,
                                   const ernc::domain::FirmwareLayout* layout,
                                   const ernc::domain::ParameterRuntimeStore* scalarStore,
                                   const ernc::domain::ArrayRuntimeStore* arrayStore) {
    if (useCompactArray(definition, layout) && arrayStore != nullptr) {
        return arrayStore->dirtyCount(definition.id) > 0U;
    }
    const ernc::domain::ParameterRuntimeState* state =
        scalarStore == nullptr ? nullptr : scalarStore->find(definition.id);
    return state != nullptr && state->dirty;
}

} // namespace ernc::presentation
