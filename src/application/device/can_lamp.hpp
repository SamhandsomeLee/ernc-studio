#pragma once

namespace ernc::application {

/// CAN 监视指示灯语义色；与 Qt 调色板无关，由上层映射到具体样式。
enum class CanLampColor {
    Gray,
    Red,
    Green,
};

/// 判断 0/1 灯色时允许的浮点容差。
constexpr float kCanLampValueEpsilon = 1e-4f;

/// @brief 按原始 float 值判定指示灯颜色。
/// @param value Can_Msg 槽位读到的值；hasValue 为 false 时不使用。
/// @param hasValue 是否有有效观测；断连或未读时为 false。
/// @return 无值 Gray；接近 0 Red；接近 1 Green；其余 Gray。
[[nodiscard]] CanLampColor evaluateCanLamp(float value, bool hasValue) noexcept;

}  // namespace ernc::application
