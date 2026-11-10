#pragma once

#include "ernc/domain/types.hpp"

#include <chrono>
#include <cstddef>
#include <map>
#include <optional>
#include <string>

namespace ernc::domain {

/// 运行状态使用的时刻。由调用方传入，Store 不读取系统钟，因此测试可以给出固定时刻。
using RuntimeTime = std::chrono::system_clock::time_point;

/// 一个参数在内存中的运行状态。Store 拥有这份副本，不表示设备已经确认或读回验证。
struct ParameterRuntimeState {
    /// 最近一次成功读回的标量。空表示还没有成功读回。它不是用户目标值。
    std::optional<ScalarValue> deviceValue;

    /// 用户准备写入的标量。空表示用户没有暂存目标。读回不会写入这里。
    std::optional<ScalarValue> stagedValue;

    /// 用户有尚未提交的目标值。读回和离线都不把它改回 false。
    bool dirty{false};

    /// 设备读数已经过期。新建记录为 true。成功读回后为 false。离线后已有记录重新变为 true。
    bool stale{true};

    /// 最近一次成功读回的时刻。没有成功读回时为空。单位是系统钟的时间点，不是采样周期。
    std::optional<RuntimeTime> readAt;

    /// 最近一次用户暂存的时刻。没有暂存时为空。它不是设备写入完成的时刻。
    std::optional<RuntimeTime> stagedAt;

    /// 最近一次观察失败的类别。空表示当前没有未清除的观察错误。成功读回会清掉它。
    std::optional<ErrorCode> error;
};

/// @brief 按参数标识保存设备读数和用户目标值。
///
/// 对象拥有全部状态。find 返回的指针指向内部记录，插入、离线以外的修改或 Store 被销毁后失效。
/// 没有后台线程，也不包含 QWidget。调用方负责不在修改本对象的同时从另一个线程读取它。
class ParameterRuntimeStore {
public:
    /// @brief 按参数标识借出状态。
    /// @param id 参数标识，只在本次调用期间借用。Store 用它的文本比较，不查询目录。
    /// @return 已有记录时指向内部状态，不转移所有权。没有记录时为空指针。
    const ParameterRuntimeState* find(const ParameterId& id) const noexcept;

    /// @brief 记录一次成功读回。
    /// @param id 参数标识，只在本次调用期间借用。文本会被复制为内部键。
    /// @param value 设备读回的标量，按值复制进 deviceValue。不换算单位，也不改写种类。
    /// @param readAt 这次读回的时刻，由调用方给出。Store 不读取系统钟。
    /// 写入 deviceValue 和 readAt，清掉 stale 和 error。不改 stagedValue、dirty 和 stagedAt。
    void observe(const ParameterId& id, const ScalarValue& value, RuntimeTime readAt);

    /// @brief 记录一次没有得到设备值的观察。
    /// @param id 参数标识，只在本次调用期间借用。文本会被复制为内部键。
    /// @param error 失败类别。Unknown 也表示这次观察失败，不表示成功。
    /// 只写入 error。不改 deviceValue、stagedValue、dirty、stale 和两个时刻。
    void failObservation(const ParameterId& id, ErrorCode error);

    /// @brief 记录用户准备写入的目标值。
    /// @param id 参数标识，只在本次调用期间借用。文本会被复制为内部键。
    /// @param value 用户目标标量，按值复制进 stagedValue。
    /// @param stagedAt 这次暂存的时刻，由调用方给出。它不是设备 ACK 或写入完成时刻。
    /// 写入 stagedValue 和 stagedAt，并把 dirty 设为 true。不改 deviceValue、stale、readAt 和 error。
    void stage(const ParameterId& id, const ScalarValue& value, RuntimeTime stagedAt);

    /// @brief 把已有记录标成过期。
    /// 数值、dirty、时刻和 error 保持原样。没有记录时不新建。
    void markOffline() noexcept;

    /// @brief 已保存的参数个数。
    /// @return 内部记录数。0 表示还没有任何观察或暂存。
    std::size_t size() const noexcept;

private:
    /// @brief 取出或新建一条记录。
    /// @param id 参数标识，只在本次调用期间借用。
    /// @return 内部记录的引用。新建时 deviceValue 和 stagedValue 为空，dirty 为 false，stale 为 true。
    ParameterRuntimeState& slot(const ParameterId& id);

    /// 按参数标识文本保存的运行状态。键是 id.text() 的副本。
    std::map<std::string, ParameterRuntimeState> records_;
};

}  // namespace ernc::domain
