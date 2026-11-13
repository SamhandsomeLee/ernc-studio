#pragma once

#include "ernc/domain/runtime_store.hpp"

#include <QString>

namespace ernc::presentation {

/// @brief 由运行状态生成参数表「状态」列正文。
/// @param state Store 记录。空表示未读取。
QString parameterStatusText(const ernc::domain::ParameterRuntimeState* state);

} // namespace ernc::presentation
