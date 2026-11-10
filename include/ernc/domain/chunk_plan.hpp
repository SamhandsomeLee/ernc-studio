#pragma once

#include "ernc/domain/types.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace ernc::domain {

/// 一次连续读取默认使用的 slot 数。
/// 旧版 DEFAULT_BULK_SLOTS。它小于读上限 62，也小于写上限 61。
constexpr std::uint16_t kDefaultChunkSlots = 60;

/// 一次读取允许的最大 slot 数。与协议层 kMaxFc03ReadSlots 相同，Domain 不包含协议头。
/// 旧版 MAX_BULK_SLOTS_READ。
constexpr std::uint16_t kMaxReadChunkSlots = 62;

/// 一次写入允许的最大 slot 数。与协议层 kMaxFc16WriteSlots 相同。
/// 旧版 MAX_BULK_SLOTS_WRITE。
constexpr std::uint16_t kMaxWriteChunkSlots = 61;

/// 分包针对的传输方向。它只选择包大小上限，不产生线字节。
enum class TransferDirection {
    /// 读取。单包最多 kMaxReadChunkSlots 个 slot。
    Read,

    /// 写入。单包最多 kMaxWriteChunkSlots 个 slot。
    Write
};

/// @brief 计划中的一包连续 slot。
///
/// 这是逻辑 slot 区间，不是 Modbus 寄存器地址，也不是已经编码的帧。
struct SlotChunk {
    /// 这一包的起始逻辑 slot。下一包从这里加上 slotCount 开始。
    std::uint32_t firstSlot{0};

    /// 这一包的 slot 数。读为 1 到 62，写为 1 到 61。0 不会出现在成功计划里。
    std::uint16_t slotCount{0};

    /// @brief 起点和数量都相同则为相等。比较不包含发生时间或设备状态。
    constexpr bool operator==(const SlotChunk&) const = default;
};

/// @brief 把一段连续 slot 切成不超过包上限的计划。
///
/// 覆盖半开区间 [firstSlot, firstSlot + slotCount)。各包首尾相接，没有重叠，也不越过区间终点。
/// 不传包大小时用 60。传入 0 被拒绝。大于该方向上限时压到上限，不产生超限的包。
/// slotCount 为 0 时成功并返回空计划。最后一个 slot 的下标必须能放进 uint32，否则拒绝且没有半份计划。
/// 本函数不编码帧，不访问串口，也不修改 RuntimeStore。寄存器地址是否放得进 16 位由组帧函数决定。
/// @param firstSlot 区间起点，逻辑 slot。不是寄存器地址。
/// @param slotCount 连续 slot 数。0 表示空区间。
/// @param direction 读或写，决定单包上限。
/// @param slotsPerChunk 调用方指定的包大小。空表示使用 kDefaultChunkSlots。0 非法。大于上限时被压到该方向的上限。
/// @return 成功时调用方拥有按顺序排列的包。包大小为 0 或区间末端超出 uint32 时，错误码为 InvalidParameter。
/// 分配失败时抛出 std::bad_alloc。
Result<std::vector<SlotChunk>> planSlotChunks(std::uint32_t firstSlot, std::uint32_t slotCount, TransferDirection direction,
                                              std::optional<std::uint16_t> slotsPerChunk = std::nullopt);

}  // namespace ernc::domain
