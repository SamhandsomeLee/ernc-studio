#pragma once

namespace ernc::domain {
struct ParameterDefinition;
}

namespace ernc::presentation {

/// @brief 参数表「写」列是否允许暂存。
/// 显式 toggle/numeric 可写；未声明 editor 的 read_write 标量默认可用行内编辑。
/// running_time / max_time 仅在函数视图编辑。
[[nodiscard]] bool parameterSetColumnEditable(const ernc::domain::ParameterDefinition& definition) noexcept;

/// @brief 写列是否使用 QLineEdit（含隐式 numeric）。
[[nodiscard]] bool parameterSetUsesNumericEditor(const ernc::domain::ParameterDefinition& definition) noexcept;

}  // namespace ernc::presentation
