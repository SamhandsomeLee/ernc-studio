#pragma once

#include "ernc/domain/analysis/dataset.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ernc::domain {

/// 一条曲线上的一个点。数值来自数据集的一次读取，本结构自己保存那一次读到的 float。
struct SeriesPoint {
    /// 这条曲线内部的序号，从 0 开始。最后一维上的位置。不是 Hz。
    std::size_t pointIndex{0};

    /// 数据集 values 中的下标，从 0 开始。等于 series * pointCount + pointIndex。
    std::size_t storageIndex{0};

    /// 该存储下标上的 float。NaN 和 Inf 原样保留。没有物理单位。
    float value{0.0F};
};

/// @brief 把数据集按存储顺序拆成多条等长曲线。
///
/// 对象只保存 shape、曲线条数和每条点数，不保存 float，也不延长 AnalysisDataset 的寿命。
/// 最后一维是一条曲线上的点，前面各级的乘积是曲线条数。空 shape 和一维 shape 都是 1 条曲线。
/// 这是存储顺序，不是 Speaker、Mic、Error 或频率轴。证据等级：设计约束。物理轴顺序待确认。
/// 没有线程亲和性，不包含 Qt。
class SeriesMap {
public:
    /// @brief 曲线条数。
    /// @return 至少为 1。构造成功的映射不会是 0 条。
    std::size_t seriesCount() const noexcept;

    /// @brief 每条曲线的点数。
    /// @return 至少为 1。所有曲线点数相同。
    std::size_t pointCount() const noexcept;

    /// @brief 计算一个点的存储下标。
    /// @param series 从 0 开始的曲线号。等于 seriesCount() 时越界。
    /// @param point 从 0 开始的曲线内序号。等于 pointCount() 时越界。
    /// @return 成功时为 series * pointCount + point。越界时为 InvalidParameter，不夹到最后一点。
    Result<std::size_t> storageIndex(std::size_t series, std::size_t point) const;

    /// @brief 从数据集读取一个点的 float。
    /// @param dataset 提供数值的数据集，只在本次调用期间借用。shape 必须和建立映射时相同。
    /// @param series 从 0 开始的曲线号。等于 seriesCount() 时越界。
    /// @param point 从 0 开始的曲线内序号。等于 pointCount() 时越界。
    /// @return 成功时为该点的 float。越界或数据集 shape 不一致时为 InvalidParameter。
    Result<float> value(const AnalysisDataset& dataset, std::size_t series, std::size_t point) const;

    /// @brief 读取一个点的序号、存储下标和数值。
    /// @param dataset 提供数值的数据集，只在本次调用期间借用。shape 必须和建立映射时相同。
    /// @param series 从 0 开始的曲线号。等于 seriesCount() 时越界。
    /// @param point 从 0 开始的曲线内序号。等于 pointCount() 时越界。
    /// @return 成功时调用方拥有 SeriesPoint。越界或数据集 shape 不一致时为 InvalidParameter。
    Result<SeriesPoint> pointAt(const AnalysisDataset& dataset, std::size_t series, std::size_t point) const;

private:
    /// @brief 允许映射函数调用私有构造。
    /// @param dataset 已通过点数检查的数据集，只在本次调用期间借用。
    /// @return 成功时调用方拥有映射。数据集的 shape 无法拆成正的条数和点数时为 InvalidParameter。
    friend Result<SeriesMap> mapStorageSeries(const AnalysisDataset& dataset);

    /// @brief 保存已经拆好的条数和点数。
    /// @param shape 建立映射时的各级长度。本构造函数持有这份向量。
    /// @param seriesCount 曲线条数。必须大于 0。
    /// @param pointCount 每条点数。必须大于 0。
    SeriesMap(std::vector<std::uint32_t> shape, std::size_t seriesCount, std::size_t pointCount);

    /// 建立映射时的 shape。用来拒绝另一份维度不同的数据集。
    std::vector<std::uint32_t> shape_;

    /// 曲线条数。前面各级长度的乘积；不足二维时为 1。
    std::size_t seriesCount_;

    /// 每条曲线的点数。最后一维的长度；空 shape 时为 1。
    std::size_t pointCount_;
};

/// @brief 按存储顺序建立曲线映射。
///
/// 不复制 float，不访问设备。
/// @param dataset 已构造的数据集，只在本次调用期间借用。映射不保管它。
/// @return 成功时调用方拥有 SeriesMap。shape 含 0 或乘积对不上样本数时为 InvalidParameter。
/// 构造成功的 AnalysisDataset 不会走到这个失败。
Result<SeriesMap> mapStorageSeries(const AnalysisDataset& dataset);

}  // namespace ernc::domain
