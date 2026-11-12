#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace ernc::infrastructure {
struct LegacyParamJsonDocument;
}

namespace ernc::application {

/// 镜像播种摘要。只统计目录与布局能解析到的字段。
struct SimulatorMirrorSeedReport {
    /// 已向镜像写入的连续 slot 数（按字段 slotCount 累加，重复路径会重复计数）。
    std::size_t slotsWritten{0};

    /// 有 firmwarePath 但布局 locate 失败或为非连续标量区的目录项。
    std::size_t skippedEntries{0};
};

/// @brief 按目录与布局，把各参数对应 slot 填为类型零（float 0、int32 0、uint32 0）。
/// @param catalog 参数目录，只借用。必须比调用期间活得久。
/// @param layout 固件布局，只借用。
/// @param bytes 从站镜像字节；长度须覆盖布局用到的 slot。本函数只写入不扩容。
/// @return 写入统计。bytes 过短时仍只写落在范围内的 slot。
SimulatorMirrorSeedReport seedSimulatorMirrorDefaults(const ernc::domain::ParameterCatalog& catalog,
                                                      const ernc::domain::FirmwareLayout& layout,
                                                      std::span<std::uint8_t> bytes);

/// @brief 6MIC 工程演示剖面：通道数对齐 proj_config，toggle 取目录 on 线值，其余为非零典型量级。
/// 证据等级：桌面仿真/demo；**不是**固件 PARSEPARAM 真值。
SimulatorMirrorSeedReport seedSimulatorMirrorDemoProfile(const ernc::domain::ParameterCatalog& catalog,
                                                         const ernc::domain::FirmwareLayout& layout,
                                                         std::span<std::uint8_t> bytes);

/// @brief 多槽字段中仍为类型零的 slot 用演示数组值补洞（在 legacy JSON 覆盖之后调用）。
void patchSimulatorMirrorArrayZeros(const ernc::domain::ParameterCatalog& catalog,
                                    const ernc::domain::FirmwareLayout& layout,
                                    std::span<std::uint8_t> bytes);

/// @brief 在默认零底之上，用旧版 params JSON 覆盖能映射的标量项（与 importToStaged 同范围）。
/// @param document version 须为 1.0.0。先调用 seedSimulatorMirrorDefaults 再 overlay 更稳妥。
SimulatorMirrorSeedReport seedSimulatorMirrorFromLegacy(const ernc::domain::ParameterCatalog& catalog,
                                                          const ernc::domain::FirmwareLayout& layout,
                                                          std::span<std::uint8_t> bytes,
                                                          const ernc::infrastructure::LegacyParamJsonDocument& document);

/// @brief 构造演示剖面从站镜像字节，长度由目录与布局 slot 上界决定。
std::vector<std::uint8_t> makeDemoSimulatorImage(const ernc::domain::ParameterCatalog& catalog,
                                                 const ernc::domain::FirmwareLayout& layout);

/// @brief 构造全零类型默认镜像（仅测试或无剖面场景）。
std::vector<std::uint8_t> makeDefaultSimulatorImage(const ernc::domain::ParameterCatalog& catalog,
                                                    const ernc::domain::FirmwareLayout& layout);

}  // namespace ernc::application
