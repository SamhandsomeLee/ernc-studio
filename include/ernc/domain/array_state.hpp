#pragma once

#include "ernc/domain/types.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace ernc::domain {

/// 数组中一个下标的读数和用户目标。下标从 0 开始，不是 Modbus 寄存器地址。
struct ArrayElement {
    /// 该下标最近一次读入的标量。空表示还没有读数。它不是用户目标。
    std::optional<ScalarValue> read;

    /// 该下标准备写入的标量。空表示用户没有目标。读入不会写到这里。
    std::optional<ScalarValue> set;

    /// 该下标的 Set 尚未通过 reset 收回。其它下标的修改不改变这一位。
    bool dirty{false};
};

/// @brief 保存同一种标量的数组元素。
///
/// 对象拥有全部元素。element 返回的指针指向内部记录，ArrayState 被移动或销毁后失效。
/// 不读取时钟，不访问设备，也不包含 Qt。调用方负责不在修改本对象的同时从另一个线程读取它。
class ArrayState {
public:
    /// @brief 按元素个数和标量种类建立空数组。
    /// @param count 元素个数。0 表示合法的空数组，不是失败。
    /// @param kind 每个元素必须使用的标量种类。float、int32 和 uint32 互不转换。
    /// @note 每个 Read 和 Set 开始都为空，dirty 为 false。
    ArrayState(std::size_t count, ScalarValue::Kind kind);

    /// @brief 元素个数。
    /// @return 构造时给出的个数。不会因读写改变。
    std::size_t size() const noexcept;

    /// @brief 数组要求的标量种类。
    /// @return 构造时给出的种类。
    ScalarValue::Kind kind() const noexcept;

    /// @brief 写入一个下标的设备读数。
    /// @param index 从 0 开始的元素下标。等于 size() 时越界。
    /// @param value 读入的标量，按值复制。种类必须和数组一致。
    /// @return 成功时没有载荷。只改该下标的 Read。不改 Set 和 dirty。
    /// 越界或种类不一致时为 InvalidParameter，数组不变。
    Result<void> setRead(std::size_t index, const ScalarValue& value);

    /// @brief 写入一个下标的用户目标。
    /// @param index 从 0 开始的元素下标。等于 size() 时越界。
    /// @param value 用户目标，按值复制。种类必须和数组一致。
    /// @return 成功时没有载荷。只改该下标的 Set，并把该下标标成 dirty。其它下标不变。
    /// 越界或种类不一致时为 InvalidParameter，数组不变。
    Result<void> setValue(std::size_t index, const ScalarValue& value);

    /// @brief 一次把每个 Set 收回对应的 Read。
    /// @note 有 Read 的下标，Set 变成那份 Read。没有 Read 的下标，Set 清空。全部 dirty 变为 false。
    /// 不访问设备。
    void reset();

    /// @brief 借出一个下标的元素。
    /// @param index 从 0 开始的元素下标。
    /// @return 下标有效时指向内部元素，不转移所有权。越界时为空指针。
    const ArrayElement* element(std::size_t index) const noexcept;

private:
    /// 每个元素必须使用的种类。构造后不变。
    ScalarValue::Kind kind_;

    /// 按下标排列的元素。本对象拥有。
    std::vector<ArrayElement> elements_;
};

}  // namespace ernc::domain
