#pragma once

#include "ernc/domain/command_policy.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <string>
#include <vector>

namespace ernc::domain {

/// 常规 WritePlan 的范围。Selected 使用选中的 id，Category 使用目录分类。
enum class WriteScopeKind {
    /// 只考虑 selected 里的参数，顺序与选择顺序一致。
    Selected,

    /// 只考虑 category 与目录分类相同的参数，顺序与目录文件一致。
    Category
};

/// 调用方给出的计划范围。Store 和目录都不由这里拥有。
struct WriteScope {
    /// 选择范围的种类。
    WriteScopeKind kind{WriteScopeKind::Selected};

    /// kind 为 Selected 时使用的参数标识。调用方拥有这份列表。
    std::vector<ParameterId> selected;

    /// kind 为 Category 时使用的分类名。空字符串不匹配任何参数。
    std::string category;
};

/// 从常规计划中排除的原因。
enum class WriteExclusion {
    /// access 为 read_only。Step 属于这一类。
    ReadOnly,

    /// access 为 command，或 editor 为 momentary_command。RNC_refresh 属于这一类。
    Command,

    /// 整段区域。没有标量 data_type，或 shape/editor 标明为 region。
    Blob,

    /// 权限未声明，或不是普通写入。
    Unsupported,

    /// 固件路径缺失、不在布局中，或几何越界。pack4-sequential 的已有路径不使用这个原因。
    Unverified
};

/// 一条可以出现在常规计划里的脏参数预览。
struct PlannedWrite {
    /// 参数标识的副本。
    ParameterId id;

    /// 目录中的固件路径。不是 slot 或寄存器地址。
    std::string firmwarePath;

    /// 计划生成时的设备读数。没有成功读回时为空。
    std::optional<ScalarValue> deviceValue;

    /// 计划生成时的用户目标值。生成计划不会改掉 Store 里的原值。
    ScalarValue stagedValue;

    /// 目录声明的标量类型。区域参数不会进入可写项。
    std::optional<DataType> dataType;

    /// 生成计划时设备读数是否已过期。过期仍可预览，但不能当成当前设备值。
    bool stale{false};
};

/// 一条留在常规计划外面的脏参数。
struct RejectedWrite {
    /// 参数标识的副本。
    ParameterId id;

    /// 排除原因。
    WriteExclusion reason{WriteExclusion::Unverified};

    /// 生成计划时设备读数是否已过期。
    bool stale{false};
};

/// 一次常规写入预览。对象拥有全部条目，不表示已经向设备发送写入。
struct WritePlan {
    /// 通过排除规则、可以进入常规计划的脏参数。
    std::vector<PlannedWrite> included;

    /// 范围内的脏参数中被排除的条目。
    std::vector<RejectedWrite> excluded;
};

/// @brief 计算选中参数或当前分类的常规写入预览。
///
/// 候选只包括范围内 dirty 且已有 stagedValue 的参数。ReadOnly、Command、Blob，以及不在四字节布局中的路径进入 excluded。
/// pack4-sequential 是 param.md 从地址 0 按四字节对齐得到的布局，不因此排除。
/// 不调用 excludedFromBulkWrite，不修改 Store，不创建 Modbus 帧，也不打开通用重试。
/// catalog、store、layout 和 scope 只在本次调用期间借用。
/// @param catalog 参数目录。
/// @param store 运行状态。
/// @param layout 固件布局。路径不在字段树中或几何越界时进入 excluded。pack4-sequential 的已有路径可以进入可写预览。
/// @param scope 选中列表或分类名。
/// @return 按值返回预览。调用方拥有这份副本。
WritePlan buildWritePlan(
    const ParameterCatalog& catalog,
    const ParameterRuntimeStore& store,
    const FirmwareLayout& layout,
    const WriteScope& scope);

}  // namespace ernc::domain
