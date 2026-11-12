#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"

#include <string_view>

namespace ernc::domain {

/// @brief 是否为 check_data / ERNC_TF 等区域路径（不走紧凑数组编辑器）。
bool isRegionLikeFirmwarePath(std::string_view firmwarePath) noexcept;

/// @brief 是否为 6×120 级曲线矩阵（走曲线分析，不走紧凑数组表）。
bool isCurveMatrixFirmwarePath(std::string_view firmwarePath) noexcept;

/// @brief 是否应在功能页/数组详情中使用紧凑数组交互（多 slot 同质标量，非 Region/曲线矩阵）。
bool shouldUseCompactArrayUi(const ParameterDefinition* definition, const FirmwareLayout& layout);

/// @brief 从布局字段解析元素种类。
ScalarValue::Kind scalarKindForField(const FieldLayout& field) noexcept;

}  // namespace ernc::domain
