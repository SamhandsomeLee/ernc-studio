#include "analysis/plot_workspace.hpp"

#include "analysis/curve_color_palette.hpp"
#include "ernc/domain/analysis/series_mapper.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPen>
#include <QPointF>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QVector>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <qwt_legend.h>
#include <qwt_legend_data.h>
#include <qwt_legend_label.h>
#include <qwt_picker_machine.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_text.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_picker.h>
#include <qwt_plot_zoomer.h>

#include <algorithm>
#include <utility>

namespace ernc::presentation {
namespace {

/// @brief 轻微提高曲线颜色的饱和度和亮度，用于克制的选中高亮。
/// @param base 原始曲线颜色。
/// @return 与原色接近但可区分的强调色。
QColor fluorescentColor(const QColor& base) {
    int hue = 0;
    int saturation = 0;
    int lightness = 0;
    int alpha = 255;
    base.getHsl(&hue, &saturation, &lightness, &alpha);
    if (hue < 0) {
        hue = 195;
    }
    saturation = qMin(255, saturation + 30);
    lightness = qMin(238, lightness + 18);
    QColor neon;
    neon.setHsl(hue, saturation, lightness, 255);
    return neon;
}

/// @brief 从一条已载入曲线复制显示点。
/// @param source 源曲线，只在本次调用期间借用。
/// @return 与 dataSize 一致的点列。
QVector<QPointF> copyCurveSamples(const QwtPlotCurve* source) {
    QVector<QPointF> samples;
    if (source == nullptr) {
        return samples;
    }
    const int count = static_cast<int>(source->dataSize());
    samples.reserve(count);
    for (int index = 0; index < count; ++index) {
        samples.append(source->sample(index));
    }
    return samples;
}

}  // namespace

PlotWorkspace::PlotWorkspace(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("erncPlotWorkspace"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("erncPlotTabs"));
    plot_ = new QwtPlot(tabs_);
    plot_->setObjectName(QStringLiteral("erncPlot"));
    plot_->setAxisTitle(QwtAxis::XBottom, QStringLiteral("索引"));
    plot_->setAxisTitle(QwtAxis::YLeft, QStringLiteral("值（单位未确认）"));
    legend_ = new QwtLegend(plot_);
    plot_->insertLegend(legend_, QwtPlot::RightLegend);
    configurePlotGrid();
    configureLegendInteraction();
    curveHintLabel_ = new QLabel(plot_);
    curveHintLabel_->setObjectName(QStringLiteral("erncPlotCurveHint"));
    curveHintLabel_->setAlignment(Qt::AlignCenter);
    curveHintLabel_->setWordWrap(true);
    curveHintLabel_->setStyleSheet(
        QStringLiteral("QLabel { background: rgba(255, 255, 255, 252); border: 2px solid #007D88; border-radius: 4px;"
                       "padding: 4px 14px; color: #005A62; font-weight: 600; font-size: 13px; }"));
    curveHintLabel_->hide();
    tabs_->addTab(plot_, QStringLiteral("未载入"));
    layout->addWidget(tabs_);
    configurePlotNavigation();
    createResetViewButton();
}

void PlotWorkspace::createResetViewButton() {
    QWidget* canvas = plot_ != nullptr ? plot_->canvas() : nullptr;
    if (canvas == nullptr) {
        return;
    }
    resetViewButton_ = new QToolButton(plot_);
    resetViewButton_->setObjectName(QStringLiteral("erncPlotResetView"));
    resetViewButton_->setAutoRaise(true);
    resetViewButton_->setCursor(Qt::ArrowCursor);
    resetViewButton_->setFocusPolicy(Qt::NoFocus);
    resetViewButton_->setToolTip(QStringLiteral("复位视图（恢复默认缩放）"));
    resetViewButton_->setIcon(style()->standardIcon(QStyle::SP_BrowserReload));
    resetViewButton_->setIconSize(QSize(14, 14));
    resetViewButton_->setFixedSize(22, 22);
    resetViewButton_->setStyleSheet(
        QStringLiteral("QToolButton { background: rgba(255, 255, 255, 210); border: 1px solid #B0B8C0; border-radius: 3px; }"
                       "QToolButton:hover { background: rgba(255, 255, 255, 245); border-color: #007D88; }"));
    connect(resetViewButton_, &QToolButton::clicked, this, [this]() { resetPlotView(); });
    updateResetViewButtonGeometry();
}

void PlotWorkspace::updateResetViewButtonGeometry() {
    if (resetViewButton_ == nullptr || plot_ == nullptr) {
        return;
    }
    QWidget* canvas = plot_->canvas();
    if (canvas == nullptr) {
        return;
    }
    const int margin = 6;
    const QSize buttonSize = resetViewButton_->size();
    const QRect canvasInPlot = canvas->geometry();
    resetViewButton_->setParent(plot_);
    resetViewButton_->move(canvasInPlot.right() - buttonSize.width() - margin + 1,
        canvasInPlot.bottom() - buttonSize.height() - margin + 1);
    resetViewButton_->show();
    resetViewButton_->raise();
    updateCurveHintGeometry();
    if (curveHintLabel_ != nullptr && emphasizedCurve_ != nullptr) {
        curveHintLabel_->raise();
    }
}

void PlotWorkspace::configurePlotGrid() {
    grid_ = new QwtPlotGrid();
    grid_->enableX(true);
    grid_->enableY(true);
    grid_->enableXMin(true);
    grid_->enableYMin(true);
    grid_->setMajorPen(QPen(QColor(QStringLiteral("#8A939E")), 1, Qt::SolidLine));
    grid_->setMinorPen(QPen(QColor(QStringLiteral("#C5CCD4")), 0, Qt::DotLine));
    grid_->setZ(-10.0);
    grid_->attach(plot_);
}

void PlotWorkspace::syncLegendClickHandlers() {
    if (legend_ == nullptr) {
        return;
    }

    std::vector<bool> bound(curves_.size(), false);
    const QList<QwtLegendLabel*> labels = legend_->findChildren<QwtLegendLabel*>();
    for (QwtLegendLabel* label : labels) {
        if (label == nullptr) {
            continue;
        }
        const QString labelTitle = label->data().title().text();
        for (std::size_t index = 0; index < curves_.size(); ++index) {
            QwtPlotCurve* curve = curves_[index];
            if (bound[index] || curve == nullptr || curve->title().text() != labelTitle) {
                continue;
            }
            label->setItemMode(QwtLegendData::Clickable);
            QObject::disconnect(label, &QwtLegendLabel::clicked, this, nullptr);
            bound[index] = true;
            break;
        }
    }
}

void PlotWorkspace::configureLegendInteraction() {
    legend_->setDefaultItemMode(QwtLegendData::Clickable);
    if (QCoreApplication::instance() != nullptr) {
        QCoreApplication::instance()->installEventFilter(this);
    }
    connect(plot_, &QwtPlot::legendDataChanged, this, [this](const QVariant&, const QList<QwtLegendData>&) {
        QTimer::singleShot(0, this, [this] { syncLegendClickHandlers(); });
    });
}

void PlotWorkspace::scheduleLegendHandlerSync() {
    syncLegendClickHandlers();
    QTimer::singleShot(0, this, [this] { syncLegendClickHandlers(); });
}

void PlotWorkspace::scheduleCurveEmphasis(QwtPlotCurve* curve) {
    const std::size_t generation = curveGeneration_;
    QTimer::singleShot(0, this, [this, curve, generation] {
        if (generation == curveGeneration_) {
            emphasizeCurve(curve);
        }
    });
}

void PlotWorkspace::recordCurveStyles() {
    curveStyles_.clear();
    curveStyles_.reserve(curves_.size());
    for (const QwtPlotCurve* curve : curves_) {
        CurvePenStyle style;
        if (curve != nullptr) {
            style.pen = curve->pen();
            style.z = curve->z();
        }
        curveStyles_.push_back(style);
    }
}

void PlotWorkspace::clearEmphasisGlow() {
    for (QwtPlotCurve* glow : emphasisGlow_) {
        if (glow != nullptr) {
            glow->detach();
            delete glow;
        }
    }
    emphasisGlow_.clear();
}

void PlotWorkspace::clearCurveEmphasis() {
    const bool changed = emphasizedCurve_ != nullptr || !emphasisGlow_.empty()
        || (curveHintLabel_ != nullptr && curveHintLabel_->isVisible());
    clearEmphasisGlow();
    emphasizedCurve_ = nullptr;
    for (std::size_t i = 0; i < curves_.size(); ++i) {
        QwtPlotCurve* curve = curves_[i];
        if (curve == nullptr || i >= curveStyles_.size()) {
            continue;
        }
        const CurvePenStyle& base = curveStyles_[i];
        curve->setPen(base.pen);
        curve->setZ(base.z);
    }
    if (curveHintLabel_ != nullptr) {
        curveHintLabel_->hide();
    }
    if (changed && plot_ != nullptr) {
        plot_->replot();
    }
}

void PlotWorkspace::rebuildEmphasisGlow(QwtPlotCurve* selected, const QColor& neon) {
    clearEmphasisGlow();
    if (selected == nullptr || plot_ == nullptr) {
        return;
    }
    const QVector<QPointF> samples = copyCurveSamples(selected);
    if (samples.isEmpty()) {
        return;
    }
    const auto makeGlow = [&](qreal width, int alpha, qreal z) {
        auto* glow = new QwtPlotCurve();
        glow->setSamples(samples);
        QColor penColor = neon;
        penColor.setAlpha(alpha);
        glow->setPen(QPen(penColor, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        glow->setRenderHint(QwtPlotItem::RenderAntialiased, true);
        glow->setItemAttribute(QwtPlotItem::Legend, false);
        glow->setItemAttribute(QwtPlotItem::AutoScale, false);
        glow->setZ(z);
        glow->attach(plot_);
        emphasisGlow_.push_back(glow);
    };
    makeGlow(7.0, 45, 11.0);
}

void PlotWorkspace::emphasizeCurve(QwtPlotCurve* selected) {
    if (selected == nullptr || std::find(curves_.begin(), curves_.end(), selected) == curves_.end()) {
        return;
    }
    if (selected == emphasizedCurve_) {
        clearCurveEmphasis();
        return;
    }
    emphasizedCurve_ = selected;
    QColor neon;
    for (std::size_t i = 0; i < curves_.size(); ++i) {
        QwtPlotCurve* curve = curves_[i];
        if (curve == nullptr || i >= curveStyles_.size()) {
            continue;
        }
        const CurvePenStyle& base = curveStyles_[i];
        if (curve == selected) {
            neon = fluorescentColor(base.pen.color());
            curve->setPen(QPen(neon, 3.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            curve->setZ(12.0);
        } else {
            QColor muted = base.pen.color();
            muted.setAlpha(105);
            curve->setPen(QPen(muted, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            curve->setZ(1.0);
        }
    }
    rebuildEmphasisGlow(selected, neon);
    if (curveHintLabel_ != nullptr) {
        const QString border = neon.isValid() ? neon.name(QColor::HexRgb) : QStringLiteral("#007D88");
        curveHintLabel_->setStyleSheet(
            QStringLiteral("QLabel { background: rgba(255, 255, 255, 245); border: 1px solid %1; border-radius: 4px;"
                           "padding: 3px 10px; color: %1; font-weight: 600; font-size: 12px; }")
                .arg(border));
        curveHintLabel_->setText(QStringLiteral("当前曲线：%1").arg(selected->title().text()));
        updateCurveHintGeometry();
        curveHintLabel_->show();
        curveHintLabel_->raise();
    }
    plot_->replot();
}

void PlotWorkspace::updateCurveHintGeometry() {
    if (curveHintLabel_ == nullptr || plot_ == nullptr) {
        return;
    }
    QWidget* canvas = plot_->canvas();
    if (canvas == nullptr) {
        return;
    }
    const QRect canvasInPlot = canvas->geometry();
    curveHintLabel_->setParent(plot_);
    const int maxWidth = qMax(160, canvasInPlot.width() - 48);
    curveHintLabel_->setMaximumWidth(maxWidth);
    curveHintLabel_->adjustSize();
    const int x = canvasInPlot.left() + (canvasInPlot.width() - curveHintLabel_->width()) / 2;
    curveHintLabel_->move(x, canvasInPlot.top() + 6);
}

void PlotWorkspace::configurePlotNavigation() {
    zoomer_ = new QwtPlotZoomer(plot_->canvas());
    zoomer_->setTrackerMode(QwtPicker::AlwaysOff);
    zoomer_->setRubberBandPen(QPen(QColor(QStringLiteral("#007D88")), 1, Qt::DashLine));
    zoomer_->setMousePattern(QwtEventPattern::MouseSelect2, Qt::RightButton, Qt::ControlModifier);

    panner_ = new QwtPlotPanner(plot_->canvas());
    panner_->setMouseButton(Qt::RightButton, Qt::NoModifier);

    magnifier_ = new QwtPlotMagnifier(plot_->canvas());
    magnifier_->setMouseButton(Qt::NoButton);
    magnifier_->setWheelModifiers(Qt::NoModifier);
    magnifier_->setAxisEnabled(QwtAxis::XBottom, true);
    magnifier_->setAxisEnabled(QwtAxis::YLeft, true);

    picker_ = new QwtPlotPicker(QwtAxis::XBottom, QwtAxis::YLeft, QwtPicker::CrossRubberBand, QwtPicker::AlwaysOn,
                                plot_->canvas());
    picker_->setStateMachine(new QwtPickerTrackerMachine());
}

void PlotWorkspace::resetPlotView() {
    plot_->setAxisAutoScale(QwtAxis::XBottom);
    plot_->setAxisAutoScale(QwtAxis::YLeft);
    plot_->replot();
    zoomer_->setZoomBase();
}

void PlotWorkspace::resetView() {
    resetPlotView();
}

ernc::domain::Result<void> PlotWorkspace::setDataset(const ernc::domain::AnalysisDataset& dataset,
                                                     const std::size_t colorOffset) {
    const ernc::domain::Result<ernc::domain::SeriesMap> mapped = ernc::domain::mapStorageSeries(dataset);
    if (!mapped.ok()) {
        return ernc::domain::Result<void>::failure(mapped.error());
    }
    const ernc::domain::SeriesMap& map = mapped.value();
    std::vector<QVector<QPointF>> samples(map.seriesCount());
    for (std::size_t series = 0; series < map.seriesCount(); ++series) {
        samples[series].reserve(static_cast<int>(map.pointCount()));
        for (std::size_t point = 0; point < map.pointCount(); ++point) {
            const ernc::domain::Result<ernc::domain::SeriesPoint> sample = map.pointAt(dataset, series, point);
            if (!sample.ok()) {
                return ernc::domain::Result<void>::failure(sample.error());
            }
            samples[series].append(QPointF(static_cast<double>(sample.value().pointIndex),
                static_cast<double>(sample.value().value)));
        }
    }

    dataset_ = dataset;
    ++curveGeneration_;
    canvasPressPosition_.reset();
    clearEmphasisGlow();
    plot_->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    curves_.clear();
    curveStyles_.clear();
    emphasizedCurve_ = nullptr;
    if (curveHintLabel_ != nullptr) {
        curveHintLabel_->hide();
    }
    curves_.reserve(samples.size());
    for (std::size_t series = 0; series < samples.size(); ++series) {
        const QString title = samples.size() == 1 ? QString::fromStdString(dataset.path())
                                                : QStringLiteral("%1 · 系列 %2")
                                                      .arg(QString::fromStdString(dataset.path()))
                                                      .arg(static_cast<qlonglong>(series + 1));
        auto* curve = new QwtPlotCurve(title);
        curve->setSamples(samples[series]);
        curve->setPen(curveColorForIndex(colorOffset + series), 2.0);
        curve->setRenderHint(QwtPlotItem::RenderAntialiased, true);
        curve->setLegendAttribute(QwtPlotCurve::LegendShowLine, true);
        curve->attach(plot_);
        curves_.push_back(curve);
    }
    recordCurveStyles();
    tabs_->setTabText(0, QString::fromStdString(dataset.path()));
    resetPlotView();
    updateResetViewButtonGeometry();
    scheduleLegendHandlerSync();
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> PlotWorkspace::setOverlayCurves(const std::vector<OverlayCurve>& curves, const QString& tabTitle) {
    ++curveGeneration_;
    canvasPressPosition_.reset();
    clearEmphasisGlow();
    plot_->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    curves_.clear();
    curveStyles_.clear();
    emphasizedCurve_ = nullptr;
    if (curveHintLabel_ != nullptr) {
        curveHintLabel_->hide();
    }
    curves_.reserve(curves.size());
    for (std::size_t index = 0; index < curves.size(); ++index) {
        const OverlayCurve& spec = curves[index];
        QVector<QPointF> samples;
        samples.reserve(static_cast<int>(spec.points.size()));
        for (const QPointF& point : spec.points) {
            samples.append(point);
        }
        auto* curve = new QwtPlotCurve(spec.title);
        curve->setSamples(samples);
        const QColor penColor = spec.color.isValid() ? spec.color : curveColorForIndex(index);
        curve->setPen(penColor, 2.0);
        curve->setRenderHint(QwtPlotItem::RenderAntialiased, true);
        curve->setLegendAttribute(QwtPlotCurve::LegendShowLine, true);
        curve->attach(plot_);
        curves_.push_back(curve);
    }
    recordCurveStyles();
    if (!tabTitle.isEmpty()) {
        tabs_->setTabText(0, tabTitle);
    }
    resetPlotView();
    updateResetViewButtonGeometry();
    scheduleLegendHandlerSync();
    return ernc::domain::Result<void>::success();
}

int PlotWorkspace::tabCount() const {
    return tabs_->count();
}

QString PlotWorkspace::tabTitle() const {
    return tabs_->tabText(0);
}

QwtPlot* PlotWorkspace::plot() const noexcept {
    return plot_;
}

QwtPlotZoomer* PlotWorkspace::zoomer() const noexcept {
    return zoomer_;
}

QwtPlotPanner* PlotWorkspace::panner() const noexcept {
    return panner_;
}

QwtPlotMagnifier* PlotWorkspace::magnifier() const noexcept {
    return magnifier_;
}

QwtPlotPicker* PlotWorkspace::picker() const noexcept {
    return picker_;
}

QwtLegend* PlotWorkspace::legend() const noexcept {
    return legend_;
}

std::size_t PlotWorkspace::curveCount() const noexcept {
    return curves_.size();
}

ernc::domain::Result<std::size_t> PlotWorkspace::displayedPointCount(std::size_t series) const {
    if (series >= curves_.size() || curves_[series] == nullptr) {
        return ernc::domain::Result<std::size_t>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return ernc::domain::Result<std::size_t>::success(curves_[series]->dataSize());
}

std::size_t PlotWorkspace::rawSampleCount() const noexcept {
    return dataset_ ? dataset_->sampleCount() : 0U;
}

bool PlotWorkspace::handleWheelZoom(QWheelEvent* event) {
    if (plot_ == nullptr || magnifier_ == nullptr || event == nullptr) {
        return false;
    }
    QWidget* canvas = plot_->canvas();
    if (canvas == nullptr) {
        return false;
    }
    QCoreApplication::sendEvent(canvas, event);
    return event->isAccepted();
}

void PlotWorkspace::wheelEvent(QWheelEvent* event) {
    if (handleWheelZoom(event)) {
        return;
    }
    QWidget::wheelEvent(event);
}

void PlotWorkspace::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResetViewButtonGeometry();
}

void PlotWorkspace::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updateResetViewButtonGeometry();
}

void PlotWorkspace::hideEvent(QHideEvent* event) {
    clearCurveEmphasis();
    QWidget::hideEvent(event);
}

bool PlotWorkspace::eventFilter(QObject* watched, QEvent* event) {
    if (plot_ != nullptr && resetViewButton_ != nullptr
        && (watched == plot_ || watched == plot_->canvas())
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        updateResetViewButtonGeometry();
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto* label = qobject_cast<QwtLegendLabel*>(watched);
        const auto* mouse = static_cast<const QMouseEvent*>(event);
        if (label != nullptr && legend_ != nullptr && legend_->isAncestorOf(label) && mouse->button() == Qt::LeftButton) {
            const QString labelTitle = label->data().title().text();
            const auto selected = std::find_if(curves_.begin(), curves_.end(), [&labelTitle](const QwtPlotCurve* curve) {
                return curve != nullptr && curve->title().text() == labelTitle;
            });
            if (selected != curves_.end()) {
                scheduleCurveEmphasis(*selected);
            }
        }
    }

    if (plot_ != nullptr && watched == plot_->canvas()) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<const QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                canvasPressPosition_ = mouse->position().toPoint();
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<const QMouseEvent*>(event);
            const std::optional<QPoint> pressedAt = canvasPressPosition_;
            canvasPressPosition_.reset();
            if (mouse->button() == Qt::LeftButton && pressedAt
                && (mouse->position().toPoint() - *pressedAt).manhattanLength() <= QApplication::startDragDistance()) {
                clearCurveEmphasis();
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

ernc::domain::Result<float> PlotWorkspace::rawValue(std::size_t storageIndex) const {
    if (!dataset_) {
        return ernc::domain::Result<float>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return dataset_->valueAt(storageIndex);
}

}  // namespace ernc::presentation
