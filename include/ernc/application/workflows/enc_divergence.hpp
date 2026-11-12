#pragma once

#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/region_decode.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/workflows/workflow_definition.hpp"
#include "ernc/domain/workflows/workflow_run_state.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ernc::application {

/// ENC Divergence 对照展示就绪程度。不把 5 点检测映射为 2/4 阶结论。
enum class EncDivergenceCompareStatus {
    Incomplete,
    StaleScalars,
    /// 检测与原始阈值齐全，等待操作员人工对照；不自动写 SpkOrderAmpThre。
    ReadyForOperatorReview
};

/// 一次 ENC Divergence 对照快照。
struct EncDivergenceSnapshot {
    ernc::domain::WorkflowRunState workflow;
    std::optional<float> divergenceControlFlag;
    std::optional<ernc::domain::DecodedField> orderSpkoutmaxCheck;
    /// 调用方读回的 SpkOrderAmpThre 三元素原值。空表示尚未提供设备读。
    std::vector<float> spkOrderAmpThreRaw;
    bool controlFlagStale{true};
    bool ampThreProvided{false};
    EncDivergenceCompareStatus status{EncDivergenceCompareStatus::Incomplete};
};

constexpr std::size_t encDivergenceExpectedAmpThreCount() noexcept {
    return 3U;
}

ernc::domain::WorkflowDefinition buildEncDivergenceWorkflowDefinition();

/// 与 ENC Echo 相同：禁止 CheckENCWFlag、CheckOrder、DataSpeedCheck 等触发写。
std::span<const std::string_view> encDivergenceForbiddenCheckTriggerIds() noexcept;

bool encDivergenceBlocksCheckTriggerWrite(std::string_view parameterId) noexcept;

/// @brief 从 check_data 与 Store 生成 Divergence 对照快照。
///
/// spkOrderAmpThre 须由 ParameterReader 等上层读满 3 个 float 后传入；本函数不推断 2/4 阶下标。
/// @param spkOrderAmpThre 长度为 0 或 3。其它长度返回 InvalidParameter。
ernc::domain::Result<EncDivergenceSnapshot> materializeEncDivergenceView(
    ernc::domain::WorkflowRunState workflow, const ernc::domain::ParameterRuntimeStore& store,
    const ernc::domain::RegionDefinition& checkData, std::span<const std::uint8_t> checkDataBytes,
    std::span<const float> spkOrderAmpThre);

constexpr std::string_view encDivergenceCheckDataRegionPath() noexcept {
    return "check_data";
}

constexpr std::string_view encDivergenceOrderCheckFieldPath() noexcept {
    return "check_data.OrderSpkoutmaxCheck";
}

}  // namespace ernc::application
