#include "theme/status_badge.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOption>

#include <algorithm>

namespace ernc::presentation {
namespace {

/// @brief 在图标盒子里画一个不依赖颜色区分的形状。
/// @param painter 已设置状态颜色的画笔，只在本次调用期间借用。
/// @param rect 图标盒子，逻辑像素，调用方拥有。
/// @param shape 要画的形状。
void paintStatusIcon(QPainter& painter, const QRect& rect, StatusIconShape shape) {
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QRectF box = QRectF(rect).adjusted(1.5, 1.5, -1.5, -1.5);
    QPen pen = painter.pen();
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);

    switch (shape) {
    case StatusIconShape::Question:
    case StatusIconShape::Ellipsis: {
        QFont glyph = painter.font();
        glyph.setPixelSize(static_cast<int>(box.height()));
        painter.setFont(glyph);
        painter.drawText(rect, Qt::AlignCenter,
            shape == StatusIconShape::Question ? QStringLiteral("?") : QStringLiteral("…"));
        break;
    }
    case StatusIconShape::List: {
        const qreal y0 = box.top() + box.height() * 0.25;
        const qreal y1 = box.center().y();
        const qreal y2 = box.top() + box.height() * 0.75;
        painter.drawLine(QPointF(box.left(), y0), QPointF(box.right(), y0));
        painter.drawLine(QPointF(box.left(), y1), QPointF(box.right(), y1));
        painter.drawLine(QPointF(box.left(), y2), QPointF(box.right(), y2));
        break;
    }
    case StatusIconShape::Triangle: {
        QPainterPath path;
        path.moveTo(box.center().x(), box.top());
        path.lineTo(box.right(), box.bottom());
        path.lineTo(box.left(), box.bottom());
        path.closeSubpath();
        painter.setBrush(pen.color());
        painter.drawPath(path);
        break;
    }
    case StatusIconShape::Cross:
        painter.drawLine(box.topLeft(), box.bottomRight());
        painter.drawLine(box.topRight(), box.bottomLeft());
        break;
    case StatusIconShape::SingleCheck: {
        QPainterPath path;
        path.moveTo(box.left(), box.center().y());
        path.lineTo(box.left() + box.width() * 0.38, box.bottom());
        path.lineTo(box.right(), box.top());
        painter.drawPath(path);
        break;
    }
    case StatusIconShape::CircledCheck: {
        painter.drawEllipse(box);
        const QRectF inner = box.adjusted(box.width() * 0.24, box.height() * 0.28,
            -box.width() * 0.2, -box.height() * 0.24);
        QPainterPath path;
        path.moveTo(inner.left(), inner.center().y());
        path.lineTo(inner.left() + inner.width() * 0.38, inner.bottom());
        path.lineTo(inner.right(), inner.top());
        painter.drawPath(path);
        break;
    }
    case StatusIconShape::NotEqual: {
        const qreal y0 = box.top() + box.height() * 0.35;
        const qreal y1 = box.top() + box.height() * 0.65;
        painter.drawLine(QPointF(box.left(), y0), QPointF(box.right(), y0));
        painter.drawLine(QPointF(box.left(), y1), QPointF(box.right(), y1));
        painter.drawLine(QPointF(box.right() - box.width() * 0.28, box.top()),
            QPointF(box.left() + box.width() * 0.28, box.bottom()));
        break;
    }
    case StatusIconShape::Clock:
        painter.drawEllipse(box);
        painter.drawLine(box.center(), QPointF(box.center().x(), box.top() + box.height() * 0.22));
        painter.drawLine(box.center(), QPointF(box.right() - box.width() * 0.22, box.center().y()));
        break;
    case StatusIconShape::HalfCircle:
        painter.setBrush(pen.color());
        painter.drawPie(box, 90 * 16, 180 * 16);
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(box);
        break;
    case StatusIconShape::BrokenCircle:
        painter.drawArc(box, 45 * 16, 250 * 16);
        break;
    }
    painter.restore();
}

} // namespace

StatusBadge::StatusBadge(StatusKind kind, QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("erncStatusBadge"));
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setContentsMargins(kSpace8, kSpace4, kSpace8, kSpace4);

    const StatusPresentation* presentation = findStatusPresentation(kind);
    if (presentation == nullptr) {
        text_ = QStringLiteral("状态未定义");
        color_ = QColor(QStringLiteral("#202B33"));
        icon_ = StatusIconShape::Question;
        setAccessibleName(text_);
        setAccessibleDescription(QStringLiteral("没有对应的状态说法，不能解释为 Off"));
        setToolTip(accessibleDescription());
        return;
    }

    text_ = QString::fromUtf8(presentation->text.data(), static_cast<qsizetype>(presentation->text.size()));
    color_ = QColor(QString::fromUtf8(presentation->colorHex.data(),
        static_cast<qsizetype>(presentation->colorHex.size())));
    if (!color_.isValid()) {
        color_ = QColor(QStringLiteral("#202B33"));
    }
    icon_ = presentation->icon;
    const QString description = QString::fromUtf8(presentation->description.data(),
        static_cast<qsizetype>(presentation->description.size()));
    setAccessibleName(text_);
    setAccessibleDescription(description);
    setToolTip(description);
}

QSize StatusBadge::sizeHint() const {
    const QFontMetrics metrics(font());
    const QMargins margins = contentsMargins();
    const int width = margins.left() + kSpace16 + kSpace8
        + metrics.horizontalAdvance(text_) + margins.right();
    const int height = margins.top() + std::max(kSpace16, metrics.height()) + margins.bottom();
    return {width, height};
}

QSize StatusBadge::minimumSizeHint() const {
    return sizeHint();
}

void StatusBadge::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    QStyleOption option;
    option.initFrom(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);

    const QRect content = contentsRect();
    const int iconTop = content.top() + std::max(0, (content.height() - kSpace16) / 2);
    const QRect iconRect(content.left(), iconTop, kSpace16, kSpace16);
    const QRect textRect(iconRect.right() + kSpace8, content.top(),
        std::max(0, content.width() - kSpace16 - kSpace8), content.height());

    painter.setPen(color_);
    paintStatusIcon(painter, iconRect, icon_);
    painter.setPen(color_);
    painter.setFont(font());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, text_);
}

void StatusBadge::changeEvent(QEvent* event) {
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        updateGeometry();
    }
    QWidget::changeEvent(event);
}

} // namespace ernc::presentation
