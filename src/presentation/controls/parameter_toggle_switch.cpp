#include "controls/parameter_toggle_switch.hpp"

#include "pages/function_view_layout.hpp"
#include "theme/theme_tokens.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

namespace ernc::presentation {
namespace {

constexpr int kThumbDiameter = 18;
constexpr int kThumbMargin = 2;

QString normalizeUnknownCaption(const QString& caption) {
    if (caption == QStringLiteral("Unknown") || caption == QStringLiteral("未知")) {
        return QStringLiteral("未知");
    }
    return caption;
}

}  // namespace

ParameterToggleSwitch::VisualState visualStateFromCaption(const QString& caption) {
    const QString normalized = normalizeUnknownCaption(caption);
    if (normalized == QStringLiteral("开")) {
        return ParameterToggleSwitch::VisualState::On;
    }
    if (normalized == QStringLiteral("关")) {
        return ParameterToggleSwitch::VisualState::Off;
    }
    return ParameterToggleSwitch::VisualState::Unknown;
}

QRect toggleTrackRectInCell(const QRect& cellRect) {
    const int left = cellRect.left() + (cellRect.width() - kToggleTrackWidth) / 2;
    const int top = cellRect.top() + (cellRect.height() - kToggleTrackHeight) / 2;
    return QRect(left, top, kToggleTrackWidth, kToggleTrackHeight);
}

bool toggleChoosesOnFromCellClick(const QRect& cellRect, const QPoint& positionInCell) {
    const QRect track = toggleTrackRectInCell(cellRect);
    if (!track.contains(positionInCell)) {
        return track.center().x() <= positionInCell.x();
    }
    return positionInCell.x() >= track.center().x();
}

void paintParameterToggleSwitch(QPainter& painter, const QRect& cellRect, ParameterToggleSwitch::VisualState state,
                                bool focused) {
    const QRect track = toggleTrackRectInCell(cellRect);
    QColor trackColor(QStringLiteral("#CFD6DB"));
    if (state == ParameterToggleSwitch::VisualState::On) {
        trackColor = QColor(QStringLiteral("#007D88"));
    } else if (state == ParameterToggleSwitch::VisualState::Unknown) {
        trackColor = QColor(QStringLiteral("#E7EBEE"));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(trackColor);
    painter.drawRoundedRect(track, kToggleTrackHeight / 2, kToggleTrackHeight / 2);

    int thumbX = track.left() + kThumbMargin;
    if (state == ParameterToggleSwitch::VisualState::On) {
        thumbX = track.right() - kThumbDiameter - kThumbMargin;
    } else if (state == ParameterToggleSwitch::VisualState::Unknown) {
        thumbX = track.center().x() - kThumbDiameter / 2;
    }
    const QRect thumb(thumbX, track.center().y() - kThumbDiameter / 2, kThumbDiameter, kThumbDiameter);
    painter.setBrush(QColor(QStringLiteral("#FFFFFF")));
    painter.drawEllipse(thumb);

    if (state == ParameterToggleSwitch::VisualState::Unknown) {
        painter.setPen(QColor(QStringLiteral("#A86609")));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(track.adjusted(0, 0, -1, -1), kToggleTrackHeight / 2, kToggleTrackHeight / 2);
    }

    if (focused) {
        const QRect outline = track.adjusted(-2, -2, 2, 2);
        painter.setPen(QColor(QStringLiteral("#005D9B")));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(outline, kToggleTrackHeight / 2 + 2, kToggleTrackHeight / 2 + 2);
    }
}

ParameterToggleSwitch::ParameterToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setObjectName(QStringLiteral("erncParameterToggleSwitch"));
    setCursor(Qt::PointingHandCursor);
    setCheckable(false);
    setFocusPolicy(Qt::StrongFocus);
    function_view_layout::styleToggleSwitch(this);
}

int ParameterToggleSwitch::semanticOptionCount() const {
    return 2;
}

QString ParameterToggleSwitch::semanticOptionLabel(int index) const {
    if (index == 0) {
        return QStringLiteral("开");
    }
    if (index == 1) {
        return QStringLiteral("关");
    }
    return {};
}

void ParameterToggleSwitch::applyCaption(const QString& caption) {
    const QString normalized = normalizeUnknownCaption(caption);
    if (normalized == QStringLiteral("开")) {
        setVisualState(VisualState::On, false);
    } else if (normalized == QStringLiteral("关")) {
        setVisualState(VisualState::Off, false);
    } else {
        setVisualState(VisualState::Unknown, false);
    }
}

ParameterToggleSwitch::VisualState ParameterToggleSwitch::visualState() const {
    return state_;
}

QString ParameterToggleSwitch::currentWord() const {
    if (state_ == VisualState::On) {
        return QStringLiteral("开");
    }
    if (state_ == VisualState::Off) {
        return QStringLiteral("关");
    }
    return {};
}

void ParameterToggleSwitch::setSuppressSignals(bool suppress) {
    suppressSignals_ = suppress;
}

void ParameterToggleSwitch::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    paintParameterToggleSwitch(painter, rect(), state_, hasFocus());
}

void ParameterToggleSwitch::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QAbstractButton::mouseReleaseEvent(event);
        return;
    }
    commitFromPointer(event->pos());
    QAbstractButton::mouseReleaseEvent(event);
}

QSize ParameterToggleSwitch::sizeHint() const {
    return {kToggleTrackWidth, kToggleTrackHeight + kSpace8};
}

QSize ParameterToggleSwitch::minimumSizeHint() const {
    return sizeHint();
}

void ParameterToggleSwitch::commitFromPointer(const QPoint& pos) {
    if (!rect().contains(pos)) {
        return;
    }
    const bool chooseOn = toggleChoosesOnFromCellClick(rect(), pos);
    const VisualState next = chooseOn ? VisualState::On : VisualState::Off;
    setVisualState(next, true);
}

void ParameterToggleSwitch::setVisualState(VisualState state, bool notify) {
    if (state_ == state) {
        return;
    }
    state_ = state;
    update();
    if (notify && !suppressSignals_) {
        const QString word = currentWord();
        if (!word.isEmpty()) {
            emit userChose(word);
        }
    }
}

}  // namespace ernc::presentation
