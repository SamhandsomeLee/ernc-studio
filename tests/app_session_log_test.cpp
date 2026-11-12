#include "ernc/infrastructure/logging/app_session_log.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

namespace {

using ernc::domain::ErrorCode;
using ernc::infrastructure::AppSessionLog;

struct TempDir {
    std::filesystem::path dir;

    TempDir() {
        std::random_device device;
        dir = std::filesystem::temp_directory_path() / ("ernc-session-log-" + std::to_string(device()));
        std::filesystem::create_directories(dir);
    }

    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
};

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool writesOperationsJsonl() {
    TempDir temp;
    AppSessionLog log(temp.dir, 9U);
    log.recordUiAction("region.import.bin", "{\"region\":\"check_data\",\"expected_bytes\":50868}", false,
                       ErrorCode::InvalidParameter);
    const std::filesystem::path file = temp.dir / "operations.jsonl";
    if (!std::filesystem::exists(file)) {
        return fail("writesOperationsJsonl");
    }
    std::ifstream input(file);
    std::string line;
    if (!std::getline(input, line) || line.find("region.import.bin") == std::string::npos) {
        return fail("writesOperationsJsonl");
    }
  return true;
}

}  // namespace

int main() {
    return writesOperationsJsonl() ? 0 : 1;
}
