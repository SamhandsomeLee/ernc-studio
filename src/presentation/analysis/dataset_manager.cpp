#include "analysis/dataset_manager.hpp"

#include "analysis/plot_workspace.hpp"
#include "ernc/domain/analysis/series_mapper.hpp"

#include <QVBoxLayout>

#include <fstream>
#include <string_view>
#include <utility>

namespace ernc::presentation {
namespace {

/// 曲线色板。颜色是 ERNC Studio 自己的区分色，不是设备参数，也不是 Siemens 官方色。
constexpr std::string_view kCurvePaletteHex[] = {
    "#007D88", "#B42318", "#167A49", "#2563A5", "#A86609",
    "#7A4EAB", "#C24D2C", "#3B6E8F", "#5E8F3B", "#8A5A44",
};

/// 色板大小。
constexpr std::size_t kCurvePaletteSize = sizeof(kCurvePaletteHex) / sizeof(kCurvePaletteHex[0]);

/// @brief 按加入顺序分配稳定颜色。
/// @param index 数据集加入时的序号。
/// @return 色板中的一种颜色。超过色板长度时循环复用。
QColor colorForIndex(std::size_t index) {
    const std::string_view hex = kCurvePaletteHex[index % kCurvePaletteSize];
    return QColor(QString::fromLatin1(hex.data(), static_cast<int>(hex.size())));
}

/// @brief 抽稀步长。
/// @param pointCount 一条曲线的原始点数。
/// @param limit 每根曲线的显示点上限。0 表示不降采样。
/// @return 不降采样时为 1，否则是 ceil(pointCount / limit)。每条曲线点数相同，步长一致。
std::size_t decimationStride(std::size_t pointCount, std::size_t limit) noexcept {
    if (limit == 0 || pointCount <= limit) {
        return 1;
    }
    return (pointCount + limit - 1) / limit;
}

/// @brief 抽稀后的显示点数。
/// @param pointCount 一条曲线的原始点数。
/// @param limit 每根曲线的显示点上限。0 表示不降采样。
/// @return 等于 ceil(pointCount / stride)，不超过 limit（limit 为 0 时等于 pointCount）。
std::size_t decimatedCount(std::size_t pointCount, std::size_t limit) noexcept {
    const std::size_t stride = decimationStride(pointCount, limit);
    return (pointCount + stride - 1) / stride;
}

}  // namespace

DatasetManager::DatasetManager(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("erncDatasetManager"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    workspace_ = new PlotWorkspace(this);
    layout->addWidget(workspace_);
}

ernc::domain::Result<void> DatasetManager::addDataset(const QString& name, const ernc::domain::AnalysisDataset& dataset) {
    if (name.isEmpty() || find(name) != nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    entries_.push_back(Entry{name, dataset, colorForIndex(entries_.size()), true});
    return rebuild();
}

ernc::domain::Result<void> DatasetManager::removeDataset(const QString& name) {
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->name == name) {
            entries_.erase(it);
            return rebuild();
        }
    }
    return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
}

ernc::domain::Result<void> DatasetManager::setVisible(const QString& name, bool visible) {
    for (Entry& entry : entries_) {
        if (entry.name == name) {
            entry.visible = visible;
            return rebuild();
        }
    }
    return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
}

ernc::domain::Result<void> DatasetManager::exportRaw(const QString& name, const std::filesystem::path& path) {
    const Entry* entry = find(name);
    if (entry == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::AnalysisDataset& dataset = entry->dataset;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::FileIoFailed);
    }
    for (std::size_t index = 0; index < dataset.sampleCount(); ++index) {
        const ernc::domain::Result<float> read = dataset.valueAt(index);
        if (!read.ok()) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::FileIoFailed);
        }
        const float value = *read.tryValue();
        out.write(reinterpret_cast<const char*>(&value), sizeof(value));
        if (!out) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::FileIoFailed);
        }
    }
    return ernc::domain::Result<void>::success();
}

std::size_t DatasetManager::datasetCount() const noexcept {
    return entries_.size();
}

QStringList DatasetManager::datasetNames() const {
    QStringList names;
    names.reserve(static_cast<int>(entries_.size()));
    for (const Entry& entry : entries_) {
        names.append(entry.name);
    }
    return names;
}

bool DatasetManager::isVisible(const QString& name) const noexcept {
    const Entry* entry = find(name);
    return entry != nullptr && entry->visible;
}

std::size_t DatasetManager::rawSampleCount(const QString& name) const noexcept {
    const Entry* entry = find(name);
    return entry == nullptr ? 0U : entry->dataset.sampleCount();
}

std::size_t DatasetManager::displayedPointCount(const QString& name) const noexcept {
    const Entry* entry = find(name);
    if (entry == nullptr) {
        return 0U;
    }
    const ernc::domain::Result<ernc::domain::SeriesMap> mapped = ernc::domain::mapStorageSeries(entry->dataset);
    if (!mapped.ok()) {
        return 0U;
    }
    std::size_t total = 0;
    for (std::size_t series = 0; series < mapped.value().seriesCount(); ++series) {
        total += decimatedCount(mapped.value().pointCount(), displayPointLimit_);
    }
    return total;
}

QColor DatasetManager::colorOf(const QString& name) const noexcept {
    const Entry* entry = find(name);
    return entry == nullptr ? QColor() : entry->color;
}

std::size_t DatasetManager::displayPointLimit() const noexcept {
    return displayPointLimit_;
}

void DatasetManager::setDisplayPointLimit(std::size_t limit) {
    displayPointLimit_ = limit;
    (void)rebuild();
}

PlotWorkspace* DatasetManager::plotWorkspace() const noexcept {
    return workspace_;
}

ernc::domain::Result<void> DatasetManager::rebuild() {
    std::vector<OverlayCurve> curves;
    for (const Entry& entry : entries_) {
        if (!entry.visible) {
            continue;
        }
        const ernc::domain::Result<ernc::domain::SeriesMap> mapped = ernc::domain::mapStorageSeries(entry.dataset);
        if (!mapped.ok()) {
            return ernc::domain::Result<void>::failure(mapped.error());
        }
        const ernc::domain::SeriesMap& map = mapped.value();
        const std::size_t seriesCount = map.seriesCount();
        const std::size_t pointCount = map.pointCount();
        const std::size_t stride = decimationStride(pointCount, displayPointLimit_);
        for (std::size_t series = 0; series < seriesCount; ++series) {
            OverlayCurve curve;
            curve.title = seriesCount == 1 ? entry.name
                                           : QStringLiteral("%1 · %2").arg(entry.name).arg(static_cast<qlonglong>(series));
            curve.color = entry.color;
            for (std::size_t point = 0; point < pointCount; point += stride) {
                const ernc::domain::Result<ernc::domain::SeriesPoint> sample = map.pointAt(entry.dataset, series, point);
                if (!sample.ok()) {
                    return ernc::domain::Result<void>::failure(sample.error());
                }
                curve.points.emplace_back(static_cast<double>(sample.value().pointIndex),
                    static_cast<double>(sample.value().value));
            }
            curves.push_back(std::move(curve));
        }
    }
    return workspace_->setOverlayCurves(curves, QStringLiteral("叠加"));
}

const DatasetManager::Entry* DatasetManager::find(const QString& name) const noexcept {
    for (const Entry& entry : entries_) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace ernc::presentation
