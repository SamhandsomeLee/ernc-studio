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
class QVBoxLayout;

namespace ernc::presentation {

/// Threshold 分类页。五个导航只汇总目录参数，不计算未确认公式。
/// 多槽参数只作为数组入口。必须在 UI 主线程创建和调用。
class ThresholdPage final : public QWidget {
public:
    /// @brief 按 Threshold 分类生成五个导航和其余摘要。
    /// @param catalog 参数目录，只借用。空指针时没有行。调用方保证它比本页活得久。
    /// @param store 运行状态，只借用。空指针时不显示数值。调用方保证它比本页活得久。
    /// @param layout 固件布局，只借用。空指针时没有数字框。调用方保证它比本页活得久。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本页。必须在 UI 主线程创建。
    /// @note 构造不创建计算按钮，也不发送 FC16。
    explicit ThresholdPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                           const ernc::domain::FirmwareLayout* layout, QWidget* parent = nullptr);

    /// @brief 按 Store 重画 1 slot 行的当前值。
    /// @note 不暂存，不发送，也不改数组行。只在 UI 主线程调用。
    void reload();

    /// @brief 登记暂存成功后的通知。
    /// @param observer 观察者。空函数表示不再通知。本页复制这份函数。参数标识只在这次调用期间有效。
    /// @note 通知发生在 stage 成功之后。只在 UI 主线程调用。
    void setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer);

    /// @brief 登记点击一行后的通知。
    /// @param observer 观察者。空函数表示不再通知。本页复制这份函数。参数标识只在这次调用期间有效。
    /// @note 通知的 ID 与被点行的目录 ID 相同。只在 UI 主线程调用。
    void setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer);

    /// @brief 这一页是否有该目录 ID。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 已生成行时为 true。
    bool containsParameter(const QString& parameterId) const;

    /// @brief 读取一行所在的导航名。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return ENC Echo、ENC Divergence、Horn、RNC Divergence、IO Threshold 或其他阈值。未知 id 为空。
    QString groupOf(const QString& parameterId) const;

    /// @brief 读取一行当前值或数组标记。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 1 slot 行是十进制原值。数组行为「数组」。未知 id 为空。
    QString valueText(const QString& parameterId) const;

    /// @brief 这一行是否有数字框。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return locate 得到 1 个标量 slot 时为 true。
    bool hasEditor(const QString& parameterId) const;

    /// @brief 页面上是否有自动计算或按公式写入的按钮。
    /// @return 本页固定为 false。
    bool hasFormulaButton() const;

    /// @brief 确认一个 1 slot 原值。
    /// @param parameterId 目录主键。数组行会被忽略。调用方拥有原件。
    /// @param text 十进制文本。调用方拥有原件。
    /// @note 不从秒或公式反算。不发送 FC16。只在 UI 主线程调用。
    void commitValue(const QString& parameterId, const QString& text);

    /// @brief 点中一行，选中它的目录 ID。
    /// @param parameterId 目录主键。未知 id 不改变当前选择。调用方拥有原件。
    /// @note 不暂存，也不发送。只在 UI 主线程调用。
    void activate(const QString& parameterId);

    /// @brief 最近一次点中的目录 ID。
    /// @return 还没有点过时为空字符串。
    QString selectedParameterId() const;

private:
    /// 一行阈值参数。指针由本页通过 Qt 父子关系拥有。
    struct Control {
        /// 目录主键。
        QString id;

        /// 导航名。不是公式名。
        QString group;

        /// 当前值，或数组行的「数组」。
        QLabel* value = nullptr;

        /// 1 slot 输入。数组行为空。
        QLineEdit* editor = nullptr;
    };

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

    /// 固件布局。本页不拥有。
    const ernc::domain::FirmwareLayout* layout_ = nullptr;

    /// 暂存入口。目录、布局和 Store 都存在时才创建。本页拥有。
    std::optional<ernc::application::ParameterStager> stager_;

    /// 已生成的行，顺序与目录文件一致。
    std::vector<Control> controls_;

    /// 暂存成功后的通知。空表示没有观察者。
    std::function<void(const ernc::domain::ParameterId&)> editObserver_;

    /// 点击一行后的通知。空表示没有观察者。
    std::function<void(const ernc::domain::ParameterId&)> selectionObserver_;

    /// 最近一次点中的目录 ID。空表示还没有点过。
    QString selectedId_;

    /// 为 true 时，重画输入框不会再次暂存。
    bool reloading_{false};
};

}  // namespace ernc::presentation
