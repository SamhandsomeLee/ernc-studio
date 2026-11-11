#pragma once

#include "theme/theme_tokens.hpp"

#include <QColor>
#include <QWidget>

class QEvent;
class QPaintEvent;

namespace ernc::presentation {

/// 同时画出图标、文字和辅助说明的状态控件。颜色来自浅色令牌，但文字和图标不依赖颜色也能区分。
/// 它只显示 `StatusPresentation` 里的固定说法，不读取设备，也不保存参数值。
class StatusBadge final : public QWidget {
public:
    /// @brief 按一种设计状态创建控件。
    /// @param kind 第 17.9 节里的状态。调用方只传入该枚举的取值。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本控件；空时由调用方负责销毁。必须在 UI 主线程创建。
    /// @note `kind` 没有对应说法时，控件显示“状态未定义”，不用 Off 代替。
    explicit StatusBadge(StatusKind kind, QWidget* parent = nullptr);

    /// @brief 按当前字体计算包含图标和全文的尺寸。
    /// @return 逻辑像素尺寸。宽度随文字变长，不截断状态文本。
    QSize sizeHint() const override;

    /// @brief 与 `sizeHint()` 相同，避免布局把状态文字压窄。
    /// @return 逻辑像素尺寸。
    QSize minimumSizeHint() const override;

protected:
    /// @brief 绘制样式表背景、状态图标和状态文字。
    /// @param event Qt 给出的重绘区域，只在本次调用期间借用。本控件不根据它做局部失效以外的判断。
    void paintEvent(QPaintEvent* event) override;

    /// @brief 字体或样式变化后重新计算尺寸。
    /// @param event Qt 事件，只在本次调用期间借用。只处理字体和样式变化，其余交给基类。
    void changeEvent(QEvent* event) override;

private:
    /// 界面上的状态文字。拥有独立副本，不借用目录里的字符串。
    QString text_;

    /// 图标和文字使用的令牌颜色。无效时改用主文本色，仍保留文字和图标。
    QColor color_;

    /// 与 `color_` 配套的形状。相同颜色的不同状态不会共用一个形状。
    StatusIconShape icon_ = StatusIconShape::Question;
};

} // namespace ernc::presentation
