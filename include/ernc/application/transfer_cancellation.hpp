#pragma once

#include "ernc/application/region_reader.hpp"
#include "ernc/application/region_writer.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ernc::application {

/// 被取消的是读还是写。
enum class RegionTransferKind {
    /// 区域读。前缀是已经按 slot 写下的字节。
    Read,

    /// 区域写。前缀只包含读回已经对上的字节。设备上可能只更新了这段。
    Write
};

/// @brief 一次取消后留下的不完整区域。
///
/// 对象拥有路径和前缀字节。canSaveAsCompleteBin 恒为 false。
/// 这不是完整快照，也不是整块 Verified。不写文件。
struct RegionTransferRecord {
    /// 读或写。
    RegionTransferKind kind{RegionTransferKind::Read};

    /// 区域路径。
    std::string path;

    /// 区域起始逻辑 slot。
    std::uint32_t originSlot{0};

    /// 从起点连续完成的 slot 数。
    std::uint32_t completedSlots{0};

    /// 只覆盖这段前缀的字节。
    std::vector<std::uint8_t> prefix;

    /// 取消诊断。成功记下的记录是 OperationCancelled。
    ernc::domain::ErrorCode diagnosis{ernc::domain::ErrorCode::OperationCancelled};

    /// 写路径上为 true，表示设备可能只更新了这段前缀。
    bool deviceMayBePartial{false};

    /// @brief 这份记录能不能当完整 BIN 保存。
    /// @return 恒为 false。不完整字节不能当成完整区域文件。
    bool canSaveAsCompleteBin() const noexcept { return false; }
};

/// @brief 从一次已取消的区域读收下不完整记录。
///
/// 不修改 reader，也不写文件。完整快照、尚未结束或失败的读不产生记录。
/// @param reader 区域读，只在本次调用期间借用。
/// @return 状态是 Cancelled 时，调用方拥有记录。其他情况为空。
std::optional<RegionTransferRecord> noteCancelledRead(const RegionReader& reader);

/// @brief 从一次已取消的区域写收下不完整记录。
///
/// 不修改 writer，也不写文件。整块 Verified、尚未结束或失败的写不产生记录。
/// @param writer 区域写，只在本次调用期间借用。
/// @return 状态是 Cancelled 时，调用方拥有记录。deviceMayBePartial 为 true。其他情况为空。
std::optional<RegionTransferRecord> noteCancelledWrite(const RegionWriter& writer);

}  // namespace ernc::application
