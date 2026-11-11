#pragma once

#include "ernc/domain/catalog.hpp"

#include <QMainWindow>
#include <QString>

class QDockWidget;

namespace ernc::presentation {

/// 无设备主窗口。左侧项目树显示目录页面，中间工作区用两个页签显示同一份选择。
/// 底部检查器跟随这份参数 id。任务卡仍是名称占位。本类不保存参数值，也不持有串口。
class MainWindow final : public QMainWindow {
public:
    /// @brief 创建主窗口。目录加载结果决定项目树显示入口还是失败原因。
    /// @param catalog 已尝试加载的参数目录，所有权移入窗口里的树模型。失败结果不会生成空的九个入口。
    /// @param catalogPath 本次加载使用的目录路径，只用于失败说明。调用方拥有原件，本函数复制它。
    /// @param parent 父控件。空时由调用方负责销毁。必须在 UI 主线程创建。
    /// @note Dock 的 `objectName` 要保持稳定。这里不保存布局，也不注册快捷键。
    explicit MainWindow(ernc::domain::CatalogLoad catalog, QString catalogPath, QWidget* parent = nullptr);

private:
    /// @brief 增加一个可拖动、可浮动、可关闭的占位 Dock。
    /// @param objectName 稳定的对象名。调用方拥有该字符串的原件，本函数只复制它。
    /// @param title 标题栏和面板里显示的区域名称。不是设备状态。
    /// @param area 初次停靠的边。之后用户可以拖到其他边。
    /// @return 已经加入本窗口的 Dock。由 Qt 父子关系拥有，调用方只在构造期间借用。
    QDockWidget* addPlaceholderDock(const QString& objectName, const QString& title, Qt::DockWidgetArea area);

    /// @brief 用说明替换项目树里的占位内容。
    /// @param dock 左侧项目树 Dock，调用方拥有。本函数不销毁它。
    /// @param message 要显示的失败原因。不是一套空的页面入口。
    void showNavigatorError(QDockWidget* dock, const QString& message);
};

} // namespace ernc::presentation
