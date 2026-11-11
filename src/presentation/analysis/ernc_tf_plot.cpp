#include "analysis/ernc_tf_plot.hpp"

#include "analysis/plot_workspace.hpp"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <string_view>
#include <utility>
#include <vector>

namespace ernc::presentation {
namespace {

/// 实部的固件路径。
constexpr std::string_view kRealPath = "ERNC_TF.fSecPathTransR";

/// 虚部的固件路径。
constexpr std::string_view kImagPath = "ERNC_TF.fSecPathTransI";

/// 前两维各自的长度。来自 manifest 的 shape {6, 6, 120}。
constexpr std::uint32_t kGroupCount = 6;

/// 最后一维的点数。
constexpr std::uint32_t kPointCount = 120;

/// 一份字段的 float 个数。6 * 6 * 120。
constexpr std::size_t kSampleCount = 4320;

/// @brief 路径和 shape 是不是实部或虚部的完整字段。
/// @param dataset 待检查的数据集，只在本次调用期间借用。
/// @param path 要求的固件路径。
/// @return 路径、shape 和点数都符合时为 true。
bool acceptedField(const ernc::domain::AnalysisDataset& dataset, std::string_view path) {
    return dataset.path() == path && dataset.shape() == std::vector<std::uint32_t>{kGroupCount, kGroupCount, kPointCount}
        && dataset.sampleCount() == kSampleCount;
}

/// @brief 计算一个点的存储下标。
/// @param group0 第一维下标，调用前已经限制在 0 到 5。
/// @param group1 第二维下标，调用前已经限制在 0 到 5。
/// @param point 最后一维下标，调用前已经限制在 0 到 119。
/// @return (group0 * 6 + group1) * 120 + point。
std::size_t storageIndex(std::uint32_t group0, std::uint32_t group1, std::uint32_t point) noexcept {
    return (static_cast<std::size_t>(group0) * kGroupCount + group1) * kPointCount + point;
}

/// @brief 从完整字段拷贝一组 120 个点。
/// @param source 完整字段，只在本次调用期间借用。样本数必须盖住这一组。
/// @param group0 第一维下标。
/// @param group1 第二维下标。
/// @return 成功时调用方拥有 120 点字段。路径仍是原字段路径，shape 是 {120}。
/// 任一存储下标读不到时为 InvalidParameter，不返回带 0 的半组。
ernc::domain::Result<ernc::domain::DecodedField> sliceGroup(const ernc::domain::AnalysisDataset& source, std::uint32_t group0,
    std::uint32_t group1) {
    ernc::domain::DecodedField slice;
    slice.path = source.path();
    slice.shape = {kPointCount};
    slice.values.reserve(kPointCount);
    for (std::uint32_t point = 0; point < kPointCount; ++point) {
        const ernc::domain::Result<float> sample = source.valueAt(storageIndex(group0, group1, point));
        if (!sample.ok()) {
            return ernc::domain::Result<ernc::domain::DecodedField>::failure(sample.error());
        }
        slice.values.push_back(sample.value());
    }
    return ernc::domain::Result<ernc::domain::DecodedField>::success(std::move(slice));
}

}  // namespace

ErncTfPlot::ErncTfPlot(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("erncTfPlot"));
    fieldBox_ = new QComboBox(this);
    fieldBox_->addItem(QStringLiteral("实部"));
    fieldBox_->addItem(QStringLiteral("虚部"));
    group0Box_ = new QSpinBox(this);
    group1Box_ = new QSpinBox(this);
    pointBox_ = new QSpinBox(this);
    group0Box_->setRange(0, static_cast<int>(kGroupCount - 1));
    group1Box_->setRange(0, static_cast<int>(kGroupCount - 1));
    pointBox_->setRange(0, static_cast<int>(kPointCount - 1));
    readout_ = new QLabel(this);
    readout_->setObjectName(QStringLiteral("erncTfPointReadout"));
    workspace_ = new PlotWorkspace(this);

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel(QStringLiteral("字段"), this));
    controls->addWidget(fieldBox_);
    controls->addWidget(new QLabel(QStringLiteral("组 0"), this));
    controls->addWidget(group0Box_);
    controls->addWidget(new QLabel(QStringLiteral("组 1"), this));
    controls->addWidget(group1Box_);
    controls->addWidget(new QLabel(QStringLiteral("点"), this));
    controls->addWidget(pointBox_);
    controls->addStretch(1);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addWidget(workspace_, 1);
    layout->addWidget(readout_);

    connect(fieldBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const ErncTfField field = index == 0 ? ErncTfField::Real : ErncTfField::Imag;
        if (!selectField(field).ok()) {
            syncControls();
        }
    });
    connect(group0Box_, &QSpinBox::valueChanged, this, [this](int value) {
        if (!selectGroup(static_cast<std::uint32_t>(value), group1_).ok()) {
            syncControls();
        }
    });
    connect(group1Box_, &QSpinBox::valueChanged, this, [this](int value) {
        if (!selectGroup(group0_, static_cast<std::uint32_t>(value)).ok()) {
            syncControls();
        }
    });
    connect(pointBox_, &QSpinBox::valueChanged, this, [this](int value) {
        if (!selectPoint(static_cast<std::uint32_t>(value)).ok()) {
            syncControls();
        }
    });
}

ernc::domain::Result<void> ErncTfPlot::setFields(const ernc::domain::AnalysisDataset& real, const ernc::domain::AnalysisDataset& imag) {
    if (!acceptedField(real, kRealPath) || !acceptedField(imag, kImagPath)) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    auto previousReal = std::move(real_);
    auto previousImag = std::move(imag_);
    const ErncTfField previousField = field_;
    const std::uint32_t previousGroup0 = group0_;
    const std::uint32_t previousGroup1 = group1_;
    const std::uint32_t previousPoint = point_;
    const bool previousLoaded = loaded_;
    real_ = real;
    imag_ = imag;
    field_ = ErncTfField::Real;
    group0_ = 0;
    group1_ = 0;
    point_ = 0;
    const ernc::domain::Result<void> shown = redraw();
    if (!shown.ok()) {
        real_ = std::move(previousReal);
        imag_ = std::move(previousImag);
        field_ = previousField;
        group0_ = previousGroup0;
        group1_ = previousGroup1;
        point_ = previousPoint;
        loaded_ = previousLoaded;
        return shown;
    }
    loaded_ = true;
    syncControls();
    updateReadout();
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> ErncTfPlot::selectField(ErncTfField field) {
    if (!loaded_) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (field == field_) {
        updateReadout();
        return ernc::domain::Result<void>::success();
    }
    const ErncTfField previous = field_;
    field_ = field;
    const ernc::domain::Result<void> shown = redraw();
    if (!shown.ok()) {
        field_ = previous;
        return shown;
    }
    syncControls();
    updateReadout();
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> ErncTfPlot::selectGroup(std::uint32_t group0, std::uint32_t group1) {
    if (!loaded_ || group0 >= kGroupCount || group1 >= kGroupCount) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const std::uint32_t previousGroup0 = group0_;
    const std::uint32_t previousGroup1 = group1_;
    group0_ = group0;
    group1_ = group1;
    const ernc::domain::Result<void> shown = redraw();
    if (!shown.ok()) {
        group0_ = previousGroup0;
        group1_ = previousGroup1;
        return shown;
    }
    syncControls();
    updateReadout();
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> ErncTfPlot::selectPoint(std::uint32_t point) {
    if (!loaded_ || point >= kPointCount) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    point_ = point;
    syncControls();
    updateReadout();
    return ernc::domain::Result<void>::success();
}

std::size_t ErncTfPlot::fullSampleCount(ErncTfField field) const noexcept {
    const std::optional<ernc::domain::AnalysisDataset>& dataset = field == ErncTfField::Real ? real_ : imag_;
    return dataset ? dataset->sampleCount() : 0U;
}

ernc::domain::Result<float> ErncTfPlot::valueAt(ErncTfField field, std::size_t storageIndex) const {
    const std::optional<ernc::domain::AnalysisDataset>& dataset = field == ErncTfField::Real ? real_ : imag_;
    if (!dataset) {
        return ernc::domain::Result<float>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return dataset->valueAt(storageIndex);
}

ernc::domain::Result<std::size_t> ErncTfPlot::displayedPointCount() const {
    return workspace_->displayedPointCount(0);
}

QString ErncTfPlot::pointReadout() const {
    return readout_->text();
}

PlotWorkspace* ErncTfPlot::plotWorkspace() const noexcept {
    return workspace_;
}

ernc::domain::Result<void> ErncTfPlot::redraw() {
    const std::optional<ernc::domain::AnalysisDataset>& source = field_ == ErncTfField::Real ? real_ : imag_;
    if (!source) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<ernc::domain::DecodedField> slice = sliceGroup(*source, group0_, group1_);
    if (!slice.ok()) {
        return ernc::domain::Result<void>::failure(slice.error());
    }
    const ernc::domain::Result<ernc::domain::AnalysisDataset> view = ernc::domain::AnalysisDataset::fromDecoded(slice.value());
    if (!view.ok()) {
        return ernc::domain::Result<void>::failure(view.error());
    }
    return workspace_->setDataset(view.value());
}

void ErncTfPlot::syncControls() {
    const QSignalBlocker blockField(fieldBox_);
    const QSignalBlocker blockGroup0(group0Box_);
    const QSignalBlocker blockGroup1(group1Box_);
    const QSignalBlocker blockPoint(pointBox_);
    fieldBox_->setCurrentIndex(field_ == ErncTfField::Real ? 0 : 1);
    group0Box_->setValue(static_cast<int>(group0_));
    group1Box_->setValue(static_cast<int>(group1_));
    pointBox_->setValue(static_cast<int>(point_));
}

void ErncTfPlot::updateReadout() {
    if (!loaded_) {
        readout_->clear();
        return;
    }
    const ernc::domain::Result<float> sample = valueAt(field_, storageIndex(group0_, group1_, point_));
    if (!sample.ok()) {
        readout_->clear();
        return;
    }
    readout_->setText(QStringLiteral("点 %1 = %2").arg(point_).arg(QString::number(sample.value(), 'g', 9)));
}

}  // namespace ernc::presentation
