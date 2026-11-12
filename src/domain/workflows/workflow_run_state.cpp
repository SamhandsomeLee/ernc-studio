#include "ernc/domain/workflows/workflow_run_state.hpp"

namespace ernc::domain {

std::optional<WorkflowBlockReason> evaluateWorkflowStepGates(const WorkflowStepDefinition& step, bool userConfirmed,
                                                             const WorkflowGateContext& gates) {
    if (step.requiresConfirmedFormula && !gates.formulaConfirmed) {
        return WorkflowBlockReason::UnconfirmedFormula;
    }
    if (step.requiresUserConfirm && !userConfirmed) {
        return WorkflowBlockReason::UserConfirmationRequired;
    }
    const bool needsLink = step.requiresDeviceRead || step.permitsWrite;
    if (needsLink && !gates.deviceConnected) {
        return WorkflowBlockReason::DeviceDisconnected;
    }
    if (step.permitsWrite && gates.writeForbidden) {
        return WorkflowBlockReason::ReadOnlyViolation;
    }
    return std::nullopt;
}

WorkflowProgress workflowProgress(const WorkflowRunState& state) {
    return WorkflowProgress{
        state.runPhase,
        state.activeStepIndex,
        state.completedCount,
        state.definition.steps.size(),
    };
}

}  // namespace ernc::domain
