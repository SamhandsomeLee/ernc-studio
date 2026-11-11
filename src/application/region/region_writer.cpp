#include "ernc/application/region_writer.hpp"

#include "ernc/domain/chunk_plan.hpp"

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

RegionWriter::RegionWriter(DeviceSession& session, CommandScheduler& scheduler)
    : session_{session}, scheduler_{scheduler} {}

ernc::domain::Result<RegionWritePreview> RegionWriter::preview(const ernc::domain::RegionDefinition& region,
                                                               std::span<const std::uint8_t> bytes) const {
    std::vector<PlannedChunk> chunks;
    const ernc::domain::Result<void> planned = plan(region, bytes, chunks);
    if (!planned.ok()) {
        return ernc::domain::Result<RegionWritePreview>::failure(codeOf(planned));
    }
    RegionWritePreview preview;
    preview.path = region.path;
    preview.slot = region.slot;
    preview.slotCount = region.slotCount;
    preview.size = region.size;
    preview.commitPermitted = ernc::domain::realWritePermitted(region.evidence, region.writable);
    preview.chunks.reserve(chunks.size());
    for (const PlannedChunk& chunk : chunks) {
        preview.chunks.push_back(RegionWriteChunk{chunk.firstSlot, chunk.slotCount});
    }
    return ernc::domain::Result<RegionWritePreview>::success(std::move(preview));
}

ernc::domain::Result<void> RegionWriter::start(const ernc::domain::RegionDefinition& region, std::span<const std::uint8_t> bytes) {
    if (busy()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (!ernc::domain::realWritePermitted(region.evidence, region.writable)) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    std::vector<PlannedChunk> chunks;
    const ernc::domain::Result<void> planned = plan(region, bytes, chunks);
    if (!planned.ok()) {
        return planned;
    }
    Active active;
    active.path = region.path;
    active.session = session_.currentId();
    active.origin = region.slot;
    active.chunks = std::move(chunks);
    active.target.assign(bytes.begin(), bytes.end());
    active.status = RegionWriteStatus::Running;
    active_ = std::move(active);
    if (active_->chunks.empty()) {
        RegionWriteSnapshot snapshot;
        snapshot.path = active_->path;
        snapshot.verified = true;
        active_->snapshot = std::move(snapshot);
        active_->status = RegionWriteStatus::Completed;
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

void RegionWriter::apply(const CommandOutcome& outcome) {
    if (!active_ || active_->status != RegionWriteStatus::Running || active_->current >= active_->chunks.size()) {
        return;
    }
    for (const PlannedChunk& previous : active_->chunks) {
        if (previous.verified && previous.command && *previous.command == outcome.id) {
            return;
        }
    }
    if (!active_->command || outcome.id != *active_->command) {
        return;
    }
    if (outcome.session != active_->session || !session_.acceptResult(outcome.session)) {
        fail(ernc::domain::ErrorCode::DeviceDisconnected);
        return;
    }
    if (const auto* failed = std::get_if<FailedCommand>(&outcome.body)) {
        fail(failed->code);
        return;
    }
    const auto* verification = std::get_if<WriteVerification>(&outcome.body);
    if (verification == nullptr || verification->phase != WritePhase::Verified) {
        const ernc::domain::ErrorCode code =
            verification != nullptr && verification->phase == WritePhase::Mismatch ? ernc::domain::ErrorCode::ReadBackMismatch
                                                                                    : ernc::domain::ErrorCode::ResponseMismatch;
        fail(code);
        return;
    }
    PlannedChunk& chunk = active_->chunks[active_->current];
    const std::size_t byteCount = static_cast<std::size_t>(chunk.slotCount) * 4U;
    if (verification->target.size() != byteCount || verification->actual.size() != byteCount) {
        fail(ernc::domain::ErrorCode::PayloadSizeMismatch);
        return;
    }
    chunk.verified = true;
    chunk.command = outcome.id;
    ++active_->verifiedChunks;
    active_->verifiedBytes += byteCount;
    active_->command.reset();
    advance();
}

bool RegionWriter::busy() const noexcept {
    return active_ && active_->status == RegionWriteStatus::Running;
}

RegionWriteProgress RegionWriter::progress() const noexcept {
    RegionWriteProgress progress;
    if (!active_) {
        return progress;
    }
    progress.plannedChunks = active_->chunks.size();
    progress.verifiedChunks = active_->verifiedChunks;
    progress.verifiedBytes = active_->verifiedBytes;
    progress.originSlot = active_->origin;
    progress.verifiedSlots = static_cast<std::uint32_t>(active_->verifiedBytes / 4U);
    progress.error = active_->error;
    return progress;
}

std::optional<CommandId> RegionWriter::inFlight() const noexcept {
    if (!active_ || active_->status != RegionWriteStatus::Running) {
        return std::nullopt;
    }
    return active_->command;
}

const RegionWriteSnapshot* RegionWriter::snapshot() const noexcept {
    if (!active_ || active_->status != RegionWriteStatus::Completed || !active_->snapshot || !active_->snapshot->verified) {
        return nullptr;
    }
    return &*active_->snapshot;
}

ernc::domain::Result<void> RegionWriter::plan(const ernc::domain::RegionDefinition& region, std::span<const std::uint8_t> bytes,
                                              std::vector<PlannedChunk>& chunks) const {
    chunks.clear();
    if (!sizeMatches(region) || bytes.size() != region.size) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<std::vector<ernc::domain::SlotChunk>> planned =
        ernc::domain::planSlotChunks(region.slot, region.slotCount, ernc::domain::TransferDirection::Write);
    if (!planned.ok()) {
        return ernc::domain::Result<void>::failure(codeOf(planned));
    }
    const std::vector<ernc::domain::SlotChunk>* slots = planned.tryValue();
    if (slots == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::Unknown);
    }
    for (const ernc::domain::SlotChunk& slot : *slots) {
        if (slot.firstSlot < region.slot || slot.firstSlot > std::numeric_limits<std::uint16_t>::max() || slot.slotCount == 0
            || slot.slotCount > ernc::domain::kMaxWriteChunkSlots) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        const std::uint32_t end = slot.firstSlot + slot.slotCount;
        if (end < slot.firstSlot || end > region.slot + region.slotCount) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        chunks.push_back(PlannedChunk{static_cast<std::uint16_t>(slot.firstSlot), slot.slotCount, false, std::nullopt});
    }
    return ernc::domain::Result<void>::success();
}

ernc::domain::Result<void> RegionWriter::submitChunk(std::size_t chunkIndex) {
    if (!active_ || chunkIndex >= active_->chunks.size()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const PlannedChunk& chunk = active_->chunks[chunkIndex];
    const std::size_t byteOffset = static_cast<std::size_t>(chunk.firstSlot - active_->origin) * 4U;
    const std::size_t byteCount = static_cast<std::size_t>(chunk.slotCount) * 4U;
    if (chunk.firstSlot < active_->origin || byteOffset + byteCount > active_->target.size()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    CommandRequest request;
    request.op = CommandOp::Write;
    request.firstSlot = chunk.firstSlot;
    request.slotCount = chunk.slotCount;
    request.readBack = true;
    request.payload.assign(active_->target.begin() + static_cast<std::ptrdiff_t>(byteOffset),
                           active_->target.begin() + static_cast<std::ptrdiff_t>(byteOffset + byteCount));
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
    active_->status = RegionWriteStatus::Running;
    return ernc::domain::Result<void>::success();
}

void RegionWriter::advance() {
    if (!active_) {
        return;
    }
    if (active_->next >= active_->chunks.size()) {
        if (active_->verifiedChunks == active_->chunks.size() && active_->verifiedBytes == active_->target.size()) {
            RegionWriteSnapshot snapshot;
            snapshot.path = active_->path;
            snapshot.bytes = active_->target;
            snapshot.verified = true;
            active_->snapshot = std::move(snapshot);
            active_->status = RegionWriteStatus::Completed;
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

void RegionWriter::fail(ernc::domain::ErrorCode code) {
    if (!active_) {
        return;
    }
    active_->error = code;
    active_->status = RegionWriteStatus::Failed;
    active_->command.reset();
    active_->snapshot.reset();
}

}  // namespace ernc::application
