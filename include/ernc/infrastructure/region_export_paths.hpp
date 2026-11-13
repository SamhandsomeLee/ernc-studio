#pragma once

#include "ernc/domain/types.hpp"

#include <filesystem>
#include <string_view>

namespace ernc::infrastructure {

/// @brief 旧版 CHNB 区域导出根目录：`{appBase}/output`。
std::filesystem::path regionOutputRoot(const std::filesystem::path& appBase);

/// @brief 确保区域导出目录存在，并返回该目录。
/// @param catalogRegionPath 区域页选中的 ERNC_TF、check_data 或 ERNC_TF.SpecRadius。
/// @param txt 为 true 时返回 txt 子目录，否则 bin。
ernc::domain::Result<std::filesystem::path> ensureRegionExportDir(const std::filesystem::path& appBase,
                                                                  std::string_view catalogRegionPath, bool txt);

/// @brief 生成旧版默认导出文件名（不含目录）。
/// @param catalogRegionPath 区域下拉路径。
/// @param selectedDefinitionPath 当前操作对象的 path（整区或子字段完整路径）。
/// @param txt 扩展名为 .txt 或 .bin。
std::string defaultRegionExportFileName(std::string_view catalogRegionPath, std::string_view selectedDefinitionPath,
                                        bool txt);

/// @brief 默认导出完整路径：`output/传函|checkdata/(bin|txt)/{name}`。
ernc::domain::Result<std::filesystem::path> defaultRegionExportFile(const std::filesystem::path& appBase,
                                                                    std::string_view catalogRegionPath,
                                                                    std::string_view selectedDefinitionPath, bool txt);

}  // namespace ernc::infrastructure
