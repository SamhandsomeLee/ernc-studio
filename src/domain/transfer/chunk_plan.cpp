#include "ernc/domain/chunk_plan.hpp"

#include <cstdint>
#include <optional>
#include <vector>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

/// @brief 该方向单包允许的最大 slot 数。
/// @param direction 读或写。
/// @return 读为 62，写为 61。方向不是这两种时返回 0。
std::uint16_t chunkCap(TransferDirection direction) noexcept {
    if (direction == TransferDirection::Read) {
        return kMaxReadChunkSlots;
    }
    if (direction == TransferDirection::Write) {
        return kMaxWriteChunkSlots;
    }
    return 0;
}

/// @brief 决定实际使用的包大小。
/// @param direction 读或写。
/// @param slotsPerChunk 调用方指定的包大小。空表示默认 60。
/// @param bulk 成功时写入压过上限后的包大小，至少为 1。
/// @return 方向非法或指定包大小为 0 时为 false。
bool resolveBulk(TransferDirection direction, const std::optional<std::uint16_t>& slotsPerChunk, std::uint16_t& bulk) noexcept {
    const std::uint16_t cap = chunkCap(direction);
    if (cap == 0) {
        return false;
    }
    if (!slotsPerChunk.has_value()) {
        bulk = kDefaultChunkSlots;
        return true;
    }
    if (*slotsPerChunk == 0) {
        return false;
    }
    bulk = *slotsPerChunk < cap ? *slotsPerChunk : cap;
    return true;
}

}  // namespace

Result<std::vector<SlotChunk>> planSlotChunks(std::uint32_t firstSlot, std::uint32_t slotCount, TransferDirection direction,
                                              std::optional<std::uint16_t> slotsPerChunk) {
    std::uint16_t bulk = 0;
    if (!resolveBulk(direction, slotsPerChunk, bulk)) {
        return Result<std::vector<SlotChunk>>::failure(ErrorCode::InvalidParameter);
    }
    if (slotCount == 0) {
        return Result<std::vector<SlotChunk>>::success({});
    }
    const std::uint64_t end = static_cast<std::uint64_t>(firstSlot) + static_cast<std::uint64_t>(slotCount);
    if (end > static_cast<std::uint64_t>(UINT32_MAX) + 1ULL) {
        return Result<std::vector<SlotChunk>>::failure(ErrorCode::InvalidParameter);
    }
    std::vector<SlotChunk> chunks;
    std::uint64_t cursor = firstSlot;
    while (cursor < end) {
        const std::uint64_t remaining = end - cursor;
        const auto count = static_cast<std::uint16_t>(remaining < bulk ? remaining : bulk);
        chunks.push_back(SlotChunk{static_cast<std::uint32_t>(cursor), count});
        cursor += count;
    }
    return Result<std::vector<SlotChunk>>::success(std::move(chunks));
}

}  // namespace ernc::domain
