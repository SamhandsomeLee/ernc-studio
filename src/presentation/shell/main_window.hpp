#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "shell/layout_settings.hpp"

#include <QMainWindow>
#include <QString>

class QCloseEvent;
class QDockWidget;

namespace ernc::presentation {

class EditorTabs;
class InspectorPanel;

/// 无设备主窗口。左侧项目树显示目录页面，中间工作区用两个页签显示同一份选择。
/// 底部检查器跟随这份参数 id。右侧任务卡和操作中心是无设备原型。本类不保存参数值，也不持有串口。
class MainWindow final : public QMainWindow {
public:
    /// @brief 创建主窗口。目录加载结果决定项目树显示入口还是失败原因。
    /// @param catalog 已尝试加载的参数目录，所有权移入窗口里的树模型。失败结果不会生成空的九个入口。
    /// @param catalogPath 本次加载使用的目录路径，只用于失败说明。调用方拥有原件，本函数复制它。
    /// @param parent 父控件。空时由调用方负责销毁。必须在 UI 主线程创建。
    /// @note Dock 的对象名保持稳定，供布局恢复使用。`--exit-after-show` 不读写布局设置。
    explicit MainWindow(ernc::domain::CatalogLoad catalog, QString catalogPath, QWidget* parent = nullptr);

protected:
    /// @brief 窗口关闭时保存几何和停靠状态。
    /// @param event 关闭事件。调用方拥有。本函数接受它，不忽略关闭。
    /// @note `--exit-after-show` 不写 `QSettings`。不保存参数 id。
    void closeEvent(QCloseEvent* event) override;

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

    /// @brief 按一份布局描述显示停靠区和页签。
    /// @param layout 目标布局。本函数只读它，不保存。
    /// @note 不写设备，也不改参数 id。只在 UI 主线程调用。
    void applyWorkspace(const WorkspaceLayout& layout);

    /// @brief 按屏幕可用区域设置起步大小，并套用宽度规则。
    /// @note 不读取已保存的布局。
    void applyDefaultShellLayout();

    /// @brief 恢复上次保存的窗口布局。
    /// @return 读到了几何或停靠状态时为 true。冒烟退出或两边都空时为 false。
    /// @note 停靠状态恢复失败时，按当前宽度重套默认规则。
    bool restoreSavedShellLayout();

    /// @brief 删掉保存的布局，并按当前窗口宽度恢复默认停靠区。
    /// @note 不改变窗口大小。冒烟退出时不碰 `QSettings`。
    void restoreDefaultShellLayout();

    /// @brief 按快捷键表绑定启用的动作。
    /// @note 表缺失或解析失败时不绑定，窗口仍然打开。保留项不创建快捷键。
    void bindShellKeymap();

    /// @brief 执行一个已经绑定、但不是停靠区开关的动作。
    /// @param action 动作名，只在本次调用期间借用。未知动作被忽略。
    /// @note `close-overlay` 在没有浮层时什么都不做。不取消写入。
    void runBoundAction(std::string_view action);

    /// 参数运行状态。本窗口拥有。参数表只借用它，不保存第二份数值。
    ernc::domain::ParameterRuntimeStore parameterStore_;

    /// 中央工作页。由本窗口通过 Qt 父子关系拥有。
    EditorTabs* editorTabs_ = nullptr;

    /// 底部检查器内容。由检查器 Dock 通过 Qt 父子关系拥有。
    InspectorPanel* inspectorPanel_ = nullptr;

    /// 左侧项目树 Dock。由本窗口通过 Qt 父子关系拥有。
    QDockWidget* projectTree_ = nullptr;

    /// 底部检查器 Dock。由本窗口通过 Qt 父子关系拥有。
    QDockWidget* inspectorDock_ = nullptr;

    /// 右侧任务卡 Dock。由本窗口通过 Qt 父子关系拥有。
    QDockWidget* taskCards_ = nullptr;

    /// 右侧操作中心 Dock。由本窗口通过 Qt 父子关系拥有。
    QDockWidget* operationCenter_ = nullptr;
};

} // namespace ernc::presentation
