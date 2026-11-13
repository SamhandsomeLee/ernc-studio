#pragma once

#include "ernc/domain/array_parameter.hpp"
#include "ernc/domain/array_runtime_store.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/types.hpp"

#include <QString>

namespace ernc::presentation {

/// @brief 功能页数组行摘要，例如「长度12 · 已读 · 2 待写」。
QString compactArrayCaption(const ernc::domain::ParameterDefinition& definition,
                            const ernc::domain::FirmwareLayout& layout,
                            const ernc::domain::ArrayRuntimeStore* arrayStore);

/// @brief 当前参数是否应使用数组详情，而不是标量行编辑。
bool usesCompactArrayUi(const ernc::domain::ParameterDefinition* definition,
                        const ernc::domain::FirmwareLayout* layout);

}  // namespace ernc::presentation
