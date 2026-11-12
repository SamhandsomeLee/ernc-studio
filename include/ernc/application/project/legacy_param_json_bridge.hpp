#pragma once

#include "ernc/application/project/parameter_snapshot.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/types.hpp"
#include "ernc/infrastructure/legacy_param_json.hpp"

namespace ernc::application {

/// 写入 `parameter_snapshot.source` 的导入来源标记。
inline constexpr std::string_view kLegacyJsonImportSnapshotSource = "legacy_json_import";

/// Set 文档来自外部导入或应用先前保存的工程。
enum class StagedParameterSource {
    /// 外部旧版文件按通用导入策略验证。
    LegacyJson,
    /// 工程中可还原功能页暂存的固件时间原值，保持既有命令和只读限制。
    Project,
};

/// @brief 将可编辑标量暂存为 Set。逐项报告不可用记录，不改设备 Read。
/// @param catalog 当前参数目录，仅在本次调用期间借用。
/// @param layout 当前固件布局，用于验证目标参数类型，仅在本次调用期间借用。
/// @param store 调用方独占的运行态，修改其中的 Set 值。
/// @param document 旧版 v1 文档，仅在本次调用期间借用。
/// @param stagedAt 新 Set 值的暂存时刻，不代表设备读写时间。
/// @param parseReport 可选的逐项解析报告，仅在本次调用期间借用。
/// @param source 外部导入或工程还原，决定是否允许恢复已暂存的时间原值。
ernc::domain::Result<ernc::infrastructure::LegacyParamJsonReport> importToStaged(
    const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
    ernc::domain::ParameterRuntimeStore& store, const ernc::infrastructure::LegacyParamJsonDocument& document,
    ernc::domain::RuntimeTime stagedAt,
    const ernc::infrastructure::LegacyParamJsonReport* parseReport = nullptr,
    StagedParameterSource source = StagedParameterSource::LegacyJson);

/// @brief 把 JSON 标量写入快照表，不修改 Store，不写设备。
ernc::domain::Result<std::int64_t> importToSnapshot(
    ParameterSnapshotService& service, const CaptureSnapshotRequest& request,
    const ernc::domain::ParameterCatalog& catalog, ernc::domain::ParameterRuntimeStore& store,
    const ernc::infrastructure::LegacyParamJsonDocument& document, ernc::domain::RuntimeTime stagedAt);

/// @brief 导出目录内全部标量：Set 优先，其次 Read；无值时按类型写 0（对齐旧版 export_params_to_json）。
/// @param catalog 当前参数目录，仅在本次调用期间借用。
/// @param store 运行态只读借用，历史及离线 Read 值也可作为回退值。
ernc::domain::Result<ernc::infrastructure::LegacyParamJsonDocument> exportStagedThenDevice(
    const ernc::domain::ParameterCatalog& catalog, const ernc::domain::ParameterRuntimeStore& store);

/// @brief 捕获工程要保存的 Set 值，包含已提交过但仍显示在 Set 列的值。
/// @param catalog 当前参数目录，仅在本次调用期间借用。
/// @param store 运行态只读借用，读取方与编辑方必须在同一线程。
ernc::infrastructure::LegacyParamJsonDocument captureStagedParameters(
    const ernc::domain::ParameterCatalog& catalog, const ernc::domain::ParameterRuntimeStore& store);

/// @brief 捕获工程 manifest 的 read_parameters：目录内全部标量；有显示用 Read 时写入该值，否则按类型写 0（与 JSON 导出的 Read 侧一致，不含 Set 覆盖）。
/// @param catalog 当前参数目录，仅在本次调用期间借用。
/// @param store 运行态只读借用。捕获和编辑必须在同一线程。
ernc::infrastructure::LegacyParamJsonDocument captureReadParameters(
    const ernc::domain::ParameterCatalog& catalog, const ernc::domain::ParameterRuntimeStore& store);

/// @brief 将工程读数作为历史值展示。已有效的实时读数优先。
/// @param catalog 当前参数目录，仅在本次调用期间借用。
/// @param layout 当前布局，仅在本次调用期间借用，用于校验标量类型。
/// @param store 调用方独占的运行态，仅更新工程历史 Read。
/// @param loaded 已成功解析的工程 Read 文档及逐项报告，仅在本次调用期间借用。
/// @return 无效、未知和非标量条目进入报告，设备读回状态保持原样。
ernc::infrastructure::LegacyParamJsonReport restoreProjectReads(
    const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
    ernc::domain::ParameterRuntimeStore& store, const ernc::infrastructure::LegacyParamJsonLoad& loaded);

}  // namespace ernc::application
