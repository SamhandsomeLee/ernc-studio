#include "analysis/analysis_panel.hpp"

#include "analysis/analysis_region_bridge.hpp"
#include "analysis/check_data_plot.hpp"
#include "analysis/dataset_manager.hpp"
#include "analysis/ernc_tf_plot.hpp"
#include "analysis/spec_radius_plot.hpp"

#include <QLabel>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>

#include "regions/region_workspace.hpp"

#include <sstream>
#include <string_view>

namespace ernc::presentation {
namespace {

using ernc::domain::AnalysisDataset;
using ernc::domain::RegionDefinition;
using ernc::domain::Result;

constexpr const char* kRealPath = "ERNC_TF.fSecPathTransR";
constexpr const char* kImagPath = "ERNC_TF.fSecPathTransI";
constexpr const char* kSpecPath = "ERNC_TF.SpecRadius";

bool isCheckDataPath(std::string_view path) noexcept {
    return path == "check_data" || path.rfind("check_data.", 0) == 0;
}

}  // namespace

AnalysisPanel::AnalysisPanel(const ernc::domain::RegionCatalog& catalog, ernc::application::RegionReader* reader,
                             ernc::application::RegionWriter* writer, QWidget* parent)
    : QWidget(parent)
    , catalog_(catalog) {
    setObjectName(QStringLiteral("erncCurveAnalysis"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    regionWorkspace_ = new RegionWorkspace(catalog_, reader, writer, splitter);
    regionWorkspace_->setObjectName(QStringLiteral("erncRegionWorkspace"));
    auto* right = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(right);
    datasetManager_ = new DatasetManager(tabs_);
    checkPlot_ = new CheckDataPlot(tabs_);
    tfPlot_ = new ErncTfPlot(tabs_);
    specPlot_ = new SpecRadiusPlot(tabs_);
    tabs_->addTab(datasetManager_, QStringLiteral("叠加曲线"));
    tabs_->addTab(checkPlot_, QStringLiteral("检测数据"));
    tabs_->addTab(tfPlot_, QStringLiteral("传递函数"));
    tabs_->addTab(specPlot_, QStringLiteral("谱半径"));
    rightLayout->addWidget(tabs_, 1);
    splitter->addWidget(regionWorkspace_);
    splitter->addWidget(right);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    root->addWidget(splitter, 1);
    regionWorkspace_->setBytesAvailableHandler([this](const RegionDefinition& region, std::span<const std::uint8_t> bytes) {
        const Result<void> loaded = loadRegionBytes(region, bytes);
        if (!loaded.ok()) {
            publishStatus(QStringLiteral("解码失败，请核对区域与文件长度。"));
            if (uiActivityRecorder_) {
                std::ostringstream detail;
                detail << "{\"region\":\"" << region.path << "\",\"byte_count\":" << bytes.size()
                       << ",\"stage\":\"analysis.decode\"}";
                uiActivityRecorder_("region.decode", detail.str(), false, loaded.error());
            }
            return;
        }
        if (uiActivityRecorder_) {
            std::ostringstream detail;
            detail << "{\"region\":\"" << region.path << "\",\"byte_count\":" << bytes.size()
                   << ",\"stage\":\"analysis.decode\"}";
            uiActivityRecorder_("region.decode", detail.str(), true, ernc::domain::ErrorCode::Unknown);
        }
        publishStatus(QStringLiteral("已载入 ") + QString::fromStdString(region.path));
        focusSpecializedTab(region.path);
    });
}

void AnalysisPanel::setStatusSink(std::function<void(const QString&)> sink) {
    statusSink_ = std::move(sink);
}

void AnalysisPanel::publishStatus(const QString& text) {
    if (statusSink_) {
        statusSink_(text);
    }
}

void AnalysisPanel::prepareIngest(std::string_view regionPath) {
    lastRegionPath_ = std::string(regionPath);
    if (regionPath == "ERNC_TF" || regionPath == "check_data") {
        pendingReal_.reset();
        pendingImag_.reset();
    }
}

Result<void> AnalysisPanel::upsertOverlay(const AnalysisDataset& dataset) {
    const QString name = QString::fromStdString(dataset.path());
    if (datasetManager_->datasetNames().contains(name)) {
        const Result<void> removed = datasetManager_->removeDataset(name);
        if (!removed.ok()) {
            return removed;
        }
    }
    return datasetManager_->addDataset(name, dataset);
}

void AnalysisPanel::bindTfPlotIfReady() {
    if (!pendingReal_ || !pendingImag_) {
        return;
    }
    (void)tfPlot_->setFields(*pendingReal_, *pendingImag_);
}

Result<void> AnalysisPanel::ingestField(const AnalysisDataset& dataset) {
    const Result<void> overlay = upsertOverlay(dataset);
    if (!overlay.ok()) {
        return overlay;
    }
    const std::string_view path{dataset.path()};
    if (isCheckDataPath(path)) {
        (void)checkPlot_->setField(dataset);
    }
    if (path == kSpecPath) {
        (void)specPlot_->setCurve(dataset);
    }
    if (path == kRealPath) {
        pendingReal_ = dataset;
        bindTfPlotIfReady();
    } else if (path == kImagPath) {
        pendingImag_ = dataset;
        bindTfPlotIfReady();
    }
    return Result<void>::success();
}

Result<void> AnalysisPanel::loadRegionBytes(const RegionDefinition& region, std::span<const std::uint8_t> bytes) {
    return AnalysisRegionBridge::applyBytes(catalog_, region, bytes, *this);
}

void AnalysisPanel::focusSpecializedTab(std::string_view regionPath) {
    if (isCheckDataPath(regionPath)) {
        tabs_->setCurrentWidget(checkPlot_);
        return;
    }
    if (regionPath == kSpecPath) {
        tabs_->setCurrentWidget(specPlot_);
        return;
    }
    if (regionPath == "ERNC_TF" || regionPath.rfind("ERNC_TF.", 0) == 0) {
        tabs_->setCurrentWidget(tfPlot_);
    }
}

RegionWorkspace* AnalysisPanel::regionWorkspace() const noexcept {
    return regionWorkspace_;
}

void AnalysisPanel::setUiActivityRecorder(
    std::function<void(std::string_view, std::string_view, bool, ernc::domain::ErrorCode)> recorder) {
    uiActivityRecorder_ = recorder;
    if (regionWorkspace_ != nullptr) {
        regionWorkspace_->setUiActivityRecorder(recorder);
    }
}

}  // namespace ernc::presentation
