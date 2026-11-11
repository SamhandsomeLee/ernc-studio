#include "ernc/domain/analysis/series_mapper.hpp"

#include <cstdint>
#include <limits>
#include <optional>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

/// 拆开后的曲线条数和每条点数。两个数都大于 0，乘积等于样本数。
struct StorageSplit {
    /// 曲线条数。
    std::size_t seriesCount{0};

    /// 每条曲线的点数。
    std::size_t pointCount{0};
};

/// @brief 按最后一维拆曲线。
/// @param shape 各级长度。空表示一个标量。
/// @param sampleCount 数据集的样本数。乘积必须和它相等。
/// @return 每一维都大于 0 且乘积等于 sampleCount 时给出条数和点数。否则为空。
std::optional<StorageSplit> splitShape(const std::vector<std::uint32_t>& shape, std::size_t sampleCount) noexcept {
    std::uint64_t seriesCount = 1U;
    std::uint64_t pointCount = 1U;
    if (shape.empty()) {
        pointCount = 1U;
    } else if (shape.size() == 1U) {
        pointCount = shape.front();
    } else {
        pointCount = shape.back();
        for (std::size_t index = 0; index + 1U < shape.size(); ++index) {
            const std::uint32_t dimension = shape[index];
            if (dimension == 0U || seriesCount > std::numeric_limits<std::uint64_t>::max() / dimension) {
                return std::nullopt;
            }
            seriesCount *= dimension;
        }
    }
    if (pointCount == 0U || seriesCount == 0U
        || seriesCount > std::numeric_limits<std::uint64_t>::max() / pointCount) {
        return std::nullopt;
    }
    const std::uint64_t covered = seriesCount * pointCount;
    if (covered != static_cast<std::uint64_t>(sampleCount)) {
        return std::nullopt;
    }
    return StorageSplit{static_cast<std::size_t>(seriesCount), static_cast<std::size_t>(pointCount)};
}

}  // namespace

SeriesMap::SeriesMap(std::vector<std::uint32_t> shape, std::size_t seriesCount, std::size_t pointCount)
    : shape_{std::move(shape)}, seriesCount_{seriesCount}, pointCount_{pointCount} {}

std::size_t SeriesMap::seriesCount() const noexcept {
    return seriesCount_;
}

std::size_t SeriesMap::pointCount() const noexcept {
    return pointCount_;
}

Result<std::size_t> SeriesMap::storageIndex(std::size_t series, std::size_t point) const {
    if (series >= seriesCount_ || point >= pointCount_) {
        return Result<std::size_t>::failure(ErrorCode::InvalidParameter);
    }
    return Result<std::size_t>::success(series * pointCount_ + point);
}

Result<SeriesPoint> SeriesMap::pointAt(const AnalysisDataset& dataset, std::size_t series, std::size_t point) const {
    const Result<std::size_t> index = storageIndex(series, point);
    if (!index.ok()) {
        return Result<SeriesPoint>::failure(index.error());
    }
    if (dataset.shape() != shape_ || dataset.sampleCount() != seriesCount_ * pointCount_) {
        return Result<SeriesPoint>::failure(ErrorCode::InvalidParameter);
    }
    const Result<float> sample = dataset.valueAt(index.value());
    if (!sample.ok()) {
        return Result<SeriesPoint>::failure(sample.error());
    }
    return Result<SeriesPoint>::success(SeriesPoint{point, index.value(), sample.value()});
}

Result<float> SeriesMap::value(const AnalysisDataset& dataset, std::size_t series, std::size_t point) const {
    const Result<SeriesPoint> sample = pointAt(dataset, series, point);
    if (!sample.ok()) {
        return Result<float>::failure(sample.error());
    }
    return Result<float>::success(sample.value().value);
}

Result<SeriesMap> mapStorageSeries(const AnalysisDataset& dataset) {
    const std::optional<StorageSplit> split = splitShape(dataset.shape(), dataset.sampleCount());
    if (!split) {
        return Result<SeriesMap>::failure(ErrorCode::InvalidParameter);
    }
    return Result<SeriesMap>::success(SeriesMap{dataset.shape(), split->seriesCount, split->pointCount});
}

}  // namespace ernc::domain
