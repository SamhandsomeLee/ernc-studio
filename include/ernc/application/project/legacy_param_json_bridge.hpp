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

/// @brief 把文档标量写入 Store 的 stagedValue；区域/数组/只读项进入 report.skippedNames。
ernc::domain::Result<ernc::infrastructure::LegacyParamJsonReport> importToStaged(
    const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
    ernc::domain::ParameterRuntimeStore& store, const ernc::infrastructure::LegacyParamJsonDocument& document,
    ernc::domain::RuntimeTime stagedAt,
    const ernc::infrastructure::LegacyParamJsonReport* parseReport = nullptr);

/// @brief 把 JSON 标量写入快照表，不修改 Store，不写设备。
ernc::domain::Result<std::int64_t> importToSnapshot(
    ParameterSnapshotService& service, const CaptureSnapshotRequest& request,
    const ernc::domain::ParameterCatalog& catalog, ernc::domain::ParameterRuntimeStore& store,
    const ernc::infrastructure::LegacyParamJsonDocument& document, ernc::domain::RuntimeTime stagedAt);

/// @brief 导出 catalog 内标量：优先 stagedValue，否则 deviceValue。
ernc::domain::Result<ernc::infrastructure::LegacyParamJsonDocument> exportStagedThenDevice(
    const ernc::domain::ParameterCatalog& catalog, const ernc::domain::ParameterRuntimeStore& store);

}  // namespace ernc::application
