#include "ernc/domain/chunk_plan.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::SlotChunk;
using ernc::domain::TransferDirection;
using ernc::domain::kDefaultChunkSlots;
using ernc::domain::kMaxReadChunkSlots;
using ernc::domain::kMaxWriteChunkSlots;
using ernc::domain::planSlotChunks;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 计划是否首尾相接并正好覆盖区间。
/// @param name 失败时打印的用例名。
/// @param chunks 成功计划，只在本次调用期间借用。
/// @param firstSlot 区间起点。
/// @param slotCount 区间长度。0 时计划必须为空。
/// @param cap 该方向单包上限。每一包的数量必须在 1 和 cap 之间。
/// @return 无空隙、无重叠、数量之和等于区间长度时为 true。
bool covers(const char* name, const std::vector<SlotChunk>& chunks, std::uint32_t firstSlot, std::uint32_t slotCount,
            std::uint16_t cap) {
    std::uint64_t cursor = firstSlot;
    std::uint64_t sum = 0;
    for (const SlotChunk& chunk : chunks) {
        if (chunk.slotCount == 0 || chunk.slotCount > cap || chunk.firstSlot != cursor) {
            return fail(name);
        }
        cursor += chunk.slotCount;
        sum += chunk.slotCount;
    }
    if (sum != slotCount || cursor != static_cast<std::uint64_t>(firstSlot) + slotCount) {
        return fail(name);
    }
    return true;
}

/// @brief 成功计划与期望的包列表一致，并满足覆盖不变量。
/// @param name 失败时打印的用例名。
/// @param direction 读或写。
/// @param firstSlot 区间起点。
/// @param slotCount 区间长度。
/// @param slotsPerChunk 包大小。空表示默认 60。
/// @param expected 期望的 (起点, 数量) 列表，只在本次调用期间借用。
/// @return 计划被接受、覆盖完整且逐包一致时为 true。
bool expectPlan(const char* name, TransferDirection direction, std::uint32_t firstSlot, std::uint32_t slotCount,
                std::optional<std::uint16_t> slotsPerChunk, std::span<const std::pair<std::uint32_t, std::uint16_t>> expected) {
    const Result<std::vector<SlotChunk>> planned = planSlotChunks(firstSlot, slotCount, direction, slotsPerChunk);
    const std::vector<SlotChunk>* chunks = planned.tryValue();
    const std::uint16_t cap = direction == TransferDirection::Read ? kMaxReadChunkSlots : kMaxWriteChunkSlots;
    if (!planned.ok() || planned.tryError() != nullptr || chunks == nullptr || chunks->size() != expected.size()) {
        return fail(name);
    }
    if (!covers(name, *chunks, firstSlot, slotCount, cap)) {
        return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if ((*chunks)[index].firstSlot != expected[index].first || (*chunks)[index].slotCount != expected[index].second) {
            return fail(name);
        }
    }
    return true;
}

/// @brief 计划被拒绝且没有成功载荷。
/// @param name 失败时打印的用例名。
/// @param direction 读或写。
/// @param firstSlot 区间起点。
/// @param slotCount 区间长度。
/// @param slotsPerChunk 包大小。空表示默认 60。
/// @return 错误码为 InvalidParameter 时为 true。
bool expectRejected(const char* name, TransferDirection direction, std::uint32_t firstSlot, std::uint32_t slotCount,
                    std::optional<std::uint16_t> slotsPerChunk) {
    const Result<std::vector<SlotChunk>> planned = planSlotChunks(firstSlot, slotCount, direction, slotsPerChunk);
    const ErrorCode* error = planned.tryError();
    if (planned.ok() || planned.tryValue() != nullptr || error == nullptr || *error != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    return true;
}

/// @brief 读 720 个 slot 得到 12 个默认包，末包起点是 1660。
/// @return 每包 60 且覆盖 [1000, 1720) 时为 true。
bool read720Default() {
    const Result<std::vector<SlotChunk>> planned = planSlotChunks(1000, 720, TransferDirection::Read);
    const std::vector<SlotChunk>* chunks = planned.tryValue();
    if (!planned.ok() || chunks == nullptr || chunks->size() != 12) {
        return fail("read_720_default");
    }
    if (!covers("read_720_default", *chunks, 1000, 720, kMaxReadChunkSlots)) {
        return false;
    }
    if (chunks->front().firstSlot != 1000 || chunks->front().slotCount != kDefaultChunkSlots || chunks->back().firstSlot != 1660
        || chunks->back().slotCount != kDefaultChunkSlots) {
        return fail("read_720_default");
    }
    for (const SlotChunk& chunk : *chunks) {
        if (chunk.slotCount != kDefaultChunkSlots) {
            return fail("read_720_default");
        }
    }
    return true;
}

/// @brief 100 个 slot 的默认读计划留下 40 个 slot 的尾包。
/// @return 包为 (0, 60)、(60, 40) 时为 true。
bool readTailForty() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 60}, {60, 40}};
    return expectPlan("read_tail_forty", TransferDirection::Read, 0, 100, std::nullopt, expected);
}

/// @brief 默认写 62 个 slot 时，第二包是 2 个 slot 的尾包。
/// @return 包为 (0, 60)、(60, 2) 时为 true。
bool writeSixtyTwoDefault() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 60}, {60, 2}};
    return expectPlan("write_sixty_two_default", TransferDirection::Write, 0, 62, std::nullopt, expected);
}

/// @brief 写包大小指定为 61 时，62 个 slot 分成 61 和 1。
/// @return 包为 (0, 61)、(61, 1) 时为 true。
bool writeSixtyTwoAtMax() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 61}, {61, 1}};
    return expectPlan("write_sixty_two_at_max", TransferDirection::Write, 0, 62, 61, expected);
}

/// @brief 读满 62 个 slot 且包大小就是 62 时只有一包。
/// @return 包为 (0, 62) 时为 true。
bool readSixtyTwoExact() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 62}};
    return expectPlan("read_sixty_two_exact", TransferDirection::Read, 0, 62, 62, expected);
}

/// @brief 默认读 62 个 slot 仍按 60 切开，不自动用到 62。
/// @return 包为 (0, 60)、(60, 2) 时为 true。
bool readSixtyTwoUsesDefault() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 60}, {60, 2}};
    return expectPlan("read_sixty_two_uses_default", TransferDirection::Read, 0, 62, std::nullopt, expected);
}

/// @brief 数量为 0 时读和写都得到空计划。
/// @return 两次都成功且没有包时为 true。
bool zeroCountIsEmpty() {
    const Result<std::vector<SlotChunk>> read = planSlotChunks(40, 0, TransferDirection::Read, 62);
    const Result<std::vector<SlotChunk>> write = planSlotChunks(40, 0, TransferDirection::Write);
    const std::vector<SlotChunk>* readChunks = read.tryValue();
    const std::vector<SlotChunk>* writeChunks = write.tryValue();
    if (!read.ok() || !write.ok() || readChunks == nullptr || writeChunks == nullptr || !readChunks->empty() || !writeChunks->empty()) {
        return fail("zero_count_is_empty");
    }
    return covers("zero_count_is_empty", *readChunks, 40, 0, kMaxReadChunkSlots)
           && covers("zero_count_is_empty", *writeChunks, 40, 0, kMaxWriteChunkSlots);
}

/// @brief 包大小 0 被拒绝，读和写都没有计划。
/// @return 两次错误码都是 InvalidParameter 时为 true。
bool zeroChunkSizeRejected() {
    if (!expectRejected("zero_chunk_size_rejected", TransferDirection::Read, 0, 10, 0)) {
        return false;
    }
    if (!expectRejected("zero_chunk_size_rejected", TransferDirection::Write, 0, 10, 0)) {
        return false;
    }
    return expectRejected("zero_chunk_size_rejected", TransferDirection::Read, 40, 0, 0);
}

/// @brief 读包大小 70 被压到 62。
/// @return 70 个 slot 分成 (0, 62)、(62, 8) 时为 true。
bool readOversizeChunkClamped() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 62}, {62, 8}};
    return expectPlan("read_oversize_chunk_clamped", TransferDirection::Read, 0, 70, 70, expected);
}

/// @brief 写包大小 70 被压到 61。
/// @return 70 个 slot 分成 (0, 61)、(61, 9) 时为 true。
bool writeOversizeChunkClamped() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 61}, {61, 9}};
    return expectPlan("write_oversize_chunk_clamped", TransferDirection::Write, 0, 70, 70, expected);
}

/// @brief 写包大小 62 也被压到 61，122 个 slot 正好两包。
/// @return 两包都是 61 时为 true。
bool writeOneHundredTwentyTwoAtMax() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 61}, {61, 61}};
    return expectPlan("write_one_hundred_twenty_two_at_max", TransferDirection::Write, 0, 122, 62, expected);
}

/// @brief 1 个 slot 的读和写都只有一包。
/// @return 起点保持为 9，数量为 1 时为 true。
bool singleSlot() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{9, 1}};
    if (!expectPlan("single_slot", TransferDirection::Read, 9, 1, std::nullopt, expected)) {
        return false;
    }
    return expectPlan("single_slot", TransferDirection::Write, 9, 1, 61, expected);
}

/// @brief 正好 60 个 slot 的默认读计划不产生尾包。
/// @return 只有 (5, 60) 时为 true。
bool readExactlySixty() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{5, 60}};
    return expectPlan("read_exactly_sixty", TransferDirection::Read, 5, 60, std::nullopt, expected);
}

/// @brief 默认读 61 个 slot 时尾包只有 1 个 slot。
/// @return 包为 (0, 60)、(60, 1) 时为 true。
bool readSixtyOneDefaultTail() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 60}, {60, 1}};
    return expectPlan("read_sixty_one_default_tail", TransferDirection::Read, 0, 61, std::nullopt, expected);
}

/// @brief 默认写 61 个 slot 时同样留下 1 个 slot 的尾包。
/// @return 包为 (0, 60)、(60, 1) 时为 true。
bool writeSixtyOneDefaultTail() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 60}, {60, 1}};
    return expectPlan("write_sixty_one_default_tail", TransferDirection::Write, 0, 61, std::nullopt, expected);
}

/// @brief 写满 61 个 slot 且包大小就是 61 时只有一包。
/// @return 包为 (3, 61) 时为 true。
bool writeSixtyOneExact() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{3, 61}};
    return expectPlan("write_sixty_one_exact", TransferDirection::Write, 3, 61, 61, expected);
}

/// @brief 默认读 63 个 slot 分成 60 和 3。
/// @return 包为 (0, 60)、(60, 3) 时为 true。
bool readSixtyThreeDefault() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 60}, {60, 3}};
    return expectPlan("read_sixty_three_default", TransferDirection::Read, 0, 63, std::nullopt, expected);
}

/// @brief 读上限 62 时，63 个 slot 的尾包是 1。
/// @return 包为 (0, 62)、(62, 1) 时为 true。
bool readSixtyThreeAtMax() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{0, 62}, {62, 1}};
    return expectPlan("read_sixty_three_at_max", TransferDirection::Read, 0, 63, 62, expected);
}

/// @brief 包大小 1 时每个 slot 单独成包。
/// @return 起点 4 的 3 个 slot 分成三包时为 true。
bool chunkSizeOne() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{4, 1}, {5, 1}, {6, 1}};
    return expectPlan("chunk_size_one", TransferDirection::Read, 4, 3, 1, expected);
}

/// @brief 非零起点的包仍然连续。
/// @return 包为 (7, 3)、(10, 2) 时为 true。
bool nonzeroStartIsContiguous() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{7, 3}, {10, 2}};
    return expectPlan("nonzero_start_is_contiguous", TransferDirection::Write, 7, 5, 3, expected);
}

/// @brief uint32 的最后一个 slot 可以单独成包。
/// @return 读和写都得到起点 UINT32_MAX、数量 1 的一包时为 true。
bool lastUint32Slot() {
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{UINT32_MAX, 1}};
    if (!expectPlan("last_uint32_slot", TransferDirection::Read, UINT32_MAX, 1, std::nullopt, expected)) {
        return false;
    }
    return expectPlan("last_uint32_slot", TransferDirection::Write, UINT32_MAX, 1, std::nullopt, expected);
}

/// @brief 再多一个 slot 就会超出 uint32，读和写都拒绝。
/// @return 两次都是 InvalidParameter 且没有计划时为 true。
bool rangePastUint32Rejected() {
    if (!expectRejected("range_past_uint32_rejected", TransferDirection::Read, UINT32_MAX, 2, std::nullopt)) {
        return false;
    }
    return expectRejected("range_past_uint32_rejected", TransferDirection::Write, UINT32_MAX, 2, 61);
}

/// @brief 一包最大读刚好结束在 uint32 最后一个 slot。
/// @return 只有一包，起点是 UINT32_MAX - 61，数量是 62 时为 true。
bool fullReadChunkEndingAtMax() {
    const std::uint32_t first = UINT32_MAX - 61U;
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{first, 62}};
    return expectPlan("full_read_chunk_ending_at_max", TransferDirection::Read, first, 62, 62, expected);
}

/// @brief 写满 61 个 slot 刚好结束在 uint32 末尾。再多一个就拒绝。
/// @return 合法区间只有一包，越界区间被拒绝时为 true。
bool writeRangeAtUint32Edge() {
    const std::uint32_t first = UINT32_MAX - 60U;
    const std::pair<std::uint32_t, std::uint16_t> expected[] = {{first, 61}};
    if (!expectPlan("write_range_at_uint32_edge", TransferDirection::Write, first, 61, 61, expected)) {
        return false;
    }
    return expectRejected("write_range_at_uint32_edge", TransferDirection::Write, first, 62, 61);
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "read_720_default") {
        return read720Default();
    }
    if (name == "read_tail_forty") {
        return readTailForty();
    }
    if (name == "write_sixty_two_default") {
        return writeSixtyTwoDefault();
    }
    if (name == "write_sixty_two_at_max") {
        return writeSixtyTwoAtMax();
    }
    if (name == "read_sixty_two_exact") {
        return readSixtyTwoExact();
    }
    if (name == "read_sixty_two_uses_default") {
        return readSixtyTwoUsesDefault();
    }
    if (name == "zero_count_is_empty") {
        return zeroCountIsEmpty();
    }
    if (name == "zero_chunk_size_rejected") {
        return zeroChunkSizeRejected();
    }
    if (name == "read_oversize_chunk_clamped") {
        return readOversizeChunkClamped();
    }
    if (name == "write_oversize_chunk_clamped") {
        return writeOversizeChunkClamped();
    }
    if (name == "write_one_hundred_twenty_two_at_max") {
        return writeOneHundredTwentyTwoAtMax();
    }
    if (name == "single_slot") {
        return singleSlot();
    }
    if (name == "read_exactly_sixty") {
        return readExactlySixty();
    }
    if (name == "read_sixty_one_default_tail") {
        return readSixtyOneDefaultTail();
    }
    if (name == "write_sixty_one_default_tail") {
        return writeSixtyOneDefaultTail();
    }
    if (name == "write_sixty_one_exact") {
        return writeSixtyOneExact();
    }
    if (name == "read_sixty_three_default") {
        return readSixtyThreeDefault();
    }
    if (name == "read_sixty_three_at_max") {
        return readSixtyThreeAtMax();
    }
    if (name == "chunk_size_one") {
        return chunkSizeOne();
    }
    if (name == "nonzero_start_is_contiguous") {
        return nonzeroStartIsContiguous();
    }
    if (name == "last_uint32_slot") {
        return lastUint32Slot();
    }
    if (name == "range_past_uint32_rejected") {
        return rangePastUint32Rejected();
    }
    if (name == "full_read_chunk_ending_at_max") {
        return fullReadChunkEndingAtMax();
    }
    if (name == "write_range_at_uint32_edge") {
        return writeRangeAtUint32Edge();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个分包计划单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_chunk_plan_tests <case>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
