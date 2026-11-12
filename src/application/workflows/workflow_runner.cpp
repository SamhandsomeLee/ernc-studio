#include "ernc/application/workflows/workflow_runner.hpp"

namespace ernc::application {

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::WorkflowBlockReason;
using ernc::domain::WorkflowDefinition;
using ernc::domain::WorkflowGateContext;
using ernc::domain::WorkflowRunPhase;
using ernc::domain::WorkflowRunState;
using ernc::domain::WorkflowStepDefinition;
using ernc::domain::WorkflowStepPhase;
using ernc::domain::WorkflowStepRun;
using ernc::domain::evaluateWorkflowStepGates;

/// @brief 当前下标是否落在定义范围内。
bool validIndex(const WorkflowRunState& state) {
    return state.activeStepIndex < state.definition.steps.size()
           && state.activeStepIndex < state.stepRuns.size();
}

/// @brief 将活动步标为 Blocked。
void markBlocked(WorkflowRunState& state, WorkflowBlockReason reason) {
    if (!validIndex(state)) {
        return;
    }
    WorkflowStepRun& run = state.stepRuns[state.activeStepIndex];
    run.phase = WorkflowStepPhase::Blocked;
    run.blockReason = reason;
}

/// @brief 完成活动步并激活下一步或结束运行。
void completeActiveStep(WorkflowRunState& state) {
    if (!validIndex(state)) {
        return;
    }
    WorkflowStepRun& run = state.stepRuns[state.activeStepIndex];
    run.phase = WorkflowStepPhase::Completed;
    run.blockReason = WorkflowBlockReason::None;
    ++state.completedCount;

    const std::size_t next = state.activeStepIndex + 1;
    if (next >= state.definition.steps.size()) {
        state.runPhase = WorkflowRunPhase::Completed;
        return;
    }
    state.activeStepIndex = next;
    WorkflowStepRun& nextRun = state.stepRuns[next];
    nextRun.phase = WorkflowStepPhase::Active;
    nextRun.blockReason = WorkflowBlockReason::None;
}

}  // namespace

Result<WorkflowRunState> WorkflowRunner::start(WorkflowDefinition definition) {
    if (definition.steps.empty()) {
        return Result<WorkflowRunState>::failure(ErrorCode::InvalidParameter);
    }
    WorkflowRunState state;
    state.definition = std::move(definition);
    state.runPhase = WorkflowRunPhase::Running;
    state.stepRuns.reserve(state.definition.steps.size());
    for (std::size_t index = 0; index < state.definition.steps.size(); ++index) {
        WorkflowStepRun run;
        run.stepId = state.definition.steps[index].id;
        run.phase = index == 0 ? WorkflowStepPhase::Active : WorkflowStepPhase::Pending;
        state.stepRuns.push_back(std::move(run));
    }
    state.activeStepIndex = 0;
    state.completedCount = 0;
    return Result<WorkflowRunState>::success(std::move(state));
}

Result<WorkflowRunState> WorkflowRunner::tryAdvance(WorkflowRunState state, const WorkflowGateContext& gates) {
    if (state.runPhase != WorkflowRunPhase::Running || !validIndex(state)) {
        return Result<WorkflowRunState>::failure(ErrorCode::InvalidParameter);
    }
    WorkflowStepRun& run = state.stepRuns[state.activeStepIndex];
    if (run.phase == WorkflowStepPhase::Blocked) {
        return Result<WorkflowRunState>::success(std::move(state));
    }
    if (run.phase != WorkflowStepPhase::Active) {
        return Result<WorkflowRunState>::failure(ErrorCode::InvalidParameter);
    }
    const WorkflowStepDefinition& step = state.definition.steps[state.activeStepIndex];
    if (const std::optional<WorkflowBlockReason> reason = evaluateWorkflowStepGates(step, run.userConfirmed, gates)) {
        markBlocked(state, *reason);
        return Result<WorkflowRunState>::success(std::move(state));
    }
    completeActiveStep(state);
    return Result<WorkflowRunState>::success(std::move(state));
}

Result<WorkflowRunState> WorkflowRunner::confirmCurrentStep(WorkflowRunState state) {
    if (state.runPhase != WorkflowRunPhase::Running || !validIndex(state)) {
        return Result<WorkflowRunState>::failure(ErrorCode::InvalidParameter);
    }
    WorkflowStepRun& run = state.stepRuns[state.activeStepIndex];
    if (run.phase != WorkflowStepPhase::Active && run.phase != WorkflowStepPhase::Blocked) {
        return Result<WorkflowRunState>::failure(ErrorCode::InvalidParameter);
    }
    const WorkflowStepDefinition& step = state.definition.steps[state.activeStepIndex];
    if (!step.requiresUserConfirm) {
        return Result<WorkflowRunState>::failure(ErrorCode::InvalidParameter);
    }
    run.userConfirmed = true;
    if (run.phase == WorkflowStepPhase::Blocked
        && run.blockReason == WorkflowBlockReason::UserConfirmationRequired) {
        run.phase = WorkflowStepPhase::Active;
        run.blockReason = WorkflowBlockReason::None;
    }
    return Result<WorkflowRunState>::success(std::move(state));
}

Result<WorkflowRunState> WorkflowRunner::resume(WorkflowRunState state, const WorkflowGateContext& gates) {
    if (state.runPhase != WorkflowRunPhase::Running || !validIndex(state)) {
        return Result<WorkflowRunState>::failure(ErrorCode::InvalidParameter);
    }
    WorkflowStepRun& run = state.stepRuns[state.activeStepIndex];
    if (run.phase != WorkflowStepPhase::Blocked) {
        return Result<WorkflowRunState>::failure(ErrorCode::InvalidParameter);
    }
    const WorkflowStepDefinition& step = state.definition.steps[state.activeStepIndex];
    if (const std::optional<WorkflowBlockReason> reason = evaluateWorkflowStepGates(step, run.userConfirmed, gates)) {
        run.blockReason = *reason;
        return Result<WorkflowRunState>::success(std::move(state));
    }
    run.phase = WorkflowStepPhase::Active;
    run.blockReason = WorkflowBlockReason::None;
    return Result<WorkflowRunState>::success(std::move(state));
}

Result<WorkflowRunState> WorkflowRunner::cancel(WorkflowRunState state) {
    if (state.runPhase != WorkflowRunPhase::Running) {
        return Result<WorkflowRunState>::failure(ErrorCode::InvalidParameter);
    }
    state.runPhase = WorkflowRunPhase::Cancelled;
    if (validIndex(state)) {
        WorkflowStepRun& run = state.stepRuns[state.activeStepIndex];
        if (run.phase != WorkflowStepPhase::Completed) {
            run.phase = WorkflowStepPhase::Blocked;
            run.blockReason = WorkflowBlockReason::Cancelled;
        }
    }
    return Result<WorkflowRunState>::success(std::move(state));
}

}  // namespace ernc::application
