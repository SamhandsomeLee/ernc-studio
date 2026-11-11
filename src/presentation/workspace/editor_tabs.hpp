#pragma once

#include "workspace/editor_selection.hpp"

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QModelIndex>
#include <QTabWidget>

#include <functional>
#include <string_view>

class QLabel;
class QLineEdit;

namespace ernc::presentation {
class ParameterFilterProxy;
}

namespace ernc::presentation {

/// Function View 和 Parameter View。两个页面读取同一份参数 id。
/// 切换页签不修改选择，也不连接设备。
class EditorTabs final : public QTabWidget {
public:
    /// @brief 创建两个工作页，初始没有选中的页面或参数。
    /// @param catalog 参数目录，只借用。空指针时参数表没有行。调用方保证它比本控件活得久。
    /// @param store 运行状态，只借用。空指针时参数表不显示数值。调用方保证它比本控件活得久。
    /// @param layout 固件布局，只借用。空指针时 Set 列确认不会暂存。调用方保证它比本控件活得久。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本控件。必须在 UI 主线程创建。
    /// @note 启动时停在 Function View。预置可以切换页签，窗口状态不单独保存这个页签。
    explicit EditorTabs(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                        const ernc::domain::FirmwareLayout* layout, QWidget* parent = nullptr);

    /// @brief 切到 Function View。
    /// @note 不改参数 id，也不读取设备。只在 UI 主线程调用。
    void showFunctionView();

    /// @brief 切到 Parameter View。
    /// @note 不改参数 id，也不把焦点送进参数表。只在 UI 主线程调用。
    void showParameterView();

    /// @brief 切到 Parameter View，并把焦点送进搜索框。
    /// @note 不改搜索文字，不改状态栏。只在 UI 主线程调用。
    void focusParameterTable();

    /// @brief 把焦点交回当前工作页。
    /// @note 不切换页签，不读取设备，不改状态栏。只在 UI 主线程调用。
    void focusCurrentPage();

    /// @brief 把参数 id 写进两页共用的选择。
    /// @param parameterId 目录主键。空文本不改变已有选择。调用方拥有原件，本函数只复制它。
    void selectParameter(const QString& parameterId);

    /// @brief 更新当前页面，保留已经选中的参数 id。
    /// @param pageId 项目树页面 id。空文本不改变已有选择。调用方拥有原件，本函数只复制它。
    /// @note 不通知参数观察者。检查器只跟参数 id 走。
    void selectPage(const QString& pageId);

    /// @brief 登记参数 id 变化后的通知。
    /// @param observer 观察者。空函数表示不再通知。本控件复制这份函数。只在 UI 主线程调用。
    /// @note 通知参数是内部 id 的视图，只在这次调用期间有效。观察者不得再调用 `selectParameter`。
    void setParameterObserver(std::function<void(std::string_view)> observer);

private:
    /// @brief 按当前选择重写 Function View 上的文字。参数表不在这里改写。
    void refresh();

    /// @brief 按对象名切到一个工作页。
    /// @param objectName 页面对象名。调用方拥有原件，本函数只比较它。找不到时不改当前页。
    void showNamedPage(const QString& objectName);

    /// @brief 参数表换行时，把该行参数 id 写入共用选择。
    /// @param current 新的当前行。无效时不改选择。本函数不清除页面 id。
    /// @param previous 换行前的当前行。本函数不读取它。
    /// @note 只在 UI 主线程由表格选择模型调用。不写设备，也不暂存 Set。
    void onParameterRowChanged(const QModelIndex& current, const QModelIndex& previous);

    /// 两页共用的页面 id 和参数 id。页签切换不会改它。
    EditorSelection selection_;

    /// Function View 上的文字。由本控件通过 Qt 父子关系拥有。
    QLabel* functionLabel_ = nullptr;

    /// 参数表搜索框。由参数页通过 Qt 父子关系拥有。
    QLineEdit* parameterSearch_ = nullptr;

    /// 参数表的筛选代理。由参数表通过 Qt 父子关系拥有。空表示还没有参数页。
    ParameterFilterProxy* parameterProxy_ = nullptr;

    /// 参数 id 写入成功后的通知。空表示没有观察者。不拥有检查器。
    std::function<void(std::string_view)> parameterObserver_;
};

} // namespace ernc::presentation
