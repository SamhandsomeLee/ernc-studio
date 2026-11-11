#include "analysis/spec_radius_plot.hpp"

#include "analysis/plot_workspace.hpp"

#include <QLabel>
#include <QVBoxLayout>

#include <string_view>
#include <utility>
#include <vector>

namespace ernc::presentation {
namespace {

/// 谱半径的固件路径。
constexpr std::string_view kPath = "ERNC_TF.SpecRadius";

/// 谱半径的点数。manifest 中 shape 是 {120}。
constexpr std::size_t kPointCount = 120;

/// @brief 这份数据是不是 120 点谱半径。
/// @param dataset 待检查的数据集，只在本次调用期间借用。
/// @return 路径、shape 和点数都符合时为 true。
bool accepted(const ernc::domain::AnalysisDataset& dataset) {
    return dataset.path() == kPath && dataset.shape() == std::vector<std::uint32_t>{static_cast<std::uint32_t>(kPointCount)}
        && dataset.sampleCount() == kPointCount;
}

}  // namespace

SpecRadiusPlot::SpecRadiusPlot(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("erncSpecRadiusPlot"));
    caption_ = new QLabel(this);
    caption_->setObjectName(QStringLiteral("erncSpecRadiusCaption"));
    workspace_ = new PlotWorkspace(this);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(caption_);
    layout->addWidget(workspace_, 1);
}

ernc::domain::Result<void> SpecRadiusPlot::setCurve(const ernc::domain::AnalysisDataset& dataset) {
    if (!accepted(dataset)) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    auto previous = std::move(dataset_);
    dataset_ = dataset;
    const ernc::domain::Result<void> shown = workspace_->setDataset(*dataset_);
    if (!shown.ok()) {
        dataset_ = std::move(previous);
        return shown;
    }
    caption_->setText(QStringLiteral("ERNC_TF.SpecRadius\n索引 0–119"));
    return ernc::domain::Result<void>::success();
}

std::size_t SpecRadiusPlot::sampleCount() const noexcept {
    return dataset_ ? dataset_->sampleCount() : 0U;
}

ernc::domain::Result<float> SpecRadiusPlot::valueAt(std::size_t index) const {
    if (!dataset_) {
        return ernc::domain::Result<float>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return dataset_->valueAt(index);
}

PlotWorkspace* SpecRadiusPlot::plotWorkspace() const noexcept {
    return workspace_;
}

QString SpecRadiusPlot::chromeText() const {
    return caption_->text();
}

}  // namespace ernc::presentation
