#pragma once

#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/types.hpp"

#include <cstdint>
#include <span>

namespace ernc::presentation {

class AnalysisPanel;

/// @brief 把一块区域字节解码后灌入曲线分析页。
///
/// 只在 UI 主线程调用。不访问串口，也不改写 RegionWorkspace 里的字节。
namespace AnalysisRegionBridge {

/// @brief 按区域路径解码并更新分析页里的各张图。
/// @param catalog 固件区域目录，只借用。
/// @param region 当前选中的区域或子字段定义，只借用。
/// @param bytes 与 region.size 一致的区域原始字节，只借用。
/// @param panel 分析页，只借用。本函数只调它的载入接口。
/// @return 解码或绑定曲线失败时为对应错误码。不部分更新专业图。
ernc::domain::Result<void> applyBytes(const ernc::domain::RegionCatalog& catalog,
                                      const ernc::domain::RegionDefinition& region,
                                      std::span<const std::uint8_t> bytes, AnalysisPanel& panel);

}  // namespace AnalysisRegionBridge

}  // namespace ernc::presentation
