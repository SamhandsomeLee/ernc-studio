#pragma once

#include <filesystem>

namespace ernc::presentation {

/// @brief 在可执行文件同级 `logs/<timestamp>-<pid>` 下创建本次会话目录。
/// @return 空路径表示创建失败。必须在 UI 主线程调用。
std::filesystem::path createSessionLogDirectory();

}  // namespace ernc::presentation
