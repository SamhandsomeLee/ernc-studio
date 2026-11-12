#pragma once

#include "ernc/domain/workflows/workflow_step.hpp"

#include <string>
#include <vector>

namespace ernc::domain {

/// 不可变的工作流蓝图。Domain 不持有 Qt 或设备资源。
struct WorkflowDefinition {
    /// 工作流标识，例如 "threshold.synthetic"。
    std::string id;

    /// 有序步骤列表。至少一步；空定义由 Runner::start 拒绝。
    std::vector<WorkflowStepDefinition> steps;
};

}  // namespace ernc::domain
