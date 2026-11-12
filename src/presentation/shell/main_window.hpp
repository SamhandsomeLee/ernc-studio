#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/project_bundle.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/array_runtime_store.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/infrastructure/legacy_param_json.hpp"
#include "shell/layout_settings.hpp"

#include <QMainWindow>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

class QAction;
class QCloseEvent;
class QDockWidget;
class QTimer;

namespace ernc::infrastructure {
class AppSessionLog;
}

namespace ernc::presentation {

class EditorTabs;
class InspectorPanel;
class ArrayDetailPanel;
class OperationCenterPanel;
class DeviceController;

/// 桌面主窗口。P1 可在 UI 线程连接内存虚拟从站。不持有 QSerialPort。
class MainWindow final : public QMainWindow {
public:
    /// @brief 创建主窗口。目录加载结果决定项目树显示入口还是失败原因。
    /// @param catalog 已尝试加载的参数目录，所有权移入窗口里的树模型。失败结果不会生成空的九个入口。
    /// @param catalogPath 本次加载使用的目录路径，只用于失败说明。调用方拥有原件，本函数复制它。
    /// @param parent 父控件。空时由调用方负责销毁。必须在 UI 主线程创建。
    /// @note Dock 的对象名保持稳定，供布局恢复使用。`--exit-after-show` 不读写布局设置。
    explicit MainWindow(ernc::domain::CatalogLoad catalog, QString catalogPath, QWidget* parent = nullptr);

    ~MainWindow() override;

protected:
    /// @brief 窗口关闭时保存几何和停靠状态。
    /// @param event 调用方拥有的关闭事件。取消关闭工程或保存失败时忽略。
    /// @note `--exit-after-show` 不写 `QSettings`。不保存参数 id。
    void closeEvent(QCloseEvent* event) override;

private:
    /// @brief 创建文件菜单。动作由窗口拥有，只在 UI 主线程调用一次。
    void addFileMenu();

    /// @brief 选择目标目录并创建空工程。创建失败或取消时保留当前工作区。
    void createProject();

    /// @brief 打开目录工程，恢复历史 Read 和 Set，保留有效的实时读数。
    void openProject();

    /// @brief 保存当前工程的标量 Read 和 Set 值。无工程或保存失败时返回 false。
    bool saveProject();

    /// @brief 询问如何处理尚未保存的 Read 或 Set 值。取消或保存失败时返回 false。
    bool confirmLeavingProject();

    /// @brief 关闭工程，清除工程历史 Read 和 Set。取消时保留原工程和运行态。
    bool closeProject();

    /// @brief 刷新标题和依赖当前工程的菜单可用状态。
    void updateProjectActions();

    /// @brief 读取旧版 JSON 并暂存其中可编辑的标量，显示应用及跳过结果。
    void importParameters();

    /// @brief 按 Set 优先、Read 回退的规则写旧版 JSON，源值只在 UI 线程复制。
    void exportParameters();

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

    /// @brief 写一条 UI 操作到会话日志并刷新操作中心。
    void recordUiAction(std::string_view action, std::string_view detail, bool success,
                        ernc::domain::ErrorCode error = ernc::domain::ErrorCode::Unknown);

    void updateDeviceStatusBar(bool connected, ernc::domain::SessionId sessionId);
    void onDeviceConnectionChanged(bool connected, ernc::domain::SessionId sessionId);
    void onParametersUpdated(ernc::domain::SessionId sessionId, const QStringList& parameterIds);

    /// 参数运行状态。本窗口拥有。参数表只借用它，不保存第二份数值。
    ernc::domain::ParameterRuntimeStore parameterStore_;
    ernc::domain::ArrayRuntimeStore arrayParameterStore_;

    /// 当前工程摘要。UI 线程持有，SQLite 连接由每次 Storage Worker 操作独占。
    std::optional<ernc::domain::ProjectBundle> project_;

    /// 最近一次成功保存或打开时显示的 Read 值，用于检测未保存的更改。
    std::vector<ernc::infrastructure::LegacyParamJsonEntry> savedProjectRead_;

    /// 最近一次成功保存或打开时显示的 Set 值，用于检测未保存的更改。
    std::vector<ernc::infrastructure::LegacyParamJsonEntry> savedProjectSet_;

    /// 打开工程动作，用于接入快捷键表。菜单通过 Qt 父子关系拥有它。
    QAction* openProjectAction_ = nullptr;

    /// 保存工程动作，只在工程打开后启用。菜单通过 Qt 父子关系拥有它。
    QAction* saveProjectAction_ = nullptr;

    /// 关闭工程动作，只在工程打开后启用。菜单通过 Qt 父子关系拥有它。
    QAction* closeProjectAction_ = nullptr;

    /// 已加载的参数目录。由项目树模型拥有内容，本窗口只保存指针。
    const ernc::domain::ParameterCatalog* catalog_{nullptr};

    /// 固件布局。加载成功时持有。参数委托只借用它来暂存。
    std::optional<ernc::domain::FirmwareLayout> firmwareLayout_;

    /// 区域目录。布局加载成功时持有。曲线分析页只借用。
    std::optional<ernc::domain::RegionCatalog> regionCatalog_;

    /// 中央工作页。由本窗口通过 Qt 父子关系拥有。
    EditorTabs* editorTabs_ = nullptr;

    /// 底部检查器内容。由检查器 Dock 通过 Qt 父子关系拥有。
    InspectorPanel* inspectorPanel_ = nullptr;
    ArrayDetailPanel* arrayDetailPanel_ = nullptr;

    /// 左侧项目树 Dock。由本窗口通过 Qt 父子关系拥有。
    QDockWidget* projectTree_ = nullptr;

    /// 底部检查器 Dock。由本窗口通过 Qt 父子关系拥有。
    QDockWidget* inspectorDock_ = nullptr;

    /// 右侧任务卡 Dock。由本窗口通过 Qt 父子关系拥有。
    QDockWidget* taskCards_ = nullptr;

    /// 右侧操作中心 Dock。由本窗口通过 Qt 父子关系拥有。
    QDockWidget* operationCenter_ = nullptr;

    /// 操作中心内容。由 Dock 拥有指针副本，用于刷新日志视图。
    OperationCenterPanel* operationPanel_ = nullptr;

    /// 本次桌面运行的会话日志。失败创建目录时为空。
    std::unique_ptr<ernc::infrastructure::AppSessionLog> sessionLog_;

    /// 虚拟从站与命令泵。目录和布局齐全时创建。
    DeviceController* deviceController_{nullptr};

    /// 连接设备时驱动调度器。由本窗口拥有。
    QTimer* devicePumpTimer_{nullptr};
};

} // namespace ernc::presentation
