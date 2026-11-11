#include "ernc/application/array_operations.hpp"

#include "ernc/domain/chunk_plan.hpp"
#include "ernc/domain/command_policy.hpp"
#include "ernc/protocol/typed_codec.hpp"

#include <limits>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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

/// @brief 把布局上的标量类型换成数组种类。
/// @param type 布局上的标量类型。
/// @return Float32、Int32 或 UInt32。
ernc::domain::ScalarValue::Kind kindOf(ernc::domain::DataType type) noexcept {
    if (type == ernc::domain::DataType::Int32) {
        return ernc::domain::ScalarValue::Kind::Int32;
    }
    if (type == ernc::domain::DataType::UInt32) {
        return ernc::domain::ScalarValue::Kind::UInt32;
    }
    return ernc::domain::ScalarValue::Kind::Float32;
}

/// 一段连续的 dirty 元素。下标从 0 开始，不是 slot。
struct DirtyRun {
    /// 这段第一个元素的下标。
    std::size_t first{0};

    /// 这段的元素个数。0 不会放进成功的分段里。
    std::size_t count{0};
};

/// @brief 收集 dirty 且带 Set 的连续下标。
/// @param state 数组，只在本次调用期间借用。
/// @param runs 成功时换成本次数组里的分段。失败时不改它。
/// @return dirty 却没有 Set 时为 InvalidParameter。否则成功，空数组得到空分段。
ernc::domain::Result<void> collectDirtyRuns(const ernc::domain::ArrayState& state, std::vector<DirtyRun>& runs) {
    std::vector<DirtyRun> gathered;
    std::size_t index = 0;
    while (index < state.size()) {
        const ernc::domain::ArrayElement* element = state.element(index);
        if (element == nullptr || !element->dirty) {
            ++index;
            continue;
        }
        if (!element->set) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        const std::size_t first = index;
        ++index;
        while (index < state.size()) {
            const ernc::domain::ArrayElement* next = state.element(index);
            if (next == nullptr || !next->dirty) {
                break;
            }
            if (!next->set) {
                return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
            }
            ++index;
        }
        gathered.push_back(DirtyRun{first, index - first});
    }
    runs = std::move(gathered);
    return ernc::domain::Result<void>::success();
}

}  // namespace

ArrayTransfer::ArrayTransfer(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                             ernc::domain::ArrayState& state, DeviceSession& session, CommandScheduler& scheduler)
    : catalog_{catalog}, layout_{layout}, state_{state}, session_{session}, scheduler_{scheduler} {}

ernc::domain::Result<void> ArrayTransfer::read(const ernc::domain::ParameterId& id) {
    if (busy()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    ernc::domain::FieldLayout field;
    const ernc::domain::ParameterDefinition* definition = nullptr;
    const ernc::domain::Result<void> prepared = prepare(id, field, definition);
    if (!prepared.ok()) {
        return prepared;
    }
    std::vector<PlannedChunk> chunks;
    const ernc::domain::Result<void> planned =
        appendChunks(field.slot, field.slotCount, false, field.slot, chunks);
    if (!planned.ok()) {
        return planned;
    }
    begin(id, true, kindOf(*field.scalarType), std::move(chunks), state_.size());
    return finishStart();
}

ernc::domain::Result<void> ArrayTransfer::write(const ernc::domain::ParameterId& id) {
    if (busy()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    ernc::domain::FieldLayout field;
    const ernc::domain::ParameterDefinition* definition = nullptr;
    const ernc::domain::Result<void> prepared = prepare(id, field, definition);
    if (!prepared.ok() || definition == nullptr) {
        return prepared.ok() ? ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::Unknown)
                             : prepared;
    }
    if (const std::optional<ernc::domain::ErrorCode> refusal = writeRefusal(*definition)) {
        return ernc::domain::Result<void>::failure(*refusal);
    }
    std::vector<DirtyRun> runs;
    const ernc::domain::Result<void> gathered = collectDirtyRuns(state_, runs);
    if (!gathered.ok()) {
        return gathered;
    }
    std::vector<PlannedChunk> chunks;
    std::size_t plannedElements = 0;
    for (const DirtyRun& run : runs) {
        if (run.first > std::numeric_limits<std::uint32_t>::max() || run.count > std::numeric_limits<std::uint32_t>::max()
            || field.slot > std::numeric_limits<std::uint32_t>::max() - static_cast<std::uint32_t>(run.first)) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        const std::uint32_t firstSlot = field.slot + static_cast<std::uint32_t>(run.first);
        const ernc::domain::Result<void> planned = appendChunks(
            firstSlot, static_cast<std::uint32_t>(run.count), true, field.slot, chunks);
        if (!planned.ok()) {
            return planned;
        }
        plannedElements += run.count;
    }
    begin(id, false, kindOf(*field.scalarType), std::move(chunks), plannedElements);
    return finishStart();
}

void ArrayTransfer::apply(const CommandOutcome& outcome) {
    if (!active_ || !active_->command || outcome.id != *active_->command || active_->current >= active_->chunks.size()) {
        return;
    }
    const PlannedChunk chunk = active_->chunks[active_->current];
    if (outcome.session != active_->session || !session_.acceptResult(outcome.session)) {
        failAt(chunk.firstIndex, ernc::domain::ErrorCode::DeviceDisconnected);
        return;
    }
    if (const auto* failed = std::get_if<FailedCommand>(&outcome.body)) {
        failAt(chunk.firstIndex, failed->code);
        return;
    }
    if (active_->reading) {
        const auto* read = std::get_if<ReadCommandResult>(&outcome.body);
        if (read == nullptr || read->slotCount != chunk.slotCount
            || read->payload.size() != static_cast<std::size_t>(chunk.slotCount) * 4U) {
            failAt(chunk.firstIndex, ernc::domain::ErrorCode::PayloadSizeMismatch);
            return;
        }
        const ernc::domain::Result<std::vector<ernc::domain::ScalarValue>> decoded =
            ernc::protocol::decodeScalars(read->payload, active_->kind);
        if (!decoded.ok()) {
            failAt(chunk.firstIndex, codeOf(decoded));
            return;
        }
        const std::vector<ernc::domain::ScalarValue>* values = decoded.tryValue();
        if (values == nullptr || !storeValues(chunk, *values, false)) {
            if (values == nullptr && active_->status == ArrayTransferStatus::Running) {
                failAt(chunk.firstIndex, ernc::domain::ErrorCode::Unknown);
            }
            return;
        }
        active_->completedElements += chunk.slotCount;
        ++active_->completedChunks;
        active_->command.reset();
        advance();
        return;
    }

    const auto* verification = std::get_if<WriteVerification>(&outcome.body);
    if (verification == nullptr) {
        failAt(chunk.firstIndex, ernc::domain::ErrorCode::ResponseMismatch);
        return;
    }
    if (verification->phase == WritePhase::Verified) {
        const ernc::domain::Result<std::vector<ernc::domain::ScalarValue>> decoded =
            ernc::protocol::decodeScalars(verification->actual, active_->kind);
        if (!decoded.ok()) {
            failAt(chunk.firstIndex, codeOf(decoded));
            return;
        }
        const std::vector<ernc::domain::ScalarValue>* values = decoded.tryValue();
        if (values == nullptr || !storeValues(chunk, *values, true)) {
            if (values == nullptr && active_->status == ArrayTransferStatus::Running) {
                failAt(chunk.firstIndex, ernc::domain::ErrorCode::Unknown);
            }
            return;
        }
        active_->completedElements += chunk.slotCount;
        ++active_->completedChunks;
        active_->command.reset();
        advance();
        return;
    }
    applyMismatch(chunk, verification->actual);
}

void ArrayTransfer::cancel() noexcept {
    if (active_ && active_->status == ArrayTransferStatus::Running) {
        active_->cancel = true;
    }
}

bool ArrayTransfer::busy() const noexcept {
    return active_ && active_->status == ArrayTransferStatus::Running;
}

ArrayTransferProgress ArrayTransfer::progress() const noexcept {
    ArrayTransferProgress progress;
    if (!active_) {
        return progress;
    }
    progress.plannedElements = active_->plannedElements;
    progress.completedElements = active_->completedElements;
    progress.plannedChunks = active_->chunks.size();
    progress.completedChunks = active_->completedChunks;
    progress.failedIndex = active_->failedIndex;
    progress.status = active_->status;
    progress.error = active_->error;
    return progress;
}

std::optional<CommandId> ArrayTransfer::inFlight() const noexcept {
    if (!active_ || active_->status != ArrayTransferStatus::Running) {
        return std::nullopt;
    }
    return active_->command;
}

ernc::domain::Result<void> ArrayTransfer::prepare(const ernc::domain::ParameterId& id, ernc::domain::FieldLayout& field,
                                                  const ernc::domain::ParameterDefinition*& definition) const {
    definition = nullptr;
    const ernc::domain::ParameterDefinition* found = catalog_.find(id.text());
    if (found == nullptr || !found->firmwarePath || found->firmwarePath->empty()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::LayoutResolve located = layout_.locate(*found->firmwarePath);
    if (!located.ok()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::FieldLayout& resolved = located.field();
    if (!resolved.scalarType || resolved.slotCount > std::numeric_limits<std::uint32_t>::max() / 4U
        || resolved.size != resolved.slotCount * 4U) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (found->dataType && *found->dataType != *resolved.scalarType) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (state_.kind() != kindOf(*resolved.scalarType) || state_.size() != resolved.slotCount) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    field = resolved;
    definition = found;
    return ernc::domain::Result<void>::success();
}

bool ArrayTransfer::isCanMsg(std::string_view path) noexcept {
    constexpr std::string_view prefix{"Can_Msg."};
    return path == "Can_Msg" || path.starts_with(prefix);
}

std::optional<ernc::domain::ErrorCode> ArrayTransfer::writeRefusal(const ernc::domain::ParameterDefinition& definition) const {
    if (!definition.firmwarePath || isCanMsg(*definition.firmwarePath)) {
        return ernc::domain::ErrorCode::InvalidParameter;
    }
    if (!definition.dataType || (definition.shape && *definition.shape == ernc::domain::ValueShape::Region)
        || (definition.editor && *definition.editor == ernc::domain::EditorKind::RegionEditor)) {
        return ernc::domain::ErrorCode::InvalidParameter;
    }
    const ernc::domain::ActuationPolicy policy = ernc::domain::classifyActuation(definition);
    if (policy.kind == ernc::domain::ActuationKind::NotActuated && definition.access
        && *definition.access == ernc::domain::AccessMode::ReadOnly) {
        return ernc::domain::ErrorCode::ReadOnlyViolation;
    }
    if (policy.kind != ernc::domain::ActuationKind::NormalWrite) {
        return ernc::domain::ErrorCode::InvalidParameter;
    }
    return std::nullopt;
}

ernc::domain::Result<void> ArrayTransfer::submitChunk(std::size_t chunkIndex) {
    if (!active_ || chunkIndex >= active_->chunks.size()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const PlannedChunk& chunk = active_->chunks[chunkIndex];
    CommandRequest request;
    request.firstSlot = chunk.firstSlot;
    request.slotCount = chunk.slotCount;
    if (active_->reading) {
        request.op = CommandOp::Read;
        request.readBack = false;
    } else {
        request.op = CommandOp::Write;
        request.readBack = true;
        std::vector<ernc::domain::ScalarValue> values;
        values.reserve(chunk.slotCount);
        for (std::uint16_t offset = 0; offset < chunk.slotCount; ++offset) {
            const ernc::domain::ArrayElement* element = state_.element(chunk.firstIndex + offset);
            if (element == nullptr || !element->set) {
                return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
            }
            values.push_back(*element->set);
        }
        const ernc::domain::Result<std::vector<std::uint8_t>> encoded = ernc::protocol::encodeScalars(values);
        if (!encoded.ok()) {
            return ernc::domain::Result<void>::failure(codeOf(encoded));
        }
        const std::vector<std::uint8_t>* payload = encoded.tryValue();
        if (payload == nullptr) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::Unknown);
        }
        request.payload = *payload;
    }
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
    active_->status = ArrayTransferStatus::Running;
    return ernc::domain::Result<void>::success();
}

void ArrayTransfer::advance() {
    if (!active_) {
        return;
    }
    if (active_->next >= active_->chunks.size()) {
        active_->status = ArrayTransferStatus::Completed;
        active_->error.reset();
        active_->command.reset();
        return;
    }
    if (active_->cancel) {
        active_->status = ArrayTransferStatus::Cancelled;
        active_->error = ernc::domain::ErrorCode::OperationCancelled;
        active_->command.reset();
        return;
    }
    const std::size_t index = active_->next;
    const ernc::domain::Result<void> submitted = submitChunk(index);
    if (!submitted.ok()) {
        failAt(active_->chunks[index].firstIndex, codeOf(submitted));
        return;
    }
    ++active_->next;
}

void ArrayTransfer::failAt(std::size_t index, ernc::domain::ErrorCode code) {
    if (!active_) {
        return;
    }
    active_->failedIndex = index;
    active_->error = code;
    active_->status = ArrayTransferStatus::Failed;
    active_->command.reset();
}

ernc::domain::Result<void> ArrayTransfer::appendChunks(std::uint32_t firstSlot, std::uint32_t slotCount, bool writing,
                                                           std::uint32_t originSlot, std::vector<PlannedChunk>& chunks) const {
    const ernc::domain::TransferDirection direction =
        writing ? ernc::domain::TransferDirection::Write : ernc::domain::TransferDirection::Read;
    const ernc::domain::Result<std::vector<ernc::domain::SlotChunk>> planned =
        ernc::domain::planSlotChunks(firstSlot, slotCount, direction);
    if (!planned.ok()) {
        return ernc::domain::Result<void>::failure(codeOf(planned));
    }
    const std::vector<ernc::domain::SlotChunk>* slots = planned.tryValue();
    if (slots == nullptr) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::Unknown);
    }
    for (const ernc::domain::SlotChunk& slot : *slots) {
        if (slot.firstSlot > std::numeric_limits<std::uint16_t>::max() || slot.slotCount == 0 || slot.firstSlot < originSlot) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        const std::size_t firstIndex = static_cast<std::size_t>(slot.firstSlot - originSlot);
        if (firstIndex + slot.slotCount > state_.size()) {
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        chunks.push_back(PlannedChunk{static_cast<std::uint16_t>(slot.firstSlot), slot.slotCount, firstIndex});
    }
    return ernc::domain::Result<void>::success();
}

void ArrayTransfer::begin(const ernc::domain::ParameterId& id, bool reading, ernc::domain::ScalarValue::Kind kind,
                          std::vector<PlannedChunk> chunks, std::size_t plannedElements) {
    active_ = Active{id,
                     session_.currentId(),
                     reading,
                     kind,
                     std::move(chunks),
                     0,
                     0,
                     std::nullopt,
                     plannedElements,
                     0,
                     0,
                     std::nullopt,
                     std::nullopt,
                     ArrayTransferStatus::Running,
                     false};
}

ernc::domain::Result<void> ArrayTransfer::finishStart() {
    advance();
    if (active_ && active_->status == ArrayTransferStatus::Failed) {
        const ernc::domain::ErrorCode code = active_->error.value_or(ernc::domain::ErrorCode::Unknown);
        return ernc::domain::Result<void>::failure(code);
    }
    return ernc::domain::Result<void>::success();
}

void ArrayTransfer::applyMismatch(const PlannedChunk& chunk, const std::vector<std::uint8_t>& actual) {
    if (!active_) {
        return;
    }
    const ernc::domain::Result<std::vector<ernc::domain::ScalarValue>> decoded =
        ernc::protocol::decodeScalars(actual, active_->kind);
    if (!decoded.ok()) {
        failAt(chunk.firstIndex, codeOf(decoded));
        return;
    }
    const std::vector<ernc::domain::ScalarValue>* values = decoded.tryValue();
    if (values == nullptr || values->size() != chunk.slotCount) {
        failAt(chunk.firstIndex, ernc::domain::ErrorCode::PayloadSizeMismatch);
        return;
    }
    std::size_t mismatch = chunk.slotCount;
    for (std::size_t offset = 0; offset < values->size(); ++offset) {
        const std::size_t index = chunk.firstIndex + offset;
        if (!state_.setRead(index, (*values)[offset]).ok()) {
            failAt(index, ernc::domain::ErrorCode::InvalidParameter);
            return;
        }
        const ernc::domain::ArrayElement* element = state_.element(index);
        const bool same = element != nullptr && element->set && *element->set == (*values)[offset];
        if (!same && mismatch == chunk.slotCount) {
            mismatch = offset;
        }
        if (same && mismatch == chunk.slotCount && !state_.markClean(index).ok()) {
            failAt(index, ernc::domain::ErrorCode::InvalidParameter);
            return;
        }
    }
    if (mismatch == chunk.slotCount) {
        failAt(chunk.firstIndex, ernc::domain::ErrorCode::ReadBackMismatch);
        return;
    }
    active_->completedElements += mismatch;
    failAt(chunk.firstIndex + mismatch, ernc::domain::ErrorCode::ReadBackMismatch);
}

bool ArrayTransfer::storeValues(const PlannedChunk& chunk, const std::vector<ernc::domain::ScalarValue>& values, bool clean) {
    if (values.size() != chunk.slotCount) {
        failAt(chunk.firstIndex, ernc::domain::ErrorCode::PayloadSizeMismatch);
        return false;
    }
    for (std::size_t offset = 0; offset < values.size(); ++offset) {
        const std::size_t index = chunk.firstIndex + offset;
        if (!state_.setRead(index, values[offset]).ok()) {
            failAt(index, ernc::domain::ErrorCode::InvalidParameter);
            return false;
        }
        if (clean && !state_.markClean(index).ok()) {
            failAt(index, ernc::domain::ErrorCode::InvalidParameter);
            return false;
        }
    }
    return true;
}

}  // namespace ernc::application
