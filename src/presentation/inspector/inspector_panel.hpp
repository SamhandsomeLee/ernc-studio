#pragma once

#include <QWidget>

#include <string>
#include <string_view>

class QLabel;

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
    /// @note 只在 UI 主线程调用。不写设备，也不暂存 Set。
    void showParameter(std::string_view parameterId);

private:
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
};

} // namespace ernc::presentation
