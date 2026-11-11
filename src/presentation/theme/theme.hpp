#pragma once

#include <QString>

class QApplication;

namespace ernc::presentation {

/// 浅色主题是否已经应用到进程。失败时界面仍保持 Qt 默认外观，调用方不应继续把它当作已换肤。
struct ThemeLoadResult {
    /// 为 true 时，字体、调色板和全局样式都已经设置。为 false 时三者都没有改。
    bool applied = false;

    /// 失败原因。成功时为空。文本只说明资源或令牌问题，不表示设备状态。
    QString message;
};

/// @brief 把唯一的浅色主题应用到整个应用。
/// @param application 进程里的 Qt 应用对象。调用方拥有它，并且必须在 UI 主线程调用。
/// @return 成功时 `applied` 为 true。资源打不开，或样式表缺少任一令牌颜色时，不调用 `setStyleSheet`。
/// @note 这是本程序唯一设置全局样式表的地方。页面不要再调用 `setStyleSheet`。
ThemeLoadResult applyLightTheme(QApplication& application);

} // namespace ernc::presentation
