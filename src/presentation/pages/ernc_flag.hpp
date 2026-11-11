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

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace ernc::presentation {

/// ERNC_Flag 功能页。三个反向开关和两个原值参数只暂存，不连接设备。
/// RNC_refresh 只显示，执行控件保持不可用。必须在 UI 主线程创建和调用。
class ErncFlagPage final : public QWidget {
public:
    /// @brief 按目录生成旗标页。
    /// @param catalog 参数目录，只借用。空指针时没有控件。调用方保证它比本页活得久。
    /// @param store 运行状态，只借用。空指针时不显示数值，确认也不暂存。调用方保证它比本页活得久。
    /// @param layout 固件布局，只借用。空指针时确认不暂存。调用方保证它比本页活得久。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本页。必须在 UI 主线程创建。
    /// @note 构造不创建会话，不发送 FC16，也不读取车速。
    explicit ErncFlagPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                          const ernc::domain::FirmwareLayout* layout, QWidget* parent = nullptr);

    /// @brief 按 Store 重画当前值。
    /// @note 不暂存，不发送。只在 UI 主线程调用。
    void reload();

    /// @brief 登记暂存成功后的通知。
    /// @param observer 观察者。空函数表示不再通知。本页复制这份函数。参数标识只在这次调用期间有效。
    /// @note 通知发生在 stage 成功之后。不在通知里再改本页的控件。只在 UI 主线程调用。
    void setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer);

    /// @brief 读取一行当前显示的值。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 开关为开、关或 Unknown。uint32 和 RNC_refresh 为十进制原值。没有值时开关为 Unknown，数值为「—」。未知 id 为空字符串。
    QString valueText(const QString& parameterId) const;

    /// @brief 这一行是不是开/关选择。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 三个反向开关为 true。Speaker、Bypass 和 RNC_refresh 为 false。
    bool hasToggle(const QString& parameterId) const;

    /// @brief 开/关选择的项数。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @return 有选择时为 2。没有选择时为 0。
    int toggleCount(const QString& parameterId) const;

    /// @brief 读取开/关选择的一项。
    /// @param parameterId 目录主键。调用方拥有原件，本函数只比较它。
    /// @param index 从 0 开始。越界时返回空字符串。
    /// @return 第 0 项为「开」，第 1 项为「关」。
    QString toggleItem(const QString& parameterId, int index) const;

    /// @brief 确认一个反向开关。
    /// @param parameterId 目录主键。只接受三个 toggle。调用方拥有原件。
    /// @param word 「开」或「关」。其它文字不暂存。
    /// @note 开暂存线值 0，关暂存线值 1。不发送 FC16。失败时 Store 不变。只在 UI 主线程调用。
    void chooseToggle(const QString& parameterId, const QString& word);

    /// @brief 确认一个 uint32 原值。
    /// @param parameterId 目录主键。只接受 SpeakerCheckValue 和 Bypass_mod。调用方拥有原件。
    /// @param text 十进制文本。调用方拥有原件。「开」和空文本不暂存。
    /// @note 种类必须是 uint32。不把 0 或 1 解释成开关。不发送 FC16。只在 UI 主线程调用。
    void commitRaw(const QString& parameterId, const QString& text);

    /// @brief RNC_refresh 的执行控件能否点击。
    /// @return 本页存在该控件时为 false。车速映射未确认，不能执行。
    bool refreshEnabled() const;

    /// @brief 按下 RNC_refresh。
    /// @note 不暂存，不写 1，不写 0，不读车速。控件不可用时调用也保持 Store 不变。
    void pressRefresh();

private:
    /// 一行旗标控件。指针由本页通过 Qt 父子关系拥有。
    struct Control {
        /// 目录主键。
        QString id;

        /// 当前值。开关显示开、关或 Unknown。数值显示十进制。
        QLabel* value = nullptr;

        /// 开/关选择。数值行和 RNC_refresh 为空。
        QComboBox* toggle = nullptr;

        /// uint32 输入。开关和 RNC_refresh 为空。
        QLineEdit* raw = nullptr;

        /// RNC_refresh 的执行按钮。其它行为空。始终不可用。
        QPushButton* refresh = nullptr;
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

    /// 暂存入口。目录、布局和 Store 都存在时才创建。本页拥有。
    std::optional<ernc::application::ParameterStager> stager_;

    /// 六行控件，顺序与本页固定名单一致。
    std::vector<Control> controls_;

    /// 暂存成功后的通知。空表示没有观察者。
    std::function<void(const ernc::domain::ParameterId&)> editObserver_;

    /// 为 true 时，重画选择框不会再次暂存。
    bool reloading_{false};
};

}  // namespace ernc::presentation
