#pragma once

#include <QColor>

#include <cstddef>
#include <string_view>

namespace ernc::presentation {

/// @brief 分析曲线色板。与主题无关的区分色，非设备真值。
/// @param index 曲线序号，从 0 开始。
/// @return 色板循环取色。
QColor curveColorForIndex(std::size_t index) noexcept;

}  // namespace ernc::presentation
