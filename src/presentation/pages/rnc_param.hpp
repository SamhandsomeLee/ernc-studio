#pragma once

#include "ernc/application/stage_value.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QString>
#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

class QLabel;
class QLineEdit;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace ernc::presentation {

/// RNC 参数页。Step 只显示，时间和 alpha 只暂存固件原值，Leaky 与 SpecRadius 只作入口。
/// SpecRadius 读写注明在参数视图。不计算参考秒数，也不连接设备。必须在 UI 主线程创建和调用。
class RncParamPage final : public QWidget {
public:
    /// @brief 按本页固定名单生成步长、alpha、频率、Leaky 和时间。
    /// @param catalog 参数目录，只借用。空指针时没有行。调用方保证它比本页活得久。
    /// @param store 运行状态，只借用。空指针时不显示数值，确认也不暂存。调用方保证它比本页活得久。
    /// @param layout 固件布局，只借用。空指针时没有数字框。调用方保证它比本页活得久。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本页。必须在 UI 主线程创建。
    /// @note 构造不创建会话，也不发送 FC16。不生成通道数量行。
    explicit RncParamPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                          const ernc::domain::FirmwareLayout* layout, QWidget* parent = nullptr);

    /// @brief 按 Store 重画可显示数值的行。
    /// @note 不暂存，不发送，也不改数组行的文字。只在 UI 主线程调用。
    void reload();

    /// @brief 登记暂存成功后的通知。
    /// @param observer 观察者。空函数表示不再通知。本页复制这份函数。参数标识只在这次调用期间有效。
    /// @note 通知发生在 stage 成功之后。Step 不会触发它。只在 UI 主线程调用。
    void setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer);

    /// @brief 登记点击一行后的通知。
    /// @param observer 观察者。空函数表示不再通知。本页复制这份函数。参数标识只在这次调用期间有效。
    /// @note 通知的 ID 与被点行的目录 ID 相同。只在 UI 主线程调用。
    void setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer);

    /// @brief 这一页是否有该目录 ID。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 已经生成行时为 true。
    bool containsParameter(const QString& parameterId) const;

    /// @brief 读取一行 Read 或数组标记。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 标量行是十进制原值，没有值时为「—」。数组行固定为「数组」。未知 id 为空字符串。
    QString valueText(const QString& parameterId) const;

    /// @brief 目录说明原文。供测试读取；界面说明在检查器属性中。
    QString detailText(const QString& parameterId) const;

    /// @brief 参数视图读写提示。无提示行时为空。
    QString parameterViewHintText(const QString& parameterId) const;

    /// @brief 这一行是否有数字框。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 可写且 locate 得到 1 个标量 slot 时为 true。Step、数组和未知 id 为 false。
    bool hasEditor(const QString& parameterId) const;

    /// @brief 确认一个可写 1 slot 的原值。
    /// @param parameterId 目录主键。Step 和数组行会被忽略。调用方拥有原件。
    /// @param text 十进制文本。调用方拥有原件。空文本不暂存。
    /// @note float 与 uint32 按目录类型暂存。时间不从秒反算。不发送 FC16。只在 UI 主线程调用。
    void commitValue(const QString& parameterId, const QString& text);

    /// @brief 点中一行，选中它的目录 ID。
    /// @param parameterId 目录主键。未知 id 不改变当前选择。调用方拥有原件。
    /// @note 不暂存，也不发送。只在 UI 主线程调用。
    void activate(const QString& parameterId);

    /// @brief 滚动到参数行并高亮。不通知共用选择观察者。
    void emphasizeParameter(const QString& parameterId);

    /// @brief 该行是否处于高亮态。供测试读取。
    bool isParameterEmphasized(const QString& parameterId) const;

    /// @brief 最近一次点中的目录 ID。
    /// @return 还没有点过时为空字符串。
    QString selectedParameterId() const;

private:
    /// 一行 RNC 参数。指针由本页通过 Qt 父子关系拥有。
    struct Control {
        /// 目录主键。
        QString id;

        /// 设备 Read（可以是工程历史值），或数组行的「数组」。
        QLabel* value = nullptr;

        /// 可写 1 slot 的输入。Step 和数组行为空。
        QLineEdit* editor = nullptr;

        /// 读写改在参数视图的提示。仅 SpecRadius 等入口行使用。
        QLabel* parameterViewHint = nullptr;

        /// 整行容器。用于滚动与高亮。
        QWidget* row = nullptr;
    };

    /// @brief 增加一个分区标题和它的参数行。
    /// @param parent 放置控件的页面内容。通过 Qt 父子关系拥有新控件。
    /// @param layout 行要加入的纵向布局。只在本次调用期间借用。
    /// @param title 分区名。调用方拥有原件。它不是参数 ID。
    /// @param ids 该分区的目录主键。只在本次调用期间借用。
    /// @param count ids 的元素个数。
    void addSection(QWidget* parent, QVBoxLayout* layout, const QString& title, const char* const* ids, int count);

    /// @brief 增加一行。目录里没有这个 ID 时跳过。
    /// @param parent 放置该行的控件。通过 Qt 父子关系拥有这一行。
    /// @param layout 行要加入的纵向布局。只在本次调用期间借用。
    /// @param parameterId 目录主键。调用方拥有原件，本函数复制它。
    void addRow(QWidget* parent, QVBoxLayout* layout, const QString& parameterId);

    /// @brief 仅展示入口；标「数组」并提示读写在参数视图。
    void addParameterViewOnlyRow(QWidget* parent, QVBoxLayout* layout, const QString& parameterId);

    /// @brief 按主键查找一行。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 找到时指向内部行。没有时为空指针。
    Control* findControl(const QString& parameterId);

    /// @brief 按主键查找一行。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 找到时指向内部行。没有时为空指针。
    const Control* findControl(const QString& parameterId) const;

    /// @brief 暂存成功后通知表格，并重画本页。
    /// @param id 刚暂存的参数。只在本次调用期间借用。
    void notifyEdited(const ernc::domain::ParameterId& id);

    /// 参数目录。本页不拥有。
    const ernc::domain::ParameterCatalog* catalog_ = nullptr;

    /// 运行状态。本页不拥有。
    ernc::domain::ParameterRuntimeStore* store_ = nullptr;

    /// 固件布局。本页不拥有。空指针时不能判断 1 slot。
    const ernc::domain::FirmwareLayout* layout_ = nullptr;

    /// 暂存入口。目录、布局和 Store 都存在时才创建。本页拥有。
    std::optional<ernc::application::ParameterStager> stager_;

    /// 已经生成的行。本页拥有这些记录，控件由 Qt 父子关系拥有。
    std::vector<Control> controls_;

    /// 暂存成功后的通知。空表示没有观察者。
    std::function<void(const ernc::domain::ParameterId&)> editObserver_;

    /// 参数列表滚动区。由本页拥有。
    QScrollArea* scroll_ = nullptr;

    /// 当前高亮行。不拥有；失效前由 clear 去掉样式。
    QWidget* emphasizedRow_ = nullptr;

    /// 点击一行后的通知。空表示没有观察者。
    std::function<void(const ernc::domain::ParameterId&)> selectionObserver_;

    /// 最近一次点中的目录 ID。空表示还没有点过。
    QString selectedId_;

    /// 为 true 时，重画输入框不会再次暂存。
    bool reloading_{false};
};

}  // namespace ernc::presentation
