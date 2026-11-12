#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/types.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::infrastructure {

/// 旧版 `export_params.tpl`：首项为行数上限，其后为参数名（顺序固定）。
struct LegacyExportTemplate {
    std::size_t rowLimit{0};
    std::vector<std::string> names;
};

struct LegacyTemplateReport {
    std::vector<std::string> unknownTemplateLines;
};

struct LegacyTemplateLoad {
    bool success{false};
    ernc::domain::ErrorCode error{ernc::domain::ErrorCode::InvalidParameter};
    LegacyExportTemplate template_;
    LegacyTemplateReport report;

    [[nodiscard]] bool ok() const noexcept { return success; }
};

struct LegacyParamsFile {
    std::vector<double> values;
};

struct LegacyParamsLoad {
    bool success{false};
    ernc::domain::ErrorCode error{ernc::domain::ErrorCode::InvalidParameter};
    LegacyParamsFile file;

    [[nodiscard]] bool ok() const noexcept { return success; }
};

/// @brief 解析旧版导出模板；`catalog` 中不存在的名进入 `unknownTemplateLines` 并失败。
LegacyTemplateLoad parseExportTemplate(std::string_view utf8, const ernc::domain::ParameterCatalog& catalog);

LegacyTemplateLoad loadExportTemplateFile(const std::filesystem::path& source,
                                          const ernc::domain::ParameterCatalog& catalog);

/// @brief 解析 `.params` 文本；非空行数必须等于 `expectedRows`。
LegacyParamsLoad parseLegacyParamsFile(std::string_view utf8, std::size_t expectedRows);

LegacyParamsLoad loadLegacyParamsFile(const std::filesystem::path& source, std::size_t expectedRows);

/// @brief 按旧版格式序列化：每行 `value,` 加换行，长度为 `rowLimit`（不足由调用方保证）。
ernc::domain::Result<std::string> serializeLegacyParamsFile(const std::vector<double>& values);

ernc::domain::Result<void> writeLegacyParamsFile(const std::filesystem::path& destination,
                                                 const std::vector<double>& values);

}  // namespace ernc::infrastructure
