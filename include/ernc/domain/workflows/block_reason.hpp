#pragma once

namespace ernc::domain {

/// 工作流步骤无法继续时的阻断原因。None 表示当前没有阻断。
enum class WorkflowBlockReason {
    /// 当前步骤可以推进。
    None,

    /// 步骤需要设备读或写，但会话未连接。
    DeviceDisconnected,

    /// 步骤需要用户显式确认，但尚未调用 confirm。
    UserConfirmationRequired,

    /// 步骤依赖尚未确认的阈值公式或映射，不能自动推进。
    UnconfirmedFormula,

    /// 步骤声明可写，但上下文标记为只读违规（调用方传入）。
    ReadOnlyViolation,

    /// 外部依赖未满足（例如映射未解析）。
    UnresolvedDependency,

    /// 用户或调用方取消了本次运行。
    Cancelled
};

}  // namespace ernc::domain
