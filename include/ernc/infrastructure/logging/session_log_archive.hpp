#pragma once

#include "ernc/domain/types.hpp"

#include <cstdint>
#include <filesystem>

namespace ernc::infrastructure {

/// @brief 要打包的日志根目录范围。
enum class SessionLogArchiveScope : std::uint8_t {
    /// 整个 `logs/` 目录下的全部会话子目录。
    AllSessionsUnderLogsRoot,
};

/// @brief 把会话日志目录树写成单个 zip（存储法，无 deflate 依赖）。
/// @param logsRoot 通常为 `<exe>/logs`。必须存在且为目录。
/// @param destinationZip 目标 `.zip` 路径。已存在时被覆盖。
/// @param scope 当前仅支持打包整个 logsRoot。
/// @return 读文件或写 zip 失败时为 FileIoFailed。
ernc::domain::Result<void> exportSessionLogsToZip(const std::filesystem::path& logsRoot,
                                                const std::filesystem::path& destinationZip,
                                                SessionLogArchiveScope scope = SessionLogArchiveScope::AllSessionsUnderLogsRoot);

}  // namespace ernc::infrastructure
