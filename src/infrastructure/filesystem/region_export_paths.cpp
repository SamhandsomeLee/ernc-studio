#include "ernc/infrastructure/region_export_paths.hpp"

#include "ernc/domain/types.hpp"

#include <chrono>
#include <cstddef>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>

namespace ernc::infrastructure {
namespace {

std::string exportTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    std::ostringstream stamp;
    stamp << std::put_time(&local, "%Y%m%d%H%M%S");
    return stamp.str();
}

std::optional<std::filesystem::path> outputKindSegment(std::string_view catalogRegionPath) {
    if (catalogRegionPath == "check_data") {
        return std::filesystem::path("checkdata");
    }
    if (catalogRegionPath == "ERNC_TF" || catalogRegionPath == "ERNC_TF.SpecRadius") {
        return std::filesystem::path(std::u8string(u8"传函"));
    }
    return std::nullopt;
}

std::string_view filePrefix(std::string_view catalogRegionPath) {
    if (catalogRegionPath == "check_data") {
        return "check_data";
    }
    if (catalogRegionPath == "ERNC_TF" || catalogRegionPath == "ERNC_TF.SpecRadius") {
        return "ERNC_TF";
    }
    return catalogRegionPath;
}

std::string leafKey(std::string_view definitionPath) {
    const std::size_t dot = definitionPath.rfind('.');
    if (dot == std::string_view::npos || dot + 1 >= definitionPath.size()) {
        return std::string(definitionPath);
    }
    return std::string(definitionPath.substr(dot + 1));
}

bool isWholeRegionExport(std::string_view catalogRegionPath, std::string_view selectedDefinitionPath) {
    return catalogRegionPath == selectedDefinitionPath;
}

}  // namespace

std::filesystem::path regionOutputRoot(const std::filesystem::path& appBase) {
    return appBase / "output";
}

ernc::domain::Result<std::filesystem::path> ensureRegionExportDir(const std::filesystem::path& appBase,
                                                                  const std::string_view catalogRegionPath,
                                                                  const bool txt) {
    const std::optional<std::filesystem::path> kindSegment = outputKindSegment(catalogRegionPath);
    if (!kindSegment) {
        return ernc::domain::Result<std::filesystem::path>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const std::filesystem::path directory =
        regionOutputRoot(appBase) / *kindSegment / (txt ? "txt" : "bin");
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return ernc::domain::Result<std::filesystem::path>::failure(ernc::domain::ErrorCode::FileIoFailed);
    }
    return ernc::domain::Result<std::filesystem::path>::success(directory);
}

std::string defaultRegionExportFileName(const std::string_view catalogRegionPath,
                                        const std::string_view selectedDefinitionPath, const bool txt) {
    const std::string timestamp = exportTimestamp();
    const char* const extension = txt ? ".txt" : ".bin";
    if (isWholeRegionExport(catalogRegionPath, selectedDefinitionPath)) {
        return std::string(selectedDefinitionPath) + '_' + timestamp + extension;
    }
    const std::string prefix(filePrefix(catalogRegionPath));
    const std::string leaf = leafKey(selectedDefinitionPath);
    return prefix + '_' + leaf + '_' + timestamp + extension;
}

ernc::domain::Result<std::filesystem::path> defaultRegionExportFile(const std::filesystem::path& appBase,
                                                                    const std::string_view catalogRegionPath,
                                                                    const std::string_view selectedDefinitionPath,
                                                                    const bool txt) {
    const auto directory = ensureRegionExportDir(appBase, catalogRegionPath, txt);
    if (!directory.ok()) {
        return ernc::domain::Result<std::filesystem::path>::failure(directory.error());
    }
    const std::string fileName = defaultRegionExportFileName(catalogRegionPath, selectedDefinitionPath, txt);
    return ernc::domain::Result<std::filesystem::path>::success(directory.value() / fileName);
}

}  // namespace ernc::infrastructure
