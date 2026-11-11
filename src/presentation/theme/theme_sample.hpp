#pragma once

class QWidget;

namespace ernc::presentation {

/// @brief 创建浅色主题的状态样例。
/// @param parent 父控件。非空时由 Qt 父子关系拥有返回的控件；空时由调用方负责销毁。必须在 UI 主线程创建。
/// @return 已排好的样例控件。它只展示固定状态说法，不连接设备，也不是后续任务里的 Dock 壳。
QWidget* createThemeSample(QWidget* parent = nullptr);

} // namespace ernc::presentation
