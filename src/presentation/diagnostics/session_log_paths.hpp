#pragma once

#include <filesystem>

namespace ernc::presentation {

/// @brief 为本进程新建一次桌面会话日志目录（`.../logs/<timestamp>-<pid>`）。
/// @return 空路径表示创建失败。必须在 UI 主线程调用。
std::filesystem::path createSessionLogDirectory();

}  // namespace ernc::presentation
