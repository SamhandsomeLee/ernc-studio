#pragma once

#include "ernc/domain/catalog.hpp"

namespace ernc::domain {

/// 参数动作的类别。本阶段只计算类别，不执行写入。
enum class ActuationKind {
    /// 普通数值或开关写入。以后可以进入批量写入计划。
    NormalWrite,

    /// 模式切换。和普通批量写入分开，也不是一次性脉冲。
    ModeChange,

    /// 一次性触发。RNC_refresh 属于这一类。
    OneShot,

    /// 只读或权限未声明。不能当作写入动作。
    NotActuated
};

/// 规则启用后才会生效的动作策略。字段描述的是预留规则，不是当前已经拦截的行为。
struct ActuationPolicy {
    /// 按 access 和 editor 分出的类别。
    ActuationKind kind{ActuationKind::NotActuated};

    /// 规则启用后，能否进入普通批量写入计划。一次性触发、模式切换和不可作动为 false。
    bool includeInBulkWritePlan{false};

    /// 规则启用后，命令策略是否允许通用重试。一次性触发和模式切换为 false。
    bool allowGenericRetry{false};

    /// 规则启用后，能否向真实设备执行。一次性触发在车速映射确认前为 false。
    bool realWritePermitted{false};
};

/// @brief 报告命令策略是否已经启用。
///
/// 当前固定返回 false。分类结果已经算好，但调用方不能用它拦截批量写入或打开重试。
/// @return 本阶段为 false。
constexpr bool commandPolicyEnforced() noexcept {
    return false;
}

/// @brief 按目录里的 access 和 editor 计算预留策略。
///
/// 不按参数名分支，不读车速，不写 1 和 0，也不修改 RuntimeStore。definition 只在本次调用期间借用。
/// @param definition 目录中的一条参数。access 缺省表示权限未声明。
/// @return 按值返回预留策略。调用方拥有这份副本。
ActuationPolicy classifyActuation(const ParameterDefinition& definition);

/// @brief 当前是否要把该动作排除出批量写入计划。
///
/// 策略未启用时恒为 false，预留规则先不拦截。policy 只在本次调用期间借用。
/// @param policy classifyActuation 的结果。
/// @return 策略已启用且 includeInBulkWritePlan 为 false 时为 true。
bool excludedFromBulkWrite(const ActuationPolicy& policy) noexcept;

/// @brief 当前是否允许通用重试。
///
/// 策略未启用时恒为 false，本阶段不打开任何通用重试。policy 只在本次调用期间借用。
/// @param policy classifyActuation 的结果。
/// @return 策略已启用且 allowGenericRetry 为 true 时为 true。
bool genericRetryPermitted(const ActuationPolicy& policy) noexcept;

}  // namespace ernc::domain
