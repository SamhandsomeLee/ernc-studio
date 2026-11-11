#include "ernc/application/read_parameter.hpp"

#include "ernc/domain/chunk_plan.hpp"
#include "ernc/protocol/typed_codec.hpp"

#include <cstddef>
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

/// @brief 把目录里的标量类型换成值的种类。
/// @param type 目录或布局上的标量类型。
/// @return Float32、Int32 或 UInt32。三种以外没有种类。
ernc::domain::ScalarValue::Kind kindOf(ernc::domain::DataType type) noexcept {
    if (type == ernc::domain::DataType::Int32) {
        return ernc::domain::ScalarValue::Kind::Int32;
    }
    if (type == ernc::domain::DataType::UInt32) {
        return ernc::domain::ScalarValue::Kind::UInt32;
    }
    return ernc::domain::ScalarValue::Kind::Float32;
}

}  // namespace

ParameterReader::ParameterReader(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                                 ernc::domain::ParameterRuntimeStore& store, DeviceSession& session,
                                 CommandScheduler& scheduler)
    : catalog_{catalog}, layout_{layout}, store_{store}, session_{session}, scheduler_{scheduler} {}

ernc::domain::Result<ReadTicket> ParameterReader::read(const ernc::domain::ParameterId& id) {
    if (active_) {
        return ernc::domain::Result<ReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (nextRaw_ == 0) {
        return ernc::domain::Result<ReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::ParameterDefinition* definition = catalog_.find(id.text());
    if (definition == nullptr || !definition->firmwarePath || definition->firmwarePath->empty()) {
        return ernc::domain::Result<ReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::LayoutResolve located = layout_.locate(*definition->firmwarePath);
    if (!located.ok()) {
        return ernc::domain::Result<ReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::FieldLayout& field = located.field();
    if (!field.scalarType || field.slotCount == 0 || field.size != field.slotCount * 4U) {
        return ernc::domain::Result<ReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (definition->dataType && *definition->dataType != *field.scalarType) {
        return ernc::domain::Result<ReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (field.slot > std::numeric_limits<std::uint16_t>::max()) {
        return ernc::domain::Result<ReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<std::vector<ernc::domain::SlotChunk>> planned =
        ernc::domain::planSlotChunks(field.slot, field.slotCount, ernc::domain::TransferDirection::Read);
    if (!planned.ok()) {
        return ernc::domain::Result<ReadTicket>::failure(codeOf(planned));
    }
    const std::vector<ernc::domain::SlotChunk>* chunks = planned.tryValue();
    if (chunks == nullptr || chunks->empty()) {
        return ernc::domain::Result<ReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }

    std::vector<CommandId> commandIds;
    std::vector<PendingChunk> pending;
    commandIds.reserve(chunks->size());
    pending.reserve(chunks->size());
    for (const ernc::domain::SlotChunk& chunk : *chunks) {
        if (chunk.firstSlot > std::numeric_limits<std::uint16_t>::max() || chunk.slotCount == 0) {
            if (!commandIds.empty()) {
                scheduler_.cancel();
            }
            return ernc::domain::Result<ReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        CommandRequest request;
        request.op = CommandOp::Read;
        request.firstSlot = static_cast<std::uint16_t>(chunk.firstSlot);
        request.slotCount = chunk.slotCount;
        request.readBack = false;
        const ernc::domain::Result<CommandId> enqueued = scheduler_.enqueue(request);
        if (!enqueued.ok()) {
            if (!commandIds.empty()) {
                scheduler_.cancel();
            }
            return ernc::domain::Result<ReadTicket>::failure(codeOf(enqueued));
        }
        const CommandId* commandId = enqueued.tryValue();
        if (commandId == nullptr) {
            if (!commandIds.empty()) {
                scheduler_.cancel();
            }
            return ernc::domain::Result<ReadTicket>::failure(ernc::domain::ErrorCode::Unknown);
        }
        commandIds.push_back(*commandId);
        pending.push_back(PendingChunk{*commandId, chunk.slotCount, std::nullopt});
    }

    const std::uint64_t raw = nextRaw_;
    if (nextRaw_ == std::numeric_limits<std::uint64_t>::max()) {
        nextRaw_ = 0;
    } else {
        ++nextRaw_;
    }
    ActiveRead active{raw, id, session_.currentId(), kindOf(*field.scalarType), field.slotCount == 1U, std::move(pending)};
    completed_.reset();
    active_ = std::move(active);
    return ernc::domain::Result<ReadTicket>::success(ReadTicket{raw, std::move(commandIds)});
}

void ParameterReader::apply(const CommandOutcome& outcome, ernc::domain::RuntimeTime readAt) {
    if (!active_) {
        return;
    }
    PendingChunk* chunk = nullptr;
    for (PendingChunk& candidate : active_->chunks) {
        if (candidate.id == outcome.id) {
            chunk = &candidate;
            break;
        }
    }
    if (chunk == nullptr) {
        return;
    }
    if (outcome.session != active_->session || !session_.acceptResult(outcome.session)) {
        active_.reset();
        return;
    }
    if (const auto* failed = std::get_if<FailedCommand>(&outcome.body)) {
        failActive(failed->code);
        return;
    }
    const auto* read = std::get_if<ReadCommandResult>(&outcome.body);
    if (read == nullptr || read->slotCount != chunk->slotCount
        || read->payload.size() != static_cast<std::size_t>(chunk->slotCount) * 4U) {
        failActive(ernc::domain::ErrorCode::PayloadSizeMismatch);
        return;
    }
    if (chunk->payload.has_value()) {
        return;
    }
    chunk->payload = read->payload;
    finish(readAt);
}

bool ParameterReader::busy() const noexcept {
    return active_.has_value();
}

const ParameterReadImage* ParameterReader::image() const noexcept {
    return completed_ ? &*completed_ : nullptr;
}

void ParameterReader::failActive(ernc::domain::ErrorCode code) {
    if (!active_) {
        return;
    }
    store_.failObservation(active_->id, code);
    active_.reset();
    completed_.reset();
}

void ParameterReader::finish(ernc::domain::RuntimeTime readAt) {
    if (!active_) {
        return;
    }
    std::vector<std::uint8_t> bytes;
    for (const PendingChunk& chunk : active_->chunks) {
        if (!chunk.payload) {
            return;
        }
        bytes.insert(bytes.end(), chunk.payload->begin(), chunk.payload->end());
    }
    const ernc::domain::Result<std::vector<ernc::domain::ScalarValue>> decoded =
        ernc::protocol::decodeScalars(bytes, active_->kind);
    if (!decoded.ok()) {
        failActive(codeOf(decoded));
        return;
    }
    const std::vector<ernc::domain::ScalarValue>* values = decoded.tryValue();
    if (values == nullptr) {
        failActive(ernc::domain::ErrorCode::Unknown);
        return;
    }
    if (active_->scalar) {
        if (values->size() != 1U) {
            failActive(ernc::domain::ErrorCode::PayloadSizeMismatch);
            return;
        }
        store_.observe(active_->id, (*values)[0], readAt);
        completed_ = ParameterReadImage{active_->id, true, (*values)[0], {}};
        active_.reset();
        return;
    }
    completed_ = ParameterReadImage{active_->id, false, std::nullopt, *values};
    active_.reset();
}

}  // namespace ernc::application
