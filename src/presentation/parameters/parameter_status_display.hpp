#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QString>

namespace ernc::domain {
class ArrayRuntimeStore;
}

namespace ernc::presentation {

/// @brief 由标量运行状态生成「状态」列正文（1-slot 参数）。
QString parameterStatusText(const ernc::domain::ParameterRuntimeState* state);

/// @brief 按目录项选择标量或紧凑数组 Store，生成参数表「状态」列正文。
QString parameterRowStatusText(const ernc::domain::ParameterDefinition& definition,
                               const ernc::domain::FirmwareLayout* layout,
                               const ernc::domain::ParameterRuntimeStore* scalarStore,
                               const ernc::domain::ArrayRuntimeStore* arrayStore);

/// @brief 该行是否应显示为已从设备读回且未过期（用于「只看已读取」筛选）。
bool parameterRowReadObserved(const ernc::domain::ParameterDefinition& definition,
                              const ernc::domain::FirmwareLayout* layout,
                              const ernc::domain::ParameterRuntimeStore* scalarStore,
                              const ernc::domain::ArrayRuntimeStore* arrayStore);

/// @brief 该行是否有未提交的写入目标（标量 dirty 或数组元素 dirty）。
bool parameterRowHasPendingWrite(const ernc::domain::ParameterDefinition& definition,
                                 const ernc::domain::FirmwareLayout* layout,
                                 const ernc::domain::ParameterRuntimeStore* scalarStore,
                                 const ernc::domain::ArrayRuntimeStore* arrayStore);

} // namespace ernc::presentation
