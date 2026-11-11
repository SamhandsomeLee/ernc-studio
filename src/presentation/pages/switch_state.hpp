#pragma once

#include <QString>
#include <QWidget>

namespace ernc::presentation {

/// ERNC_Switch_State 只读页。三行只表示数组下标，不表示 ANC、ENC 或 RNC。
/// 不读取数组元素，也不连接设备。必须在 UI 主线程创建和调用。
class SwitchStatePage final : public QWidget {
public:
    /// @brief 生成 State0、State1、State2 和更新时刻。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本页。必须在 UI 主线程创建。
    /// @note 数值固定为未读取。不创造新的参数 ID，也不写 Store。
    explicit SwitchStatePage(QWidget* parent = nullptr);

    /// @brief 某一行的状态标题。
    /// @param index 0、1 或 2。越界时为空字符串。
    /// @return State0、State1 或 State2。不含 ANC、ENC、RNC。
    QString stateLabel(int index) const;

    /// @brief 某一行显示的原始状态。
    /// @param index 0、1 或 2。越界时为空字符串。
    /// @return 当前固定为「未读取」。这不是设备状态码。
    QString stateValue(int index) const;

    /// @brief 更新时刻。
    /// @return 没有整段数组读数时为「—」。不是虚构时刻。
    QString updatedAtText() const;

private:
    /// 三行状态文字。由本页通过 Qt 父子关系拥有。
    QString labels_[3];
};

}  // namespace ernc::presentation
