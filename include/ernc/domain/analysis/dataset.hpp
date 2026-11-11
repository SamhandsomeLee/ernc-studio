#pragma once

#include "ernc/domain/region_decode.hpp"
#include "ernc/domain/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ernc::domain {

/// @brief 一份只读的分析样本。
///
/// 调用方拥有对象。对象拥有路径、shape 和 float 的副本，不借用 DecodedField。
/// 构造成功之后没有修改数值的接口。没有线程亲和性，不包含 Qt。
/// 多个线程可以同时读取同一份 const 对象。它不是设备 Read、用户 Set，也不是绘图控件里的缓存。
/// shape 只保留目录给出的各级长度，不命名物理轴。证据等级：设计约束。频率轴和 Speaker/Mic/Error 仍待确认。
class AnalysisDataset {
public:
    /// @brief 从已解码字段拷贝一份数据集。
    ///
    /// 不访问设备，也不重新解码字节。
    /// @param field 已解码字段，只在本次调用期间借用。path、shape、values 会被拷贝。
    /// @return 成功时调用方拥有独立副本。空 shape 必须正好有 1 个 float。
    /// 非空 shape 的每一维都要大于 0，乘积必须等于 values 的个数。
    /// 乘积溢出 64 位，或点数对不上时为 InvalidParameter，不返回半份数据。
    /// NaN 和 Inf 按原位模式保留。
    static Result<AnalysisDataset> fromDecoded(const DecodedField& field);

    /// @brief 字段路径。
    /// @return 拷贝自 DecodedField 的路径。本对象不检查它是否属于 ERNC_TF 或 check_data。
    /// 引用指向内部字符串，不转移所有权，只在本对象存活期间有效。
    const std::string& path() const noexcept;

    /// @brief 各级长度。
    /// @return 拷贝自 DecodedField 的 shape。空表示这一个 float 没有数组维度。
    /// 引用指向内部向量，不转移所有权，只在本对象存活期间有效。
    const std::vector<std::uint32_t>& shape() const noexcept;

    /// @brief 样本个数。
    /// @return values 的个数，等于存储下标的合法范围。0 不会出现在构造成功的对象上。
    std::size_t sampleCount() const noexcept;

    /// @brief 读取一个存储下标上的 float。
    /// @param storageIndex 从 0 开始的存储下标。等于 sampleCount() 时越界。它不是频率，也不是通道号。
    /// @return 成功时为该位置的 float，按值返回。NaN 和 Inf 原样返回。越界时为 InvalidParameter。
    Result<float> valueAt(std::size_t storageIndex) const;

private:
    /// @brief 装入已经通过点数检查的副本。
    /// @param path 字段路径。本构造函数持有这份字符串。
    /// @param shape 各级长度。本构造函数持有这份向量。
    /// @param values 存储顺序的 float。本构造函数持有这份向量。
    AnalysisDataset(std::string path, std::vector<std::uint32_t> shape, std::vector<float> values);

    /// 字段路径。构造后不变。
    std::string path_;

    /// 各级长度。构造后不变。空表示一个标量 float。
    std::vector<std::uint32_t> shape_;

    /// 存储顺序的 float。下标 0 是字节序列中的第一个值。构造后不变。
    std::vector<float> values_;
};

}  // namespace ernc::domain
