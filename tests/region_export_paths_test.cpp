#include "ernc/infrastructure/region_export_paths.hpp"

#include <cctype>
#include <filesystem>
#include <string>

namespace {

bool hasLegacyTimestampSuffix(const std::string& fileName, const char* extension) {
    const std::string ext(extension);
    if (fileName.size() < ext.size() + 14U + 1U) {
        return false;
    }
    if (fileName.compare(fileName.size() - ext.size(), ext.size(), ext) != 0) {
        return false;
    }
    const std::size_t digitEnd = fileName.size() - ext.size();
    const std::size_t digitStart = digitEnd - 14U;
    if (digitStart == 0 || fileName[digitStart - 1] != '_') {
        return false;
    }
    for (std::size_t index = digitStart; index < digitEnd; ++index) {
        if (std::isdigit(static_cast<unsigned char>(fileName[index])) == 0) {
            return false;
        }
    }
    return true;
}

bool whole_region_bin_name() {
    const std::string name = ernc::infrastructure::defaultRegionExportFileName("check_data", "check_data", false);
    return name.rfind("check_data_", 0) == 0 && hasLegacyTimestampSuffix(name, ".bin");
}

bool subregion_bin_name() {
    const std::string name =
        ernc::infrastructure::defaultRegionExportFileName("ERNC_TF", "ERNC_TF.fSecPathTransR", false);
    return name.rfind("ERNC_TF_fSecPathTransR_", 0) == 0 && hasLegacyTimestampSuffix(name, ".bin");
}

bool export_dir_under_output() {
    const std::filesystem::path temp = std::filesystem::temp_directory_path() / "ernc_region_export_test";
    std::error_code error;
    std::filesystem::remove_all(temp, error);
    const auto directory = ernc::infrastructure::ensureRegionExportDir(temp, "ERNC_TF", true);
  if (!directory.ok()) {
        return false;
    }
    const std::filesystem::path expected = temp / "output" / std::filesystem::path(u8"传函") / "txt";
    return std::filesystem::equivalent(directory.value(), expected);
}

}  // namespace

int main(const int argc, char* argv[]) {
    if (argc != 2) {
        return 1;
    }
    const std::string_view caseName = argv[1];
    if (caseName == "whole_region_bin_name") {
        return whole_region_bin_name() ? 0 : 1;
    }
    if (caseName == "subregion_bin_name") {
        return subregion_bin_name() ? 0 : 1;
    }
    if (caseName == "export_dir_under_output") {
        return export_dir_under_output() ? 0 : 1;
    }
    return 1;
}
