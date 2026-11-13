#pragma once

#include <filesystem>
#include <string_view>

namespace ernc::presentation {

/// @brief 优先使用可执行文件旁的资源（安装包布局），否则回退到编译期绝对路径（开发树）。
/// @param relativeToExe 相对 exe 目录的路径，使用正斜杠，例如 catalog/parameter_catalog.json。
/// @param compileTimeAbsolute 开发构建时 CMake 注入的源码树路径。
std::filesystem::path resolveBundledResourcePath(std::string_view relativeToExe, std::string_view compileTimeAbsolute);

}  // namespace ernc::presentation
