#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/types.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::infrastructure {

/// 旧版 JSON 单个参数项；v1 只承载已解析的标量。
struct LegacyParamJsonEntry {
    std::string name;
    ernc::domain::ScalarValue value;
};

/// 旧版 `version` + `params` 文档。
struct LegacyParamJsonDocument {
    std::string version;
    std::vector<LegacyParamJsonEntry> params;
};

/// 解析与应用时的未识别字段报告。
struct LegacyParamJsonReport {
    std::vector<std::string> unrecognizedTopLevelFields;
    std::vector<std::string> unknownNames;
    std::vector<std::string> skippedNames;
    std::vector<std::string> malformedEntries;
    std::size_t appliedCount{0};
};

/// 解析结果：文档与报告分离，报告在无致命语法错误时仍可有内容。
struct LegacyParamJsonLoad {
    bool success{false};
    ernc::domain::ErrorCode error{ernc::domain::ErrorCode::InvalidParameter};
    LegacyParamJsonDocument document;
    LegacyParamJsonReport report;

    [[nodiscard]] bool ok() const noexcept { return success; }
};

inline constexpr std::string_view kLegacyParamJsonVersion = "1.0.0";

/// @brief 解析旧版参数 JSON 文本。语法错误返回 InvalidParameter；`version` 非 1.0.0 也失败。
/// @param catalog 非空时：目录中不存在的 `name` 记入 `unknownNames` 且不进入 `document`。
LegacyParamJsonLoad parseLegacyParamJson(std::string_view utf8,
                                         const ernc::domain::ParameterCatalog* catalog = nullptr);

/// @brief 从文件读取并解析。
LegacyParamJsonLoad loadLegacyParamJsonFile(const std::filesystem::path& source);

/// @brief 序列化为旧版 JSON 文本。
ernc::domain::Result<std::string> serializeLegacyParamJson(const LegacyParamJsonDocument& document);

/// @brief 原子写入 JSON 文件。
ernc::domain::Result<void> writeLegacyParamJsonFile(const std::filesystem::path& destination,
                                                   const LegacyParamJsonDocument& document);

}  // namespace ernc::infrastructure
