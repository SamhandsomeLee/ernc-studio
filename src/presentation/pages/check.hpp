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

/// Check 页。两个模式必须确认后才暂存，区域和 CAN 只作为入口。
/// 不解码 check_data，也不连接设备。必须在 UI 主线程创建和调用。
class CheckPage final : public QWidget {
public:
    /// @brief 生成检测模式、区域入口和 CAN 入口。
    /// @param catalog 参数目录，只借用。空指针时没有参数行。调用方保证它比本页活得久。
    /// @param store 运行状态，只借用。空指针时确认不暂存。调用方保证它比本页活得久。
    /// @param layout 固件布局，只借用。空指针时没有数字框。调用方保证它比本页活得久。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本页。必须在 UI 主线程创建。
    /// @note 构造不创建会话，也不发送 FC16。check_data 的子字段不生成控件。
    explicit CheckPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                       const ernc::domain::FirmwareLayout* layout, QWidget* parent = nullptr);

    /// @brief 按 Store 重画两个模式的当前值。
    /// @note 不把输入框里尚未确认的文字写进 Store。只在 UI 主线程调用。
    void reload();

    /// @brief 登记暂存成功后的通知。
    /// @param observer 观察者。空函数表示不再通知。本页复制这份函数。参数标识只在这次调用期间有效。
    /// @note 只有确认成功才通知。只在 UI 主线程调用。
    void setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer);

    /// @brief 登记点击一行后的通知。
    /// @param observer 观察者。空函数表示不再通知。本页复制这份函数。参数标识只在这次调用期间有效。
    /// @note 通知的 ID 与被点行的目录 ID 相同。只在 UI 主线程调用。
    void setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer);

    /// @brief 登记 CAN 名称目录的打开动作。
    /// @param opener 打开者。空函数表示按钮不再打开目录。本页复制这份函数。
    /// @note 不读取 Can_Msg。只在 UI 主线程调用。
    void setCanOpener(std::function<void()> opener);

    /// @brief 这一页是否有该目录 ID 的行。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 已生成行时为 true。子检测字段为 false。
    bool containsParameter(const QString& parameterId) const;

    /// @brief 读取一行当前值或数组标记。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 模式行是十进制原值，没有值时为「—」。区域行为「数组」。未知 id 为空。
    QString valueText(const QString& parameterId) const;

    /// @brief 这一行是否有可编辑的模式输入。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return check_mod 和 TranFuncFlag 为 true。区域行和未知 id 为 false。
    bool hasEditor(const QString& parameterId) const;

    /// @brief 改输入框，但不暂存。
    /// @param parameterId 目录主键。区域行会被忽略。调用方拥有原件。
    /// @param text 尚未确认的文本。调用方拥有原件。
    /// @note 不调用 stage。只在 UI 主线程调用。
    void setDraft(const QString& parameterId, const QString& text);

    /// @brief 确认一个模式的当前输入。
    /// @param parameterId 目录主键。只接受两个模式。调用方拥有原件。
    /// @note 把输入框里的 float 原值暂存。空文本或非数字不暂存，也不发送 FC16。只在 UI 主线程调用。
    void confirm(const QString& parameterId);

    /// @brief 点中一行，选中它的目录 ID。
    /// @param parameterId 目录主键。未知 id 不改变当前选择。调用方拥有原件。
    /// @note 不暂存，也不发送。只在 UI 主线程调用。
    void activate(const QString& parameterId);

    /// @brief 打开 CAN 名称目录。
    /// @note 只调用已登记的打开者。没有打开者时什么都不做，也不读 Can_Msg。
    void openCanCatalog();

    /// @brief 最近一次点中的目录 ID。
    /// @return 还没有点过时为空字符串。
    QString selectedParameterId() const;

private:
    /// 一行检测参数。指针由本页通过 Qt 父子关系拥有。
    struct Control {
        /// 目录主键。
        QString id;

        /// 当前值，或区域行的「数组」。
        QLabel* value = nullptr;

        /// 模式输入。区域行为空。改字本身不暂存。
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

    /// 固件布局。本页不拥有。
    const ernc::domain::FirmwareLayout* layout_ = nullptr;

    /// 暂存入口。目录、布局和 Store 都存在时才创建。本页拥有。
    std::optional<ernc::application::ParameterStager> stager_;

    /// 已生成的行。
    std::vector<Control> controls_;

    /// 暂存成功后的通知。空表示没有观察者。
    std::function<void(const ernc::domain::ParameterId&)> editObserver_;

    /// 点击一行后的通知。空表示没有观察者。
    std::function<void(const ernc::domain::ParameterId&)> selectionObserver_;

    /// CAN 目录打开者。空表示按钮不切换页面。
    std::function<void()> canOpener_;

    /// 最近一次点中的目录 ID。空表示还没有点过。
    QString selectedId_;

    /// 为 true 时，重画输入框不会把旧值当成一次新确认。
    bool reloading_{false};
};

}  // namespace ernc::presentation
