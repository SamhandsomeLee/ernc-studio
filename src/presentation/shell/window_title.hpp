#pragma once

#include <QString>

namespace ernc::presentation {

/// @brief 主窗口标题前缀，含 6 位构建时 Git 提交号（无 Git 时仅为产品名）。
QString applicationBaseTitle();

/// @brief 完整窗口标题。suffix 为空时只返回前缀。
QString applicationWindowTitle(const QString& suffix = {});

}  // namespace ernc::presentation
