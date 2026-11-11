#pragma once

#include <QWidget>

#include <functional>
#include <string>
#include <string_view>

class QLabel;
class QTabWidget;

namespace ernc::domain {
class ParameterCatalog;
}

namespace ernc::presentation {

/// 底部检查器。三个标签显示同一次选择的参数 id。
/// 本控件只保存 id，不保存 Read、Set 或设备状态，也不连接串口。
class InspectorPanel final : public QWidget {
public:
    /// @brief 创建三个空标签。初始显示「未选择参数」。
    /// @param catalog 参数目录。空指针表示目录不可用。本控件不拥有它，调用方须保证它比本控件活得更久。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本控件。必须在 UI 主线程创建。
    explicit InspectorPanel(const ernc::domain::ParameterCatalog* catalog, QWidget* parent = nullptr);

    /// @brief 换成另一个参数 id，并按目录和假数据表重写三个标签。
    /// @param parameterId 目录主键。空文本表示尚未选择。只在本次调用期间借用，本控件会复制它。
    /// @note 只在 UI 主线程调用。不写设备，也不暂存 Set。不改动「操作中心」按钮。
    void showParameter(std::string_view parameterId);

    /// @brief 登记 Diagnostics 里「操作中心」按钮的动作。
    /// @param opener 按钮按下时调用的函数。空函数表示按下后不做任何事。本控件复制这份函数。
    /// @note 只在 UI 主线程调用。这个按钮不改参数 id，也不写设备。
    void setOperationCenterOpener(std::function<void()> opener);

    /// @brief 切到 Properties。
    /// @note 不改参数 id，也不写设备。只在 UI 主线程调用。
    void showPropertiesPage();

    /// @brief 切到 Diagnostics。
    /// @note 不改参数 id，也不写设备。只在 UI 主线程调用。
    void showDiagnosticsPage();

private:
    /// @brief 按对象名选中一个检查器标签。
    /// @param objectName 页面对象名。调用方拥有这段字面量。找不到时不改当前标签。
    void showNamedPage(const char* objectName);
    /// 借用的目录。空指针表示没有可查询的定义。
    const ernc::domain::ParameterCatalog* catalog_ = nullptr;

    /// 当前参数 id。空字符串表示未选择。这里不保存该参数的值。
    std::string parameterId_;

    /// Properties 正文。由本控件通过 Qt 父子关系拥有。
    QLabel* propertiesLabel_ = nullptr;

    /// Info 正文。由本控件通过 Qt 父子关系拥有。
    QLabel* infoLabel_ = nullptr;

    /// Diagnostics 正文。由本控件通过 Qt 父子关系拥有。
    QLabel* diagnosticsLabel_ = nullptr;

    /// Diagnostics 上「操作中心」按钮按下时调用。空表示没有动作。不拥有操作中心。
    std::function<void()> operationCenterOpener_;

    /// 三个标签的页签控件。由本控件通过 Qt 父子关系拥有。
    QTabWidget* tabs_ = nullptr;
};

} // namespace ernc::presentation
