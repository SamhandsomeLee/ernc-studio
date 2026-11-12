#include "diagnostics/session_log_paths.hpp"

#include <QCoreApplication>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace ernc::presentation {

std::filesystem::path createSessionLogDirectory() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    if (exeDir.isEmpty()) {
        return {};
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    std::ostringstream stamp;
    stamp << std::put_time(&local, "%Y%m%d-%H%M%S");
#ifdef _WIN32
    const int pid = _getpid();
#else
    const int pid = static_cast<int>(getpid());
#endif
    stamp << '-' << pid;
    const std::filesystem::path directory =
        std::filesystem::path(exeDir.toStdWString()) / "logs" / stamp.str();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return {};
    }
    return directory;
}

}  // namespace ernc::presentation
