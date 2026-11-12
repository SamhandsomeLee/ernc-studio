#include "analysis/check_data_plot.hpp"

#include "analysis/plot_workspace.hpp"

#include "ernc/domain/analysis/series_mapper.hpp"

#include <QApplication>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <array>
#include <string_view>
#include <utility>

namespace ernc::presentation {
namespace {

/// check_data 的 20 个子字段。顺序与解码器名单一致，不表示单位。
constexpr std::array<std::string_view, 20> kFields{
    "check_data.OutputCheck",
    "check_data.RefCheck",
    "check_data.ErrCheck",
    "check_data.ENCRefCheckSin",
    "check_data.ENCRefCheckCos",
    "check_data.ENCEngineSpeedCheck",
    "check_data.ENCErrCheck",
    "check_data.ENCOutputCheck",
    "check_data.HornRefCheck_T",
    "check_data.HornRefCheck_F",
    "check_data.xPowCheck",
    "check_data.MiuCheck",
    "check_data.ENCEchoGainCheck",
    "check_data.OrderSpkoutmaxCheck",
    "check_data.DataSpeedCheck",
    "check_data.DataSinCheck",
    "check_data.DataCosCheck",
    "check_data.RNCAdapCheckRefNum",
    "check_data.RNCAdapCheckSpkNum",
    "check_data.RNCAdapCheck",
};

/// @brief 路径是不是 20 个 check_data 子字段之一。
/// @param path 字段路径，只在本次调用期间借用。
/// @return 在名单里时为 true。
bool isCheckDataField(std::string_view path) noexcept {
    for (const std::string_view field : kFields) {
        if (field == path) {
            return true;
        }
    }
    return false;
}

/// @brief 从一条曲线拷贝出单独的数据集。
/// @param source 完整字段，只在本次调用期间借用。
/// @param map 这份字段的存储映射，只在本次调用期间借用。条数和点数必须和 source 一致。
/// @param series 从 0 开始的曲线号。
/// @return 成功时调用方拥有这一条曲线的数据集。空 shape 仍是空 shape。其它情况 shape 只有最后一维。
/// 读不到某个点时为 InvalidParameter，不返回半条曲线。
ernc::domain::Result<ernc::domain::AnalysisDataset> sliceSeries(const ernc::domain::AnalysisDataset& source,
    const ernc::domain::SeriesMap& map, std::size_t series) {
    ernc::domain::DecodedField slice;
    slice.path = source.path();
    if (source.shape().empty()) {
        slice.shape = {};
    } else {
        slice.shape = {static_cast<std::uint32_t>(map.pointCount())};
    }
    slice.values.reserve(map.pointCount());
    for (std::size_t point = 0; point < map.pointCount(); ++point) {
        const ernc::domain::Result<std::size_t> index = map.storageIndex(series, point);
        if (!index.ok()) {
            return ernc::domain::Result<ernc::domain::AnalysisDataset>::failure(index.error());
        }
        const ernc::domain::Result<float> sample = source.valueAt(index.value());
        if (!sample.ok()) {
            return ernc::domain::Result<ernc::domain::AnalysisDataset>::failure(sample.error());
        }
        slice.values.push_back(sample.value());
    }
    return ernc::domain::AnalysisDataset::fromDecoded(std::move(slice));
}

/// @brief 指针在图表上时，滚轮用于 Qwt 缩放，不驱动 QScrollArea 上下滚动。
class PlotWheelScrollGuard final : public QObject {
public:
    explicit PlotWheelScrollGuard(QScrollArea* scroll) : QObject(scroll), scroll_(scroll) {
        scroll_->viewport()->installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched != scroll_->viewport() || event->type() != QEvent::Wheel) {
            return QObject::eventFilter(watched, event);
        }
        auto* wheel = static_cast<QWheelEvent*>(event);
        QWidget* at = QApplication::widgetAt(wheel->globalPosition().toPoint());
        for (QWidget* widget = at; widget != nullptr && widget != scroll_; widget = widget->parentWidget()) {
            if (widget->objectName() != QStringLiteral("erncPlotWorkspace")) {
                continue;
            }
            auto* workspace = static_cast<PlotWorkspace*>(widget);
            (void)workspace->handleWheelZoom(wheel);
            wheel->accept();
            return true;
        }
        return false;
    }

private:
    QScrollArea* scroll_;
};

}  // namespace

CheckDataPlot::CheckDataPlot(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("erncCheckDataPlot"));
    fieldBox_ = new QComboBox(this);
    modeBox_ = new QComboBox(this);
    modeBox_->addItem(QStringLiteral("叠加"));
    modeBox_->addItem(QStringLiteral("堆叠"));

    auto* controls = new QHBoxLayout();
    controls->addWidget(new QLabel(QStringLiteral("字段"), this));
    controls->addWidget(fieldBox_, 1);
    controls->addWidget(new QLabel(QStringLiteral("模式"), this));
    controls->addWidget(modeBox_);

    auto* host = new QWidget(this);
    plotLayout_ = new QVBoxLayout(host);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setWidget(host);
    (void)new PlotWheelScrollGuard(scroll);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(controls);
    layout->addWidget(scroll, 1);

    connect(fieldBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }
        if (!selectField(fieldBox_->itemText(index).toStdString()).ok()) {
            syncFieldBox();
        }
    });
    connect(modeBox_, &QComboBox::currentIndexChanged, this, [this](int index) {
        const CheckDataViewMode mode = index == 0 ? CheckDataViewMode::Overlay : CheckDataViewMode::Stack;
        if (!setMode(mode).ok()) {
            syncModeBox();
        }
    });
}

ernc::domain::Result<void> CheckDataPlot::setField(const ernc::domain::AnalysisDataset& field) {
    if (!isCheckDataField(field.path())) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const bool first = currentPath_.empty();
    bool replaced = false;
    for (ernc::domain::AnalysisDataset& stored : fields_) {
        if (stored.path() == field.path()) {
            stored = field;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        fields_.push_back(field);
    }
    const std::string previousPath = currentPath_;
    if (first) {
        currentPath_ = field.path();
    }
    syncFieldBox();
    if (!first && currentPath_ != field.path()) {
        return ernc::domain::Result<void>::success();
    }
    const ernc::domain::Result<void> shown = rebuild();
    if (!shown.ok()) {
        currentPath_ = previousPath;
        syncFieldBox();
        return shown;
    }
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> CheckDataPlot::selectField(std::string_view path) {
    if (findField(path) == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (path == currentPath_) {
        return ernc::domain::Result<void>::success();
    }
    const std::string previousPath = currentPath_;
    currentPath_ = std::string(path);
    syncFieldBox();
    const ernc::domain::Result<void> shown = rebuild();
    if (!shown.ok()) {
        currentPath_ = previousPath;
        syncFieldBox();
        return shown;
    }
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> CheckDataPlot::setMode(CheckDataViewMode mode) {
    if (mode == mode_) {
        return ernc::domain::Result<void>::success();
    }
    const CheckDataViewMode previous = mode_;
    mode_ = mode;
    syncModeBox();
    if (currentPath_.empty()) {
        return ernc::domain::Result<void>::success();
    }
    const ernc::domain::Result<void> shown = rebuild();
    if (!shown.ok()) {
        mode_ = previous;
        syncModeBox();
        return shown;
    }
    return ernc::domain::Result<void>::success();
}

CheckDataViewMode CheckDataPlot::mode() const noexcept {
    return mode_;
}

std::size_t CheckDataPlot::fullSampleCount(std::string_view path) const noexcept {
    const ernc::domain::AnalysisDataset* field = findField(path);
    return field == nullptr ? 0U : field->sampleCount();
}

ernc::domain::Result<float> CheckDataPlot::valueAt(std::string_view path, std::size_t storageIndex) const {
    const ernc::domain::AnalysisDataset* field = findField(path);
    if (field == nullptr) {
        return ernc::domain::Result<float>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return field->valueAt(storageIndex);
}

int CheckDataPlot::plotCount() const noexcept {
    return static_cast<int>(plots_.size());
}

ernc::domain::Result<std::size_t> CheckDataPlot::displayedPointCount(int plotIndex) const {
    if (plotIndex < 0 || plotIndex >= plotCount()) {
        return ernc::domain::Result<std::size_t>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    return plots_[static_cast<std::size_t>(plotIndex)]->displayedPointCount(0);
}

PlotWorkspace* CheckDataPlot::plotAt(int plotIndex) const noexcept {
    if (plotIndex < 0 || plotIndex >= plotCount()) {
        return nullptr;
    }
    return plots_[static_cast<std::size_t>(plotIndex)];
}

QString CheckDataPlot::chromeText() const {
    QString text = fieldBox_->currentText();
    text += QLatin1Char('\n');
    text += modeBox_->currentText();
    for (const QLabel* label : labels_) {
        text += QLatin1Char('\n');
        text += label->text();
    }
    return text;
}

ernc::domain::Result<void> CheckDataPlot::rebuild() {
    const ernc::domain::AnalysisDataset* current = findField(currentPath_);
    if (current == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    std::vector<ernc::domain::AnalysisDataset> views;
    if (mode_ == CheckDataViewMode::Overlay) {
        views.push_back(*current);
    } else {
        const ernc::domain::Result<ernc::domain::SeriesMap> mapped = ernc::domain::mapStorageSeries(*current);
        if (!mapped.ok()) {
            return ernc::domain::Result<void>::failure(mapped.error());
        }
        const ernc::domain::SeriesMap& map = mapped.value();
        views.reserve(map.seriesCount());
        for (std::size_t series = 0; series < map.seriesCount(); ++series) {
            ernc::domain::Result<ernc::domain::AnalysisDataset> slice = sliceSeries(*current, map, series);
            if (!slice.ok()) {
                return ernc::domain::Result<void>::failure(slice.error());
            }
            views.push_back(std::move(slice.value()));
        }
    }

    std::vector<QWidget*> rows;
    std::vector<PlotWorkspace*> newPlots;
    std::vector<QLabel*> newLabels;
    rows.reserve(views.size());
    for (std::size_t index = 0; index < views.size(); ++index) {
        auto* row = new QWidget();
        auto* rowLayout = new QVBoxLayout(row);
        auto* label = new QLabel(row);
        if (mode_ == CheckDataViewMode::Stack) {
            label->setText(QString::fromStdString(current->path()) + QStringLiteral(" 组 %1").arg(static_cast<qlonglong>(index)));
        } else {
            label->setText(QString::fromStdString(current->path()));
        }
        auto* plot = new PlotWorkspace(row);
        const ernc::domain::Result<void> shown = plot->setDataset(views[index], index);
        if (!shown.ok()) {
            delete row;
            for (QWidget* created : rows) {
                delete created;
            }
            return shown;
        }
        rowLayout->addWidget(label);
        rowLayout->addWidget(plot);
        rows.push_back(row);
        newPlots.push_back(plot);
        newLabels.push_back(label);
    }

    while (QLayoutItem* item = plotLayout_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    plots_.clear();
    labels_.clear();
    for (QWidget* row : rows) {
        plotLayout_->addWidget(row);
    }
    plots_ = std::move(newPlots);
    labels_ = std::move(newLabels);
    return ernc::domain::Result<void>::success();
}

void CheckDataPlot::syncFieldBox() {
    const QSignalBlocker blocker(fieldBox_);
    fieldBox_->clear();
    for (const ernc::domain::AnalysisDataset& field : fields_) {
        fieldBox_->addItem(QString::fromStdString(field.path()));
    }
    const int index = fieldBox_->findText(QString::fromStdString(currentPath_));
    if (index >= 0) {
        fieldBox_->setCurrentIndex(index);
    }
}

void CheckDataPlot::syncModeBox() {
    const QSignalBlocker blocker(modeBox_);
    modeBox_->setCurrentIndex(mode_ == CheckDataViewMode::Overlay ? 0 : 1);
}

const ernc::domain::AnalysisDataset* CheckDataPlot::findField(std::string_view path) const noexcept {
    for (const ernc::domain::AnalysisDataset& field : fields_) {
        if (field.path() == path) {
            return &field;
        }
    }
    return nullptr;
}

}  // namespace ernc::presentation
