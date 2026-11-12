#pragma once

#include <QAbstractButton>
#include <QPoint>
#include <QRect>
#include <QString>

class QPainter;

namespace ernc::presentation {

/// 参数表与函数视图共用的滑动开关列宽（逻辑像素）。
inline constexpr int kToggleTrackWidth = 44;
inline constexpr int kToggleTrackHeight = 22;

/// 目录 Toggle 的滑动开关。开/关两态；未知时滑块居中，不可当作关。
/// 交互对齐旧版 `BrAlgoOffSwitchPanel`：点击滑到开或关，不显示下拉框。
class ParameterToggleSwitch final : public QAbstractButton {
    Q_OBJECT

public:
    enum class VisualState {
        Unknown,
        On,
        Off,
    };

    explicit ParameterToggleSwitch(QWidget* parent = nullptr);

    /// 语义选项数。与旧下拉「开」「关」两项一致，供测试读取。
    int semanticOptionCount() const;

    /// @param index 0 为「开」，1 为「关」。越界返回空。
    QString semanticOptionLabel(int index) const;

    /// 按显示文字设置滑块。接受「开」「关」「未知」/「Unknown」。
    void applyCaption(const QString& caption);

    /// 当前可视化状态。
    VisualState visualState() const;

    /// 用户可操作时的语义：开或关。未知时返回空。
    QString currentWord() const;

    /// 为 true 时忽略 toggled，供页面 reload 使用。
    void setSuppressSignals(bool suppress);

signals:
    /// 用户把滑块拨到开或关。reload 抑制期间不发射。
    void userChose(const QString& word);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void commitFromPointer(const QPoint& pos);
    void setVisualState(VisualState state, bool notify);

    VisualState state_{VisualState::Unknown};
    bool suppressSignals_{false};
};

ParameterToggleSwitch::VisualState visualStateFromCaption(const QString& caption);

void paintParameterToggleSwitch(QPainter& painter, const QRect& cellRect, ParameterToggleSwitch::VisualState state,
                                bool focused);

QRect toggleTrackRectInCell(const QRect& cellRect);

bool toggleChoosesOnFromCellClick(const QRect& cellRect, const QPoint& positionInCell);

}  // namespace ernc::presentation
