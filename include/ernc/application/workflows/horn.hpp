#pragma once

#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/region_decode.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/workflows/workflow_definition.hpp"
#include "ernc/domain/workflows/workflow_gate_context.hpp"
#include "ernc/domain/workflows/workflow_run_state.hpp"

#include <optional>
#include <span>
#include <string_view>

namespace ernc::application {

enum class HornCompareStatus {
    Incomplete,
    StaleScalars,
    ReadyForOperatorReview
};

/// Horn 引导对照快照。不自动判频，不写输入滤波器。
struct HornSnapshot {
    ernc::domain::WorkflowRunState workflow;
    std::optional<ernc::domain::DecodedField> hornRefCheckF_silent;
    std::optional<ernc::domain::DecodedField> hornRefCheckF_horn;
    std::optional<ernc::domain::DecodedField> hornRefCheckT;
    std::optional<float> hornControl;
    std::optional<float> hornRefThreshold;
    std::optional<float> refChooseFloat;
    std::optional<float> hornOffContinueTime;
    std::optional<float> hornTrigger;
    bool scalarsStale{true};
    HornCompareStatus status{HornCompareStatus::Incomplete};
};

/// 调用方把鸣笛滤波器动作是否已确认映射到 WorkflowGateContext::formulaConfirmed。
struct HornWorkflowGates {
    ernc::domain::WorkflowGateContext workflow;
    bool hornFilterActionConfirmed{false};

    /// @brief 合并为 Runner 使用的门禁。formulaConfirmed 表示 horn_filter_switch 步可完成。
    [[nodiscard]] ernc::domain::WorkflowGateContext toWorkflowGates() const noexcept;
};

ernc::domain::WorkflowDefinition buildHornWorkflowDefinition();

std::span<const std::string_view> hornForbiddenCheckTriggerIds() noexcept;
bool hornBlocksCheckTriggerWrite(std::string_view parameterId) noexcept;

/// @brief 输入滤波器写入门禁（R-12，名单可扩展）。未确认前不得写这些 id。
bool hornBlocksInputFilterWrite(std::string_view parameterId) noexcept;

/// @brief 是否允许对某参数 enqueue FC16。
/// @param hornFilterActionConfirmed 鸣笛滤波器切换规则是否已由负责人确认。
/// @param allowHornScalarWrites 仅在为 true 时允许写 Horn 阈值守卫外的标量（本切片默认 false）。
bool hornPermitsParameterWrite(std::string_view parameterId, bool hornFilterActionConfirmed,
                               bool allowHornScalarWrites) noexcept;

ernc::domain::Result<HornSnapshot> materializeHornView(
    ernc::domain::WorkflowRunState workflow, const ernc::domain::ParameterRuntimeStore& store,
    const ernc::domain::RegionDefinition& checkData, std::span<const std::uint8_t> checkDataBytesSilent,
    std::span<const std::uint8_t> checkDataBytesHorn, std::span<const std::uint8_t> checkDataBytesForT);

constexpr std::string_view hornCheckDataRegionPath() noexcept {
    return "check_data";
}

constexpr std::string_view hornRefCheckFieldPath() noexcept {
    return "check_data.HornRefCheck_F";
}

constexpr std::string_view hornRefCheckTFieldPath() noexcept {
    return "check_data.HornRefCheck_T";
}

constexpr std::string_view hornFilterSwitchStepId() noexcept {
    return "horn_filter_switch";
}

}  // namespace ernc::application
