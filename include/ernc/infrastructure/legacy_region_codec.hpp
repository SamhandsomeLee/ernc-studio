#pragma once

#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/types.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace ernc::infrastructure {

/// @brief 读取一块区域的旧版 BIN。
///
/// 只读打开 source。BIN 没有文件头，内容是小端 float 的原始字节。
/// 证据来自 docs/legacy/BEHAVIOR-MATRIX.md 对旧文件的观察，不是已批准黄金 BIN。
/// 不修改 source，也不访问设备。
/// @param region 区域定义，只在本次调用期间借用。文件长度必须等于它的 size。
/// @param source 用户的 .bin 路径。只读打开。
/// @return 成功时调用方拥有与文件相同的字节。长度不是区域 size，或扩展名不是 .bin 时为 InvalidParameter。
/// 打不开文件时为 FileIoFailed。失败不返回半段字节。
ernc::domain::Result<std::vector<std::uint8_t>> readRegionBin(const ernc::domain::RegionDefinition& region,
                                                              const std::filesystem::path& source);

/// @brief 把完整区域字节写成旧版 BIN。
///
/// 先写临时文件，完成后才换成 destination。不打开别的路径。
/// 字节数必须等于 region.size。长度不符时不创建目标文件。
/// @param region 区域定义，只在本次调用期间借用。
/// @param bytes 完整区域字节，只在本次调用期间借用。
/// @param destination 新的 .bin 路径。它应当不是用户正在打开的源文件。
/// @return 文件写完时成功。长度或扩展名不符为 InvalidParameter。写入失败为 FileIoFailed。
ernc::domain::Result<void> writeRegionBin(const ernc::domain::RegionDefinition& region, std::span<const std::uint8_t> bytes,
                                          const std::filesystem::path& destination);

/// @brief 读取一块区域的旧版 TXT。
///
/// 只读打开 source。空行和以 # 开头的行不当前值。数据行是一个 float 再加一个逗号。
/// 若出现 # region=、# format= 或 # slots=，必须等于区域路径、slot_floats 和 slot 数。
/// 个数必须等于 slotCount。不修改 source。
/// @param region 区域定义，只在本次调用期间借用。
/// @param source 用户的 .txt 路径。只读打开。
/// @return 成功时调用方拥有存储顺序的 float。个数等于 slotCount。
/// 格式不符为 InvalidParameter。打不开文件时为 FileIoFailed。失败不返回半段数值。
ernc::domain::Result<std::vector<float>> readRegionTxt(const ernc::domain::RegionDefinition& region,
                                                       const std::filesystem::path& source);

/// @brief 把完整 float 序列写成旧版区域 TXT。
///
/// 先写三行注释：# region=、# format=slot_floats、# slots=。然后每个 float 一行 value,。
/// 顺序与 values 相同。个数必须等于 slotCount。先写临时文件，完成后才换成 destination。
/// @param region 区域定义，只在本次调用期间借用。
/// @param values 存储顺序的 float，只在本次调用期间借用。
/// @param destination 新的 .txt 路径。它应当不是用户正在打开的源文件。
/// @return 文件写完时成功。个数或扩展名不符为 InvalidParameter。写入失败为 FileIoFailed。
ernc::domain::Result<void> writeRegionTxt(const ernc::domain::RegionDefinition& region, std::span<const float> values,
                                          const std::filesystem::path& destination);

}  // namespace ernc::infrastructure
