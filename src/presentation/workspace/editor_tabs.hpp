#pragma once

#include "workspace/editor_selection.hpp"

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QModelIndex>
#include <QString>
#include <QTabWidget>

#include "ernc/domain/types.hpp"

#include <functional>
#include <string_view>

class QLabel;
class QLineEdit;
class QTableView;

namespace ernc::domain {
class ArrayRuntimeStore;
}

namespace ernc::presentation {
class ArrayDetailPanel;
class DeviceController;
class CanNamePage;
class ThresholdPage;
class OtherPage;
class CheckPage;
class SwitchStatePage;
class EncParamPage;
class RncParamPage;
class ErncFlagPage;
class ErncInOutPage;
class AnalysisPanel;
class CanInfoPanel;
class ParameterFilterProxy;
class ParameterTableModel;
}

namespace ernc::presentation {

/// Function View、Parameter View 和离线曲线分析页。参数页读取同一份参数 id。
/// 切换页签不修改选择。曲线分析只导入本地 BIN/TXT，不连接 Region 设备读写端口。
class EditorTabs final : public QTabWidget {
public:
    /// @brief 创建参数工作页，并在区域目录可用时创建离线曲线分析页。初始没有选中的页面或参数。
    /// @param catalog 参数目录，只借用。空指针时参数表没有行。调用方保证它比本控件活得久。
    /// @param store 运行状态，只借用。空指针时参数表不显示数值。调用方保证它比本控件活得久。
    /// @param layout 固件布局，只借用。空指针时 Set 列确认不会暂存。调用方保证它比本控件活得久。
    /// @param regionCatalog 区域目录，只借用。空指针时不创建曲线分析页。调用方保证它比本控件活得久。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本控件。必须在 UI 主线程创建。
    /// @note 启动时停在 Function View。曲线分析页不绑定 RegionReader 或 RegionWriter。
    /// 预置可以切换页签，窗口状态不单独保存这个页签。
    explicit EditorTabs(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                        const ernc::domain::FirmwareLayout* layout,
                        const ernc::domain::RegionCatalog* regionCatalog = nullptr, QWidget* parent = nullptr);

    /// @brief 切到 Function View。
    /// @note 不改参数 id，也不读取设备。只在 UI 主线程调用。
    void showFunctionView();

    /// @brief 切到 Parameter View。
    /// @note 不改参数 id，也不把焦点送进参数表。只在 UI 主线程调用。
      void showParameterView();

    /// @brief 切到离线曲线分析页。无区域目录时仍停在当前页。
    void showCurveAnalysis();

    /// @brief 切到 CAN 信息页。
    void showCanInfoTab();

    /// @brief 把会话 UI 记录接到曲线分析页。
    /// @param recorder 曲线导入操作的记录回调。本控件复制它，空函数表示停止记录。
    void setUiActivityRecorder(
        std::function<void(std::string_view action, std::string_view detail, bool success,
                           ernc::domain::ErrorCode error)>
            recorder);

    /// @brief 切到 Parameter View，并把焦点送进搜索框。
    /// @note 不改搜索文字，不改状态栏。只在 UI 主线程调用。
    void focusParameterTable();

    /// @brief 把焦点交回当前工作页。
    /// @note 不切换页签，不读取设备，不改状态栏。只在 UI 主线程调用。
    void focusCurrentPage();

    /// @brief 把参数 id 写进两页共用的选择。
    /// @param parameterId 目录主键。空文本不改变已有选择。调用方拥有原件，本函数只复制它。
    void selectParameter(const QString& parameterId);

    /// @brief 打开 CAN 名称目录。只在当前树页是 Check 时显示。
    /// @note 不读取 Can_Msg。只在 UI 主线程调用。
    void showCanCatalog();

    /// @brief 关闭 CAN 名称目录。
    /// @note 不改变参数 ID。只在 UI 主线程调用。
    void hideCanCatalog();

    /// @brief 读取 CAN 名称文件。
    /// @param path Excel 导出的 JSON。调用方拥有原件。空路径清空目录。
    /// @note 不把行号写成槽位。只在 UI 主线程调用。
    void setCanNamesPath(const QString& path);

    void setCanSlotMapPath(const QString& path);

    /// @brief 更新当前页面，保留已经选中的参数 id。
    /// @param pageId 项目树页面 id。空文本不改变已有选择。调用方拥有原件，本函数只复制它。
    /// @note 不通知参数观察者。检查器只跟参数 id 走。
    void selectPage(const QString& pageId);

    /// @brief 登记参数 id 变化后的通知。
    /// @param observer 观察者。空函数表示不再通知。本控件复制这份函数。只在 UI 主线程调用。
    /// @note 通知参数是内部 id 的视图，只在这次调用期间有效。观察者不得再调用 `selectParameter`。
    void setParameterObserver(std::function<void(std::string_view)> observer);

    /// @brief 绑定桌面设备控制器。空指针表示无设备能力。
    void setDeviceController(DeviceController* controller);

    void setArrayRuntimeStore(ernc::domain::ArrayRuntimeStore* store) noexcept;
    void setArrayDetailPanel(ArrayDetailPanel* panel) noexcept;

    /// @brief 读取当前项目树页面分类的参数。未连接时提示由控制器记录。
    void readCurrentPage();

    /// @brief 打开当前分类的写入预览（仅本页已修改参数）。
    void openWritePlan();

    /// @brief 打开全工程待写标量的预览。只在确认后进入既有批量写入口。
    void openAllWritePlan();

    void bindParameterSession(ernc::domain::SessionId session);
    void notifyParametersUpdated(ernc::domain::SessionId session, const QStringList& parameterIds);
    void noteDeviceDisconnected();

    /// @brief 文件导入或工程切换后重读 Store，更新参数表与当前功能页。
    /// @note 只在 UI 主线程调用，不依赖设备会话编号。
    void refreshLocalParameters();

private:
    /// @brief 打开一次模态写入计划。已有预览时复用它。
    /// @param pageId 借用的目录分类或 ALL，由对话框复制。
    void showWritePlan(const QString& pageId);

    /// @brief 参数视图可见时定位两视图共享的参数。不更改筛选或暂存状态。
    void syncParameterSelection();

    /// @brief 按当前选择重写 Function View，并让当前功能页占满剩余高度。
    /// @note 参数表不在这里改写。没有功能页时，说明文字仍停在上方。
    void refresh();

    /// @brief 按对象名切到一个工作页。
    /// @param objectName 页面对象名。调用方拥有原件，本函数只比较它。找不到时不改当前页。
    void showNamedPage(const QString& objectName);

    /// @brief 参数表换行时，把该行参数 id 写入共用选择。
    /// @param current 新的当前行。无效时不改选择。本函数不清除页面 id。
    /// @param previous 换行前的当前行。本函数不读取它。
    /// @note 只在 UI 主线程由表格选择模型调用。不写设备，也不暂存 Set。
    void onParameterRowChanged(const QModelIndex& current, const QModelIndex& previous);

    void emphasizeFunctionViewParameter(const QString& parameterId);

    bool showingFunctionView() const;

    /// 两页共用的页面 id 和参数 id。页签切换不会改它。
    EditorSelection selection_;

    /// 参数目录。只借用。空指针时写入预览没有行。调用方保证它比本控件活得久。
    const ernc::domain::ParameterCatalog* catalog_ = nullptr;

    /// 运行状态。只借用。写入预览从这里读 Read 和 Set。调用方保证它比本控件活得久。
    ernc::domain::ParameterRuntimeStore* store_ = nullptr;

    /// 固件布局。只借用。空指针时写入预览不能判断 1 slot。调用方保证它比本控件活得久。
    const ernc::domain::FirmwareLayout* layout_ = nullptr;

    /// ERNC_Flag 功能页。由 Function View 通过 Qt 父子关系拥有。其它页面隐藏它。
    ErncFlagPage* flagPage_ = nullptr;

    /// ERNC_IN_OUT 功能页。由 Function View 通过 Qt 父子关系拥有。其它页面隐藏它。
    ErncInOutPage* inOutPage_ = nullptr;

    /// ENC_Param 阶次页。由 Function View 通过 Qt 父子关系拥有。其它页面隐藏它。
    EncParamPage* encPage_ = nullptr;

    /// RNC_Param 参数页。由 Function View 通过 Qt 父子关系拥有。其它页面隐藏它。
    RncParamPage* rncPage_ = nullptr;

    /// Check 页。由 Function View 通过 Qt 父子关系拥有。其它页面隐藏它。
    CheckPage* checkPage_ = nullptr;

    /// 开关状态页。由 Function View 通过 Qt 父子关系拥有。其它页面隐藏它。
    SwitchStatePage* switchPage_ = nullptr;

    /// CAN 名称页。由 Function View 通过 Qt 父子关系拥有。只从 Check 打开。
    CanNamePage* canPage_ = nullptr;

    /// 阈值页。由 Function View 通过 Qt 父子关系拥有。其它页面隐藏它。
    ThresholdPage* thresholdPage_ = nullptr;

    /// other 分类页。由 Function View 通过 Qt 父子关系拥有。其它页面隐藏它。
    OtherPage* otherPage_ = nullptr;

    /// 离线曲线分析页。有区域目录时创建，由本控件通过 Qt 父子关系拥有。
    AnalysisPanel* curveAnalysis_ = nullptr;

    /// CAN 信息页。独立页签，不放在函数视图内。
    CanInfoPanel* canInfoPanel_ = nullptr;

    /// 为 true 时，Check 树页显示 CAN 名称而不是检测页。
    bool canShown_{false};

    /// 参数表模型。由参数表通过 Qt 父子关系拥有。功能页暂存后用它重画同一行。
    ParameterTableModel* parameterModel_ = nullptr;

    /// 参数表，由参数页拥有。只在参数视图可见时对齐选中行。
    QTableView* parameterTable_ = nullptr;

    /// Function View 上的文字。由本控件通过 Qt 父子关系拥有。功能页显示时隐藏。
    QLabel* functionLabel_ = nullptr;

    /// 参数表搜索框。由参数页通过 Qt 父子关系拥有。
    QLineEdit* parameterSearch_ = nullptr;

    /// 参数表的筛选代理。由参数表通过 Qt 父子关系拥有。空表示还没有参数页。
    ParameterFilterProxy* parameterProxy_ = nullptr;

    /// 参数 id 写入成功后的通知。空表示没有观察者。不拥有检查器。
    std::function<void(std::string_view)> parameterObserver_;

    /// 设备控制器。不拥有。
    DeviceController* deviceController_{nullptr};
    ernc::domain::ArrayRuntimeStore* arrayStore_{nullptr};
    ArrayDetailPanel* arrayDetail_{nullptr};
};

} // namespace ernc::presentation
