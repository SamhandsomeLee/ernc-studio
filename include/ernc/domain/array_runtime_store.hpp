#pragma once

#include "ernc/domain/array_state.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/types.hpp"

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ernc::domain {

/// 数组在运行期的元信息。不表示设备已 Verified。
struct ArrayRuntimeMeta {
    /// 还没有成功读过整段数组。
    bool stale{true};

    /// 最近一次整段读成功的时刻。调用方提供。
    std::optional<RuntimeTime> readAt;

    /// 最近一次观察失败。
    std::optional<ErrorCode> error;
};

/// @brief 按参数 id 保存多 slot 同质数组的 Read/Set。
///
/// 与 ParameterRuntimeStore 并列。标量仍进 ParameterRuntimeStore，多 slot 浮点/整型进这里。
class ArrayRuntimeStore {
public:
    /// @brief 借出一份数组状态；不存在时按 count/kind 惰性创建。
    /// @param id 目录主键。
    /// @param count 元素个数，须与布局 slot 数一致。
    /// @param kind 元素种类。
    ArrayState& ensure(const ParameterId& id, std::size_t count, ScalarValue::Kind kind);

    /// @brief 查找已有数组。
    const ArrayState* find(const ParameterId& id) const noexcept;

    /// @brief 查找元信息。
    const ArrayRuntimeMeta* meta(const ParameterId& id) const noexcept;

    /// @brief 写入一次整段设备读数。
    /// @param elements 长度须等于已存在数组的 size；不存在时按长度创建。
    Result<void> observeRead(const ParameterId& id, ScalarValue::Kind kind,
                             const std::vector<ScalarValue>& elements, RuntimeTime readAt);

    /// @brief 记录观察失败。不改已有读数。
    void failObservation(const ParameterId& id, ErrorCode error);

    /// @brief 统计 dirty 元素个数。
    std::size_t dirtyCount(const ParameterId& id) const noexcept;

    /// @brief 是否至少有一个元素读过。
    bool hasAnyRead(const ParameterId& id) const noexcept;

private:
    struct Record {
        ArrayState state;
        ArrayRuntimeMeta meta;
    };

    std::map<std::string, Record> records_;
};

}  // namespace ernc::domain
