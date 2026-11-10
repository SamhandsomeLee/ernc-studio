#include "ernc/domain/command_policy.hpp"

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

/// @brief 做成普通写入的预留策略。
/// @return 可以进入批量计划。命令策略不禁止它以后使用通用重试。真实写入许可留给后续写入切片。
ActuationPolicy normalWrite() {
    return ActuationPolicy{ActuationKind::NormalWrite, true, true, true};
}

/// @brief 做成模式切换的预留策略。
/// @return 不进普通批量计划，不使用通用重试。
ActuationPolicy modeChange() {
    return ActuationPolicy{ActuationKind::ModeChange, false, false, true};
}

/// @brief 做成一次性触发的预留策略。
/// @return 不进批量计划，不允许通用重试。车速映射未确认，真实执行保持关闭。
ActuationPolicy oneShot() {
    return ActuationPolicy{ActuationKind::OneShot, false, false, false};
}

/// @brief 做成不可作动的预留策略。
/// @return 三项许可都是 false。
ActuationPolicy notActuated() {
    return ActuationPolicy{ActuationKind::NotActuated, false, false, false};
}

}  // namespace

ActuationPolicy classifyActuation(const ParameterDefinition& definition) {
    const bool momentary = definition.editor && *definition.editor == EditorKind::MomentaryCommand;
    const bool modeSelector = definition.editor && *definition.editor == EditorKind::ModeSelector;
    if (!definition.access || *definition.access == AccessMode::ReadOnly) {
        return notActuated();
    }
    if (*definition.access == AccessMode::Command || momentary) {
        return oneShot();
    }
    if (modeSelector) {
        return modeChange();
    }
    if (*definition.access == AccessMode::ReadWrite || *definition.access == AccessMode::WriteOnly) {
        return normalWrite();
    }
    return notActuated();
}

bool excludedFromBulkWrite(const ActuationPolicy& policy) noexcept {
    return commandPolicyEnforced() && !policy.includeInBulkWritePlan;
}

bool genericRetryPermitted(const ActuationPolicy& policy) noexcept {
    return commandPolicyEnforced() && policy.allowGenericRetry;
}

}  // namespace ernc::domain
