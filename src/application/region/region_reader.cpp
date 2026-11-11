#include "ernc/application/region_reader.hpp"

#include "ernc/domain/chunk_plan.hpp"

#include <algorithm>
#include <limits>
#include <utility>
#include <variant>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_application must not include Qt"
#endif

namespace ernc::application {
namespace {

/// @brief 取出失败结果里的错误码。
/// @tparam T 成功载荷类型。
/// @param result 已经失败的结果，只在本次调用期间借用。
/// @return 结果里的错误码。没有错误码时为 Unknown。
template <typename T>
ernc::domain::ErrorCode codeOf(const ernc::domain::Result<T>& result) noexcept {
    if (const ernc::domain::ErrorCode* code = result.tryError()) {
        return *code;
    }
    return ernc::domain::ErrorCode::Unknown;
}

/// @brief 这块区域的字节数是否等于 slot 数乘 4。
/// @param region 区域定义，只在本次调用期间借用。
/// @return 长度一致时为 true。slot 数大会让乘法溢出，这时为 false。
bool sizeMatches(const ernc::domain::RegionDefinition& region) noexcept {
    if (region.slotCount > (std::numeric_limits<std::uint32_t>::max() / 4U)) {
        return false;
    }
    return region.size == region.slotCount * 4U;
}

}  // namespace

RegionReader::RegionReader(DeviceSession& session, CommandScheduler& scheduler)
    : session_{session}, scheduler_{scheduler} {}

ernc::domain::Result<void> RegionReader::start(const ernc::domain::RegionDefinition& region) {
    if (busy()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (!ernc::domain::realReadPermitted(region.evidence) || !sizeMatches(region)) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<std::vector<ernc::domain::SlotChunk>> planned =
        ernc::domain::planSlotChunks(region.slot, region.slotCount, ernc::domain::TransferDirection::Read);
    if (!planned.ok()) {
        return ernc::domain::Result<void>::failure(codeOf(planned));
    }
    const std::vector<ernc::domain::SlotChunk>* slots = planned.tryValue();
    if (slots == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::Unknown);
    }
    std::vector<PlannedChunk> chunks;
    chunks.reserve(slots->size());
    for (const ernc::domain::SlotChunk& slot : *slots) {
        if (slot.firstSlot < region.slot || slot.firstSlot > std::numeric_limits<std::uint16_t>::max() || slot.slotCount == 0) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        const std::uint32_t end = slot.firstSlot + slot.slotCount;
        if (end < slot.firstSlot || end > region.slot + region.slotCount) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        chunks.push_back(PlannedChunk{static_cast<std::uint16_t>(slot.firstSlot), slot.slotCount, false, std::nullopt});
    }

    Active active;
    active.path = region.path;
    active.session = session_.currentId();
    active.origin = region.slot;
    active.chunks = std::move(chunks);
    active.buffer.assign(region.size, 0);
    active.status = RegionReadStatus::Running;
    active_ = std::move(active);
    if (active_->chunks.empty()) {
        RegionSnapshot snapshot;
        snapshot.path = active_->path;
        snapshot.complete = true;
        active_->snapshot = std::move(snapshot);
        active_->status = RegionReadStatus::Completed;
        return ernc::domain::Result<void>::success();
    }
    const ernc::domain::Result<void> submitted = submitChunk(0);
    if (!submitted.ok()) {
        const ernc::domain::ErrorCode code = codeOf(submitted);
        fail(code);
        return ernc::domain::Result<void>::failure(code);
    }
    active_->next = 1;
    return ernc::domain::Result<void>::success();
}

void RegionReader::apply(const CommandOutcome& outcome) {
    if (!active_ || active_->status != RegionReadStatus::Running || active_->current >= active_->chunks.size()) {
        return;
    }
    for (const PlannedChunk& previous : active_->chunks) {
        if (previous.filled && previous.command && *previous.command == outcome.id) {
            return;
        }
    }
    if (!active_->command || outcome.id != *active_->command) {
        return;
    }
    PlannedChunk& chunk = active_->chunks[active_->current];
    if (outcome.session != active_->session || !session_.acceptResult(outcome.session)) {
        fail(ernc::domain::ErrorCode::DeviceDisconnected);
        return;
    }
    if (const auto* failed = std::get_if<FailedCommand>(&outcome.body)) {
        fail(failed->code);
        return;
    }
    const auto* read = std::get_if<ReadCommandResult>(&outcome.body);
    if (read == nullptr || read->slotCount != chunk.slotCount || read->payload.size() != static_cast<std::size_t>(chunk.slotCount) * 4U) {
        fail(ernc::domain::ErrorCode::PayloadSizeMismatch);
        return;
    }
    if (chunk.firstSlot < active_->origin) {
        fail(ernc::domain::ErrorCode::InvalidParameter);
        return;
    }
    const std::size_t byteOffset = static_cast<std::size_t>(chunk.firstSlot - active_->origin) * 4U;
    if (byteOffset + read->payload.size() > active_->buffer.size()) {
        fail(ernc::domain::ErrorCode::PayloadSizeMismatch);
        return;
    }
    std::copy(read->payload.begin(), read->payload.end(), active_->buffer.begin() + static_cast<std::ptrdiff_t>(byteOffset));
    chunk.filled = true;
    chunk.command = outcome.id;
    ++active_->filledChunks;
    active_->filledBytes += read->payload.size();
    active_->command.reset();
    advance();
}

bool RegionReader::busy() const noexcept {
    return active_ && active_->status == RegionReadStatus::Running;
}

RegionReadProgress RegionReader::progress() const noexcept {
    RegionReadProgress progress;
    if (!active_) {
        return progress;
    }
    progress.plannedChunks = active_->chunks.size();
    progress.filledChunks = active_->filledChunks;
    progress.plannedBytes = active_->buffer.size();
    progress.filledBytes = active_->filledBytes;
    progress.error = active_->error;
    return progress;
}

std::optional<CommandId> RegionReader::inFlight() const noexcept {
    if (!active_ || active_->status != RegionReadStatus::Running) {
        return std::nullopt;
    }
    return active_->command;
}

const RegionSnapshot* RegionReader::snapshot() const noexcept {
    if (!active_ || active_->status != RegionReadStatus::Completed || !active_->snapshot || !active_->snapshot->complete) {
        return nullptr;
    }
    return &*active_->snapshot;
}

ernc::domain::Result<void> RegionReader::submitChunk(std::size_t chunkIndex) {
    if (!active_ || chunkIndex >= active_->chunks.size()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const PlannedChunk& chunk = active_->chunks[chunkIndex];
    CommandRequest request;
    request.op = CommandOp::Read;
    request.firstSlot = chunk.firstSlot;
    request.slotCount = chunk.slotCount;
    request.readBack = false;
    const ernc::domain::Result<CommandId> enqueued = scheduler_.enqueue(request);
    if (!enqueued.ok()) {
        return ernc::domain::Result<void>::failure(codeOf(enqueued));
    }
    const CommandId* command = enqueued.tryValue();
    if (command == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::Unknown);
    }
    active_->command = *command;
    active_->current = chunkIndex;
    active_->status = RegionReadStatus::Running;
    return ernc::domain::Result<void>::success();
}

void RegionReader::advance() {
    if (!active_) {
        return;
    }
    if (active_->next >= active_->chunks.size()) {
        if (active_->filledChunks == active_->chunks.size() && active_->filledBytes == active_->buffer.size()) {
            RegionSnapshot snapshot;
            snapshot.path = active_->path;
            snapshot.bytes = active_->buffer;
            snapshot.complete = true;
            active_->snapshot = std::move(snapshot);
            active_->status = RegionReadStatus::Completed;
            active_->error.reset();
        } else {
            fail(ernc::domain::ErrorCode::InvalidParameter);
        }
        active_->command.reset();
        return;
    }
    const std::size_t index = active_->next;
    const ernc::domain::Result<void> submitted = submitChunk(index);
    if (!submitted.ok()) {
        fail(codeOf(submitted));
        return;
    }
    ++active_->next;
}

void RegionReader::fail(ernc::domain::ErrorCode code) {
    if (!active_) {
        return;
    }
    active_->error = code;
    active_->status = RegionReadStatus::Failed;
    active_->command.reset();
    active_->snapshot.reset();
}

}  // namespace ernc::application
