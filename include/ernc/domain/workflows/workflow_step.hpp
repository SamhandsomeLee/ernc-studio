#pragma once

#include <string>

namespace ernc::domain {

/// 工作流定义中的一步。只描述元数据，不执行设备 I/O。
struct WorkflowStepDefinition {
    /// 步骤标识，在同一工作流内唯一。空 id 在载入时应被 Validator 拒绝（ES-090 测试使用合法 id）。
    std::string id;

    /// 展示标题。
    std::string title;

    /// 操作说明。可引用 Excel 备注，但不解析公式。
    std::string description;

    /// 为 true 时，deviceConnected 为 false 则不能完成本步。
    bool requiresDeviceRead{false};

    /// 为 true 时，必须先 confirmCurrentStep，不能直接 tryAdvance 完成。
    bool requiresUserConfirm{false};

    /// 为 true 时，formulaConfirmed 为 false 时保持 Blocked，不能 resume 越过。
    bool requiresConfirmedFormula{false};

    /// 为 true 时，本步语义包含写入；仍受 requiresUserConfirm 和设备连接约束。
    bool permitsWrite{false};
};

}  // namespace ernc::domain
