#pragma once

#include "ernc/application/project/parameter_snapshot.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/types.hpp"
#include "ernc/infrastructure/legacy_param_json.hpp"
#include "ernc/infrastructure/legacy_param_template.hpp"

namespace ernc::application {

/// 写入 `parameter_snapshot.source` 的 `.params` 导入来源标记。
inline constexpr std::string_view kLegacyParamsImportSnapshotSource = "legacy_params_import";

/// @brief 按模板顺序导出 `.params` 文本（staged 优先，否则 device）；缺失写 `0,` 并补行到 `rowLimit`。
ernc::domain::Result<std::string> exportParamsWithTemplate(
    const ernc::domain::ParameterCatalog& catalog, const ernc::domain::ParameterRuntimeStore& store,
    const ernc::infrastructure::LegacyExportTemplate& template_);

/// @brief 把 `.params` 行按模板名映射进 stagedValue；不写设备。
ernc::domain::Result<ernc::infrastructure::LegacyParamJsonReport> importParamsToStaged(
    const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
    ernc::domain::ParameterRuntimeStore& store, const ernc::infrastructure::LegacyExportTemplate& template_,
    const ernc::infrastructure::LegacyParamsFile& paramsFile, ernc::domain::RuntimeTime stagedAt);

/// @brief 将模板绑定的标量写入快照；不修改 Store。
ernc::domain::Result<std::int64_t> importParamsToSnapshot(
    ParameterSnapshotService& service, const CaptureSnapshotRequest& request,
    const ernc::domain::ParameterCatalog& catalog, const ernc::domain::ParameterRuntimeStore& store,
    const ernc::infrastructure::LegacyExportTemplate& template_,
    const ernc::infrastructure::LegacyParamsFile& paramsFile);

}  // namespace ernc::application
