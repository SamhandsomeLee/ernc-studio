#include "analysis/plot_workspace.hpp"

#include "ernc/domain/analysis/series_mapper.hpp"

#include <QPointF>
#include <QTabWidget>
#include <QVector>
#include <QVBoxLayout>

#include <qwt_legend.h>
#include <qwt_picker_machine.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
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

    zoomer_ = new QwtPlotZoomer(plot_->canvas());
    zoomer_->setTrackerMode(QwtPicker::AlwaysOff);
    panner_ = new QwtPlotPanner(plot_->canvas());
    panner_->setMouseButton(Qt::MiddleButton, Qt::NoModifier);
    picker_ = new QwtPlotPicker(QwtAxis::XBottom, QwtAxis::YLeft, QwtPicker::CrossRubberBand,
        QwtPicker::AlwaysOn, plot_->canvas());
    picker_->setStateMachine(new QwtPickerTrackerMachine());
}

ernc::domain::Result<void> PlotWorkspace::setDataset(const ernc::domain::AnalysisDataset& dataset) {
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
        auto* curve = new QwtPlotCurve(QStringLiteral("曲线 %1").arg(static_cast<qlonglong>(series)));
        curve->setSamples(samples[series]);
        curve->attach(plot_);
        curves_.push_back(curve);
    }
    tabs_->setTabText(0, QString::fromStdString(dataset.path()));
    plot_->replot();
    zoomer_->setZoomBase(false);
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

ernc::domain::Result<float> PlotWorkspace::rawValue(std::size_t storageIndex) const {
    if (!dataset_) {
        return ernc::domain::Result<float>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return dataset_->valueAt(storageIndex);
}

}  // namespace ernc::presentation
