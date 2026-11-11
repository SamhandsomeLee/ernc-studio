#include "ernc/domain/analysis/dataset.hpp"
#include "ernc/domain/analysis/series_mapper.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::AnalysisDataset;
using ernc::domain::DecodedField;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::SeriesMap;
using ernc::domain::SeriesPoint;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 组装一份手写字段。数值不是设备黄金样本。
/// @param path 字段路径。本函数把它移入返回值。
/// @param shape 各级长度。本函数把它移入返回值。
/// @param values 存储顺序的 float。本函数把它移入返回值。
/// @return 调用方拥有的字段。
DecodedField makeField(std::string path, std::vector<std::uint32_t> shape, std::vector<float> values) {
    DecodedField field;
    field.path = std::move(path);
    field.shape = std::move(shape);
    field.values = std::move(values);
    return field;
}

/// @brief 按下标填入可精确表示的整数 float。
/// @param count 个数。
/// @return 下标 i 的值等于 float(i)。i 不超过 16777216 时没有舍入。
std::vector<float> indexValues(std::size_t count) {
    std::vector<float> values(count);
    for (std::size_t index = 0; index < count; ++index) {
        values[index] = static_cast<float>(index);
    }
    return values;
}

/// @brief 120 点保留下标，中间的 NaN 和 Inf 仍在。
/// @return 点数、副本隔离和单条曲线都成立时为 true。
bool specRadiusKeeps120Indexes() {
    DecodedField field = makeField("ERNC_TF.SpecRadius", {120}, indexValues(120));
    field.values[3] = std::numeric_limits<float>::quiet_NaN();
    field.values[4] = std::numeric_limits<float>::infinity();
    const Result<AnalysisDataset> built = AnalysisDataset::fromDecoded(field);
    if (!built.ok()) {
        return fail("spec_radius_keeps_120_indexes");
    }
    field.values[0] = 99.0F;
    const AnalysisDataset& dataset = built.value();
    const Result<SeriesMap> mapped = ernc::domain::mapStorageSeries(dataset);
    if (dataset.path() != "ERNC_TF.SpecRadius" || dataset.shape() != std::vector<std::uint32_t>{120}
        || dataset.sampleCount() != 120U || !mapped.ok() || mapped.value().seriesCount() != 1U
        || mapped.value().pointCount() != 120U) {
        return fail("spec_radius_keeps_120_indexes");
    }
    const Result<float> first = dataset.valueAt(0);
    const Result<float> nanSample = dataset.valueAt(3);
    const Result<SeriesPoint> infSample = mapped.value().pointAt(dataset, 0, 4);
    const Result<std::size_t> index = mapped.value().storageIndex(0, 119);
    if (!first.ok() || first.value() != 0.0F || !nanSample.ok() || !std::isnan(nanSample.value()) || !infSample.ok()
        || infSample.value().pointIndex != 4U || infSample.value().storageIndex != 4U
        || !std::isinf(infSample.value().value) || !index.ok() || index.value() != 119U
        || mapped.value().storageIndex(1, 0).ok() || dataset.valueAt(120).ok()) {
        return fail("spec_radius_keeps_120_indexes");
    }
    return true;
}

/// @brief 6×6×120 拆成 36 条曲线，存储下标不重不漏。
/// @return 第 1 条曲线的第一个点下标是 120，且每个下标恰好出现一次时为 true。
bool gridPartitions6x6x120() {
    constexpr std::size_t kSeries = 36U;
    constexpr std::size_t kPoints = 120U;
    const Result<AnalysisDataset> built = AnalysisDataset::fromDecoded(
        makeField("ERNC_TF.fSecPathTransR", {6, 6, 120}, indexValues(kSeries * kPoints)));
    if (!built.ok()) {
        return fail("grid_partitions_6x6x120");
    }
    const AnalysisDataset& dataset = built.value();
    const Result<SeriesMap> mapped = ernc::domain::mapStorageSeries(dataset);
    if (!mapped.ok() || mapped.value().seriesCount() != kSeries || mapped.value().pointCount() != kPoints) {
        return fail("grid_partitions_6x6x120");
    }
    std::vector<char> seen(kSeries * kPoints, 0);
    for (std::size_t series = 0; series < kSeries; ++series) {
        for (std::size_t point = 0; point < kPoints; ++point) {
            const Result<SeriesPoint> sample = mapped.value().pointAt(dataset, series, point);
            const std::size_t expected = series * kPoints + point;
            if (!sample.ok() || sample.value().pointIndex != point || sample.value().storageIndex != expected
                || sample.value().value != static_cast<float>(expected) || seen[expected] != 0) {
                return fail("grid_partitions_6x6x120");
            }
            seen[expected] = 1;
        }
    }
    const Result<SeriesPoint> secondSeries = mapped.value().pointAt(dataset, 1, 0);
    for (const char mark : seen) {
        if (mark == 0 || !secondSeries.ok() || secondSeries.value().storageIndex != 120U) {
            return fail("grid_partitions_6x6x120");
        }
    }
    return true;
}

/// @brief 空 shape 的一个 float 是 1 条 1 点曲线。
/// @return 路径、点数和越界拒绝都成立时为 true。
bool scalarEmptyShapeIsOnePoint() {
    const Result<AnalysisDataset> built = AnalysisDataset::fromDecoded(
        makeField("check_data.RNCAdapCheckRefNum", {}, {1.5F}));
    if (!built.ok()) {
        return fail("scalar_empty_shape_is_one_point");
    }
    const AnalysisDataset& dataset = built.value();
    const Result<SeriesMap> mapped = ernc::domain::mapStorageSeries(dataset);
    const Result<SeriesPoint> sample = mapped.ok() ? mapped.value().pointAt(dataset, 0, 0) : Result<SeriesPoint>::failure(ErrorCode::InvalidParameter);
    if (!dataset.shape().empty() || dataset.sampleCount() != 1U || !mapped.ok() || mapped.value().seriesCount() != 1U
        || mapped.value().pointCount() != 1U || !sample.ok() || sample.value().storageIndex != 0U
        || sample.value().value != 1.5F || mapped.value().pointAt(dataset, 0, 1).ok()
        || mapped.value().pointAt(dataset, 1, 0).ok()) {
        return fail("scalar_empty_shape_is_one_point");
    }
    return true;
}

/// @brief 点数和 shape 对不上时拒绝，不留下数据集。
/// @return 三种不一致都失败时为 true。
bool shapeMismatchRefuses() {
    const Result<AnalysisDataset> shortRadius = AnalysisDataset::fromDecoded(makeField("ERNC_TF.SpecRadius", {120}, indexValues(119)));
    const Result<AnalysisDataset> emptyNone = AnalysisDataset::fromDecoded(makeField("check_data.RNCAdapCheckRefNum", {}, {}));
    const Result<AnalysisDataset> emptyTwo = AnalysisDataset::fromDecoded(makeField("check_data.RNCAdapCheckRefNum", {}, {1.0F, 2.0F}));
    const Result<AnalysisDataset> shortGrid = AnalysisDataset::fromDecoded(
        makeField("ERNC_TF.fSecPathTransR", {6, 6, 120}, indexValues(4319)));
    if (shortRadius.ok() || shortRadius.tryValue() != nullptr || emptyNone.ok() || emptyTwo.ok() || shortGrid.ok()
        || shortRadius.tryError() == nullptr || *shortRadius.tryError() != ErrorCode::InvalidParameter) {
        return fail("shape_mismatch_refuses");
    }
    return true;
}

/// @brief 含 0 的维度拒绝。
/// @return 每一维为 0 的 shape 都失败时为 true。
bool zeroDimensionRefuses() {
    const Result<AnalysisDataset> zeroOnly = AnalysisDataset::fromDecoded(makeField("zero", {0}, {}));
    const Result<AnalysisDataset> zeroMiddle = AnalysisDataset::fromDecoded(makeField("zero", {6, 0, 120}, {}));
    const Result<AnalysisDataset> zeroLeading = AnalysisDataset::fromDecoded(makeField("zero", {0, 1}, {}));
    if (zeroOnly.ok() || zeroMiddle.ok() || zeroLeading.ok()) {
        return fail("zero_dimension_refuses");
    }
    return true;
}

/// @brief 越界曲线号和另一份 shape 都拒绝。
/// @return 合法端点仍能读到，越界和错 shape 失败时为 true。
bool channelOutOfRangeRefuses() {
    const Result<AnalysisDataset> built = AnalysisDataset::fromDecoded(makeField("check_data.OutputCheck", {6, 200}, indexValues(1200)));
    const Result<AnalysisDataset> other = AnalysisDataset::fromDecoded(makeField("ERNC_TF.SpecRadius", {120}, indexValues(120)));
    if (!built.ok() || !other.ok()) {
        return fail("channel_out_of_range_refuses");
    }
    const Result<SeriesMap> mapped = ernc::domain::mapStorageSeries(built.value());
    if (!mapped.ok() || mapped.value().seriesCount() != 6U || mapped.value().pointCount() != 200U) {
        return fail("channel_out_of_range_refuses");
    }
    const SeriesMap& map = mapped.value();
    const Result<SeriesPoint> last = map.pointAt(built.value(), 5, 199);
    if (!last.ok() || last.value().storageIndex != 1199U || last.value().value != 1199.0F || map.storageIndex(6, 0).ok()
        || map.storageIndex(0, 200).ok() || map.value(other.value(), 0, 0).ok()) {
        return fail("channel_out_of_range_refuses");
    }
    return true;
}

/// @brief 长度为 1 的维度保留，不改写成另一条 shape。
/// @return {1,120} 仍是 1 条 120 点，{120,1} 仍是 120 条 1 点时为 true。
bool shapeIsNotCollapsed() {
    const Result<AnalysisDataset> leading = AnalysisDataset::fromDecoded(makeField("leading", {1, 120}, indexValues(120)));
    const Result<AnalysisDataset> trailing = AnalysisDataset::fromDecoded(makeField("trailing", {120, 1}, indexValues(120)));
    if (!leading.ok() || !trailing.ok()) {
        return fail("shape_is_not_collapsed");
    }
    const Result<SeriesMap> leadingMap = ernc::domain::mapStorageSeries(leading.value());
    const Result<SeriesMap> trailingMap = ernc::domain::mapStorageSeries(trailing.value());
    if (leading.value().shape() != std::vector<std::uint32_t>{1, 120} || trailing.value().shape() != std::vector<std::uint32_t>{120, 1}
        || !leadingMap.ok() || !trailingMap.ok() || leadingMap.value().seriesCount() != 1U
        || leadingMap.value().pointCount() != 120U || trailingMap.value().seriesCount() != 120U
        || trailingMap.value().pointCount() != 1U) {
        return fail("shape_is_not_collapsed");
    }
    const Result<std::size_t> leadingIndex = leadingMap.value().storageIndex(0, 7);
    const Result<std::size_t> trailingIndex = trailingMap.value().storageIndex(7, 0);
    if (!leadingIndex.ok() || leadingIndex.value() != 7U || !trailingIndex.ok() || trailingIndex.value() != 7U
        || trailingMap.value().storageIndex(7, 1).ok()) {
        return fail("shape_is_not_collapsed");
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。这些用例不读取 manifest，也不使用设备样本。
/// @return 用例通过时为 true。
bool runCase(std::string_view name) {
    if (name == "spec_radius_keeps_120_indexes") {
        return specRadiusKeeps120Indexes();
    }
    if (name == "grid_partitions_6x6x120") {
        return gridPartitions6x6x120();
    }
    if (name == "scalar_empty_shape_is_one_point") {
        return scalarEmptyShapeIsOnePoint();
    }
    if (name == "shape_mismatch_refuses") {
        return shapeMismatchRefuses();
    }
    if (name == "zero_dimension_refuses") {
        return zeroDimensionRefuses();
    }
    if (name == "channel_out_of_range_refuses") {
        return channelOutOfRangeRefuses();
    }
    if (name == "shape_is_not_collapsed") {
        return shapeIsNotCollapsed();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_analysis_dataset_tests <case>\n";
        return 2;
    }
    return runCase(argv[1]) ? 0 : 1;
}
