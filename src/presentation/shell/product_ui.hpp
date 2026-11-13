#pragma once

namespace ernc::presentation {

/// 为 false 时隐藏 .erncproj 目录工程入口，参数持久化仅通过旧版 JSON 导入/导出。
inline constexpr bool kProjectBundleUiEnabled = false;

}  // namespace ernc::presentation
