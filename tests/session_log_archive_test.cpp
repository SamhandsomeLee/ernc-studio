#include "ernc/infrastructure/logging/session_log_archive.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>

namespace {

using ernc::domain::Result;
using ernc::infrastructure::SessionLogArchiveScope;
using ernc::infrastructure::exportSessionLogsToZip;

struct TempDir {
    std::filesystem::path dir;

    TempDir() {
        std::random_device device;
        dir = std::filesystem::temp_directory_path() / ("ernc-log-zip-" + std::to_string(device()));
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

bool zipContainsSessionFiles() {
    TempDir temp;
    const std::filesystem::path session = temp.dir / "20260101-120000-1";
    std::filesystem::create_directories(session);
    {
        std::ofstream output(session / "session.log");
        output << "hello\n";
    }
    {
        std::ofstream output(session / "operations.jsonl");
        output << "{\"kind\":\"ui.action\"}\n";
    }
    const std::filesystem::path zipPath = temp.dir / "export.zip";
    const Result<void> exported =
        exportSessionLogsToZip(temp.dir, zipPath, SessionLogArchiveScope::AllSessionsUnderLogsRoot);
    if (!exported.ok() || !std::filesystem::exists(zipPath) || std::filesystem::file_size(zipPath) < 64U) {
        return fail("zip_contains_session_files");
    }
    std::ifstream input(zipPath, std::ios::binary);
    char magic[4] = {};
    input.read(magic, 4);
    if (magic[0] != 'P' || magic[1] != 'K' || magic[2] != 3 || magic[3] != 4) {
        return fail("zip_contains_session_files.magic");
    }
    return true;
}

}  // namespace

int main() {
    return zipContainsSessionFiles() ? 0 : 1;
}
