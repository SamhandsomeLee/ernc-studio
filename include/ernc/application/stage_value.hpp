#pragma once

#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

namespace ernc::application {

/// @brief 把用户编辑写入暂存值。
///
/// 借用目录、布局和 Store，不拥有它们。这些对象必须比本对象活得久。
/// 不接收调度器，不入队 FC03 或 FC16，也不读取系统钟。
/// 没有后台线程，也不是 QObject。调用必须和 Store 在同一个线程。
/// 只有 1 个 slot 的标量可以暂存。数组和结构体整段留在 Store 外面。
class ParameterStager {
public:
    /// @brief 绑定目录、布局和运行状态。
    /// @param catalog 参数目录，只借用。
    /// @param layout 固件布局，只借用。几何来自 locate()。
    /// @param store 运行状态，只借用。校验通过后由 stage 调用 store.stage。
    ParameterStager(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                    ernc::domain::ParameterRuntimeStore& store);

    /// @brief 禁止复制。借用的对象只有一份。
    ParameterStager(const ParameterStager&) = delete;

    /// @brief 禁止复制赋值。
    ParameterStager& operator=(const ParameterStager&) = delete;

    /// @brief 禁止移动。引用成员不能改绑。
    ParameterStager(ParameterStager&&) = delete;

    /// @brief 禁止移动赋值。
    ParameterStager& operator=(ParameterStager&&) = delete;

    /// @brief 校验后暂存一个标量。
    ///
    /// 先查目录，再调用 validateStagedValue。通过后还要求 locate 得到 1 个 slot、4 字节和标量类型。
    /// 任一步失败时不调用 store.stage，已有记录保持原样。
    /// @param id 参数标识，只在本次调用期间借用。
    /// @param value 用户目标标量，只在本次调用期间借用。种类必须和目录声明一致。没有物理单位。
    /// @param stagedAt 这次暂存的时刻，由调用方给出。它不是设备 ACK 或写入完成时刻。
    /// @return 成功时没有载荷。Store 里该参数的 stagedValue、stagedAt 已更新，dirty 为 true。
    /// deviceValue、stale、readAt 和 error 保持调用前的值。
    /// 目录没有这条参数、只读、命令、种类不符、浮点非有限、不是 1 个标量 slot 时失败，Store 不变。
    /// 只读返回 ReadOnlyViolation。其余拒绝返回 InvalidParameter。
    ernc::domain::Result<void> stage(const ernc::domain::ParameterId& id, const ernc::domain::ScalarValue& value,
                                     ernc::domain::RuntimeTime stagedAt);

private:
    /// 参数目录。本对象不拥有。
    const ernc::domain::ParameterCatalog& catalog_;

    /// 固件布局。本对象不拥有。
    const ernc::domain::FirmwareLayout& layout_;

    /// 运行状态。只有校验和几何都通过时才修改它。
    ernc::domain::ParameterRuntimeStore& store_;
};

}  // namespace ernc::application
