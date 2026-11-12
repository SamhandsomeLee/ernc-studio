#pragma once

#include "ernc/domain/types.hpp"
#include "ernc/domain/workflows/workflow_definition.hpp"
#include "ernc/domain/workflows/workflow_gate_context.hpp"
#include "ernc/domain/workflows/workflow_run_state.hpp"

namespace ernc::application {

/// 无 Qt 的工作流状态迁移。不访问串口、Store 或 ParameterWriter。
class WorkflowRunner {
public:
    /// @brief 从定义启动一次新运行。
    /// @param definition 至少包含一步的工作流蓝图。调用方拥有 definition。
    /// @return 第一步为 Active 的 Running 状态；空 steps 时为 InvalidParameter。
    static ernc::domain::Result<ernc::domain::WorkflowRunState> start(ernc::domain::WorkflowDefinition definition);

    /// @brief 尝试完成当前活动步并进入下一步。
    ///
    /// 当前步门禁未通过时，将该步标为 Blocked 并返回更新后的状态（非 Error）。
    /// 已在 Blocked 时再次调用不会自动跳过；应先 resume 或满足 confirm。
    /// @param state 当前运行态。按值传入，返回新副本。
    /// @param gates 设备与公式确认事实。
    /// @return 新运行态；Completed/Cancelled/NotStarted 上调用时为 InvalidParameter。
    static ernc::domain::Result<ernc::domain::WorkflowRunState> tryAdvance(ernc::domain::WorkflowRunState state,
                                                                          const ernc::domain::WorkflowGateContext& gates);

    /// @brief 对当前活动步记录用户确认。
    /// @param state 当前运行态。按值传入。
    /// @return 当前步 userConfirmed 为 true 的新状态；无活动步或已结束时为 InvalidParameter。
    static ernc::domain::Result<ernc::domain::WorkflowRunState> confirmCurrentStep(
        ernc::domain::WorkflowRunState state);

    /// @brief 在阻断条件清除后，将 Blocked 步恢复为 Active。
    ///
    /// 不自动完成该步。UnconfirmedFormula 在 formulaConfirmed 仍为 false 时保持 Blocked。
    /// @param state 当前运行态。按值传入。
    /// @param gates 最新门禁事实。
    /// @return 新运行态；当前步不是 Blocked 时为 InvalidParameter。
    static ernc::domain::Result<ernc::domain::WorkflowRunState> resume(ernc::domain::WorkflowRunState state,
                                                                      const ernc::domain::WorkflowGateContext& gates);

    /// @brief 取消运行。
    /// @param state 当前运行态。按值传入。
    /// @return runPhase 为 Cancelled 的状态；已 Completed 时为 InvalidParameter。
    static ernc::domain::Result<ernc::domain::WorkflowRunState> cancel(ernc::domain::WorkflowRunState state);
};

}  // namespace ernc::application
