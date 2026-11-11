#pragma once

#include <QMainWindow>

class QDockWidget;
class QString;

namespace ernc::presentation {

/// 无设备主窗口骨架。左侧是项目树，中间是工作区，底部是检查器，右侧是任务卡。
/// 这些区域只有名称占位。本类不读取 Catalog，不保存参数，也不持有串口。
class MainWindow final : public QMainWindow {
public:
    /// @brief 创建主窗口并放好五个区域。
    /// @param parent 父控件。空时由调用方负责销毁。必须在 UI 主线程创建。
    /// @note `objectName` 要保持稳定，供以后恢复布局。这里不保存布局，也不注册快捷键。
    explicit MainWindow(QWidget* parent = nullptr);

private:
    /// @brief 增加一个可拖动、可浮动、可关闭的占位 Dock。
    /// @param objectName 稳定的对象名。调用方拥有该字符串的原件，本函数只复制它。
    /// @param title 标题栏和面板里显示的区域名称。不是设备状态。
    /// @param area 初次停靠的边。之后用户可以拖到其他边。
    /// @return 已经加入本窗口的 Dock。由 Qt 父子关系拥有，调用方只在构造期间借用。
    QDockWidget* addPlaceholderDock(const QString& objectName, const QString& title, Qt::DockWidgetArea area);
};

} // namespace ernc::presentation
