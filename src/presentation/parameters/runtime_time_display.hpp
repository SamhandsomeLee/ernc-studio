#pragma once

#include "ernc/domain/runtime_store.hpp"

#include <QString>

#include <string>

namespace ernc::presentation {

/// @brief 把系统钟瞬间格式化为北京时间文字，供参数表等 UI 列显示。
/// @param time 读回或暂存时刻；按 UTC 纪元解释后再换到上海时区。
/// @return 例如 2026-11-13 11:03:08；秒以下被丢弃。
QString formatRuntimeTimeBeijing(ernc::domain::RuntimeTime time);

/// @brief 与 formatRuntimeTimeBeijing 相同语义，UTF-8 供检查器等非 Qt 控件路径。
std::string formatRuntimeTimeBeijingUtf8(ernc::domain::RuntimeTime time);

}  // namespace ernc::presentation
