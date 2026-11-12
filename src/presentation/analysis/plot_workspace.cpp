#include "analysis/plot_workspace.hpp"

#include "analysis/curve_color_palette.hpp"
#include "ernc/domain/analysis/series_mapper.hpp"

#include <QCoreApplication>
#include <QPen>
#include <QPointF>
#include <QEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QStyle>
#include <QToolButton>
#include <QWheelEvent>
#include <QTabWidget>
#include <QVector>
#include <QVBoxLayout>

#include <qwt_legend.h>
#include <qwt_picker_machine.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_magnifier.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_picker.h>
#include <qwt_plot_zoomer.h>

#include <utility>

namespace ernc::presentation {

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
    canvas->installEventFilter(this);
    plot_->installEventFilter(this);
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
    plot_->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    curves_.clear();
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
    tabs_->setTabText(0, QString::fromStdString(dataset.path()));
    resetPlotView();
    updateResetViewButtonGeometry();
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> PlotWorkspace::setOverlayCurves(const std::vector<OverlayCurve>& curves, const QString& tabTitle) {
    plot_->detachItems(QwtPlotItem::Rtti_PlotCurve, true);
    curves_.clear();
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
    if (!tabTitle.isEmpty()) {
        tabs_->setTabText(0, tabTitle);
    }
    resetPlotView();
    updateResetViewButtonGeometry();
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

bool PlotWorkspace::eventFilter(QObject* watched, QEvent* event) {
    if (plot_ != nullptr && resetViewButton_ != nullptr
        && (watched == plot_ || watched == plot_->canvas())
        && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        updateResetViewButtonGeometry();
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
