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

/// ENC 阶次页。阶次 1、2、3 共用同一张卡片，数组只作为入口。
/// 不显示阶次 4、5 的未确认开关，也不连接设备。必须在 UI 主线程创建和调用。
class EncParamPage final : public QWidget {
public:
    /// @brief 按目录生成三张阶次卡片和卡片外的 1 slot 参数。
    /// @param catalog 参数目录，只借用。空指针时没有行。调用方保证它比本页活得久。
    /// @param store 运行状态，只借用。空指针时不显示数值，确认也不暂存。调用方保证它比本页活得久。
    /// @param layout 固件布局，只借用。空指针时没有数字框。调用方保证它比本页活得久。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本页。必须在 UI 主线程创建。
    /// @note 构造不创建会话，也不发送 FC16。阶次 4、5 不生成控件。
    explicit EncParamPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                          const ernc::domain::FirmwareLayout* layout, QWidget* parent = nullptr);

    /// @brief 按 Store 重画 1 slot 行的当前值。
    /// @note 不暂存，不发送，也不改数组行的文字。只在 UI 主线程调用。
    void reload();

    /// @brief 登记暂存成功后的通知。
    /// @param observer 观察者。空函数表示不再通知。本页复制这份函数。参数标识只在这次调用期间有效。
    /// @note 通知发生在 stage 成功之后。只在 UI 主线程调用。
    void setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer);

    /// @brief 登记点击一行后的通知。
    /// @param observer 观察者。空函数表示不再通知。本页复制这份函数。参数标识只在这次调用期间有效。
    /// @note 通知的 ID 与被点行的目录 ID 相同。只在 UI 主线程调用。
    void setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer);

    /// @brief 已生成的阶次卡片数。
    /// @return 目录里有对应开闭标志时计入。最多 3，不含阶次 4、5。
    int orderCardCount() const;

    /// @brief 这一页是否有该目录 ID。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 已经生成行时为 true。阶次 4、5 和噪声类型为 false。
    bool containsParameter(const QString& parameterId) const;

    /// @brief 读取一行当前值或数组标记。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 1 slot 行是十进制原值，没有值时为「—」。数组行固定为「数组」。未知 id 为空字符串。
    QString valueText(const QString& parameterId) const;

    /// @brief 读取目录说明，不含参数 ID。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 说明原文。没有说明时为空。未知 id 为空字符串。
    /// @brief 目录说明原文。供测试读取；界面说明在检查器属性中。
    QString detailText(const QString& parameterId) const;

    /// @brief 这一行是否有数字框。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return locate 得到 1 个标量 slot 时为 true。数组行和未知 id 为 false。
    bool hasEditor(const QString& parameterId) const;

    /// @brief 确认一个 1 slot 的 float 原值。
    /// @param parameterId 目录主键。数组行会被忽略。调用方拥有原件。
    /// @param text 十进制文本。调用方拥有原件。空文本和「开」不暂存。
    /// @note 不把 1 编成开。不发送 FC16。失败时 Store 不变。只在 UI 主线程调用。
    void commitValue(const QString& parameterId, const QString& text);

    /// @brief 点中一行，选中它的目录 ID。
    /// @param parameterId 目录主键。未知 id 不改变当前选择。调用方拥有原件。
    /// @note 不暂存，也不发送。只在 UI 主线程调用。
    void activate(const QString& parameterId);

    /// @brief 最近一次点中的目录 ID。
    /// @return 还没有点过时为空字符串。
    QString selectedParameterId() const;

private:
    /// 一行阶次参数。指针由本页通过 Qt 父子关系拥有。
    struct Control {
        /// 目录主键。
        QString id;

        /// 1 slot 的当前值，或数组行的「数组」。
        QLabel* value = nullptr;

        /// 1 slot 输入。数组行为空。
        QLineEdit* editor = nullptr;
    };

    /// @brief 增加一行。目录里没有这个 ID 时跳过。
    /// @param parent 放置该行的控件。通过 Qt 父子关系拥有这一行。
    /// @param layout 行要加入的纵向布局。只在本次调用期间借用。
    /// @param parameterId 目录主键。调用方拥有原件，本函数复制它。
    void addRow(QWidget* parent, QVBoxLayout* layout, const QString& parameterId);

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

    /// 已生成的阶次卡片数。不含阶次 4、5。
    int cardCount_{0};

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
