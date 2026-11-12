#pragma once

#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/region_decode.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/workflows/workflow_definition.hpp"
#include "ernc/domain/workflows/workflow_run_state.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ernc::application {

/// ENC Echo 对照在展示层的就绪程度。不表示设备算法通过/失败。
enum class EncEchoCompareStatus {
    /// 还缺检测区或标量读数。
    Incomplete,

    /// 检测区已解码，但阈值或开关读数为 Stale。
    StaleScalars,

    /// 检测与阈值数据齐全，等待操作员对照；不写回设备。
    ReadyForOperatorReview
};

/// 一次 ENC Echo 对照快照。对象拥有解码结果和从 Store 借用的标量副本。
struct EncEchoSnapshot {
    /// 工作流运行态副本。供 UI 显示 Step/Progress/BlockReason。
    ernc::domain::WorkflowRunState workflow;

    /// 回声控制开关 deviceValue。空表示 Store 尚无读回。
    std::optional<float> controlFlag;

    /// ENCEchoGainThreshold 的 deviceValue。
    std::optional<float> gainThreshold;

    /// 从 check_data 解出的 ENCEchoGainCheck。200 点 float 为布局观察，非黄金真值。
    std::optional<ernc::domain::DecodedField> echoGainCheck;

    /// 控制开关是否为 Stale。
    bool controlFlagStale{true};

    /// 阈值参数是否为 Stale。
    bool thresholdStale{true};

    /// 汇总状态，不含自动判灯。
    EncEchoCompareStatus status{EncEchoCompareStatus::Incomplete};
};

/// @brief 构造 ENC Echo 引导工作流定义。
///
/// 步骤仅描述读 check_data 与子字段、展示阈值，以及人工对照。任何一步 permitsWrite 均为 false。
/// @return 调用方拥有的 WorkflowDefinition。
ernc::domain::WorkflowDefinition buildEncEchoWorkflowDefinition();

/// @brief Excel 备注禁止在 Echo 路径触发的 Check 相关标量 id。
///
/// 工作流不得对这些参数 enqueue FC16。ENC Echo 直接读 check_data.ENCEchoGainCheck。
/// @return 只读 id 列表；元素为目录主键文本。
std::span<const std::string_view> encEchoForbiddenCheckTriggerIds() noexcept;

/// @brief 报告某次计划写入是否违反 ENC Echo 安全边界。
/// @param parameterId 目录主键或 firmware_path 文本，只在本次调用期间借用。
/// @return 为禁止触发 Check 的 id 时为 true。
bool encEchoBlocksCheckTriggerWrite(std::string_view parameterId) noexcept;

/// @brief 从已读满的 check_data 字节和 Store 标量生成对照快照。
///
/// 不解码其它 Region，不入队 Modbus。checkData 长度必须等于 region.size。
/// @param workflow 当前 WorkflowRunState，按值复制进结果。
/// @param store 参数运行状态，只借用。不修改 Store。
/// @param checkData check_data 区域目录项，只借用。子字段路径必须含 ENCEchoGainCheck。
/// @param checkDataBytes 父区域原始字节，只借用。
/// @return 成功时 EncEchoSnapshot；字节或子字段不合法时为对应 ErrorCode。
ernc::domain::Result<EncEchoSnapshot> materializeEncEchoView(ernc::domain::WorkflowRunState workflow,
                                                              const ernc::domain::ParameterRuntimeStore& store,
                                                              const ernc::domain::RegionDefinition& checkData,
                                                              std::span<const std::uint8_t> checkDataBytes);

/// @brief 从正式布局路径常量：父区域路径。
constexpr std::string_view encEchoCheckDataRegionPath() noexcept {
    return "check_data";
}

/// @brief 从正式布局路径常量：Echo 检测子字段。
constexpr std::string_view encEchoGainCheckFieldPath() noexcept {
    return "check_data.ENCEchoGainCheck";
}

}  // namespace ernc::application
