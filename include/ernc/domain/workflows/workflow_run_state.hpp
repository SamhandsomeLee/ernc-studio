#pragma once

#include "ernc/domain/workflows/block_reason.hpp"
#include "ernc/domain/workflows/workflow_definition.hpp"
#include "ernc/domain/workflows/workflow_gate_context.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace ernc::domain {

/// 单步在运行中的阶段。
enum class WorkflowStepPhase {
    /// 尚未轮到。
    Pending,

    /// 当前活动步；可能已收到用户确认但尚未 tryAdvance。
    Active,

    /// 本步已完成，结果已确认。
    Completed,

    /// 门禁未通过，等待 resume 或用户动作。
    Blocked
};

/// 一步的运行记录。
struct WorkflowStepRun {
    /// 与 definition.steps[i].id 对应。
    std::string stepId;

    WorkflowStepPhase phase{WorkflowStepPhase::Pending};

    /// phase 为 Blocked 时的原因。
    WorkflowBlockReason blockReason{WorkflowBlockReason::None};

    /// requiresUserConfirm 为 true 时，confirmCurrentStep 置为 true。
    bool userConfirmed{false};
};

/// 整次运行的阶段。
enum class WorkflowRunPhase {
    NotStarted,
    Running,
    Completed,
    Cancelled
};

/// 工作流运行态。值语义，可由 Application 在主线程或测试中拷贝。
struct WorkflowRunState {
    /// 启动时复制的定义。
    WorkflowDefinition definition;

    WorkflowRunPhase runPhase{WorkflowRunPhase::NotStarted};

    /// 与 definition.steps 等长的运行记录。
    std::vector<WorkflowStepRun> stepRuns;

    /// 当前活动步下标；Completed 或 Cancelled 时仍可指向最后活动步。
    std::size_t activeStepIndex{0};

    /// 已完成步数（phase == Completed）。
    std::size_t completedCount{0};
};

/// @brief 根据步骤定义和门禁上下文计算阻断原因。
///
/// 不修改 state。userConfirmed 对应当前步的运行记录。
/// @param step 当前步定义，只在本次调用期间借用。
/// @param userConfirmed 当前步是否已收到用户确认。
/// @param gates 外部事实，只在本次调用期间借用。
/// @return 无阻断时为 nullopt；否则为首个失败的 BlockReason（按确认公式、用户确认、设备、只读顺序）。
std::optional<WorkflowBlockReason> evaluateWorkflowStepGates(const WorkflowStepDefinition& step, bool userConfirmed,
                                                             const WorkflowGateContext& gates);

/// @brief 已完成与已取消运行的进度摘要。
/// @param state 当前运行态，只在本次调用期间借用。
/// @return runPhase、activeStepIndex、completedCount 与总步数。
struct WorkflowProgress {
    WorkflowRunPhase runPhase{WorkflowRunPhase::NotStarted};
    std::size_t activeStepIndex{0};
    std::size_t completedCount{0};
    std::size_t totalSteps{0};
};

/// @brief 从运行态提取进度。
/// @param state 当前运行态，只在本次调用期间借用。
/// @return 调用方拥有的进度副本。
WorkflowProgress workflowProgress(const WorkflowRunState& state);

}  // namespace ernc::domain
