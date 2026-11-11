#include "ernc/application/batch_read.hpp"

#include "ernc/domain/chunk_plan.hpp"
#include "ernc/protocol/typed_codec.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <variant>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_application must not include Qt"
#endif

namespace ernc::application {
namespace {

/// 一个通过过滤、等待按 slot 合并的字段。
struct AcceptedField {
    /// 参数标识的副本。
    ernc::domain::ParameterId id;

    /// 起始逻辑 slot。
    std::uint32_t slot{0};

    /// 占用的 slot 数。
    std::uint32_t slotCount{0};

    /// 元素种类。
    ernc::domain::ScalarValue::Kind kind{ernc::domain::ScalarValue::Kind::Float32};

    /// 为 true 时这是 1 个 slot。
    bool scalar{false};
};

/// @brief 判断字段的最后一个 slot 能否放进 FC03 的 16 位地址。
/// @param slot 起始逻辑 slot。
/// @param slotCount 占用的 slot 数。调用前已经大于 0。
/// @return 起点和终点都不超过 uint16 最大值时为 true。
bool fitsFrameAddress(std::uint32_t slot, std::uint32_t slotCount) noexcept {
    if (slot > std::numeric_limits<std::uint16_t>::max()) {
        return false;
    }
    const std::uint64_t last = static_cast<std::uint64_t>(slot) + static_cast<std::uint64_t>(slotCount) - 1U;
    return last <= std::numeric_limits<std::uint16_t>::max();
}

}  // namespace

ParameterBatchReader::ParameterBatchReader(const ernc::domain::ParameterCatalog& catalog,
                                           const ernc::domain::FirmwareLayout& layout,
                                           ernc::domain::ParameterRuntimeStore& store, DeviceSession& session,
                                           CommandScheduler& scheduler)
    : catalog_{catalog}, layout_{layout}, store_{store}, session_{session}, scheduler_{scheduler} {}

ernc::domain::ScalarValue::Kind ParameterBatchReader::kindOf(ernc::domain::DataType type) noexcept {
    if (type == ernc::domain::DataType::Int32) {
        return ernc::domain::ScalarValue::Kind::Int32;
    }
    if (type == ernc::domain::DataType::UInt32) {
        return ernc::domain::ScalarValue::Kind::UInt32;
    }
    return ernc::domain::ScalarValue::Kind::Float32;
}

ernc::domain::Result<BatchReadTicket> ParameterBatchReader::start(const BatchReadScope& scope) {
    if (busy()) {
        return ernc::domain::Result<BatchReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (nextRaw_ == 0) {
        return ernc::domain::Result<BatchReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }

    std::vector<const ernc::domain::ParameterDefinition*> candidates;
    std::vector<BatchSkip> skipped;
    if (scope.kind == BatchReadScopeKind::Selected) {
        std::vector<std::string> seen;
        for (const ernc::domain::ParameterId& id : scope.selected) {
            if (std::find(seen.begin(), seen.end(), id.text()) != seen.end()) {
                continue;
            }
            seen.push_back(id.text());
            const ernc::domain::ParameterDefinition* definition = catalog_.find(id.text());
            if (definition == nullptr) {
                skipped.push_back(BatchSkip{id, BatchSkipReason::NotInCatalog});
                continue;
            }
            candidates.push_back(definition);
        }
    } else {
        for (const ernc::domain::ParameterDefinition& definition : catalog_.entries()) {
            if (scope.kind == BatchReadScopeKind::Category) {
                if (!definition.category || *definition.category != scope.category) {
                    continue;
                }
            }
            candidates.push_back(&definition);
        }
    }

    std::vector<AcceptedField> accepted;
    for (const ernc::domain::ParameterDefinition* definition : candidates) {
        if (!definition->firmwarePath || definition->firmwarePath->empty()) {
            skipped.push_back(BatchSkip{definition->id, BatchSkipReason::MissingPath});
            continue;
        }
        const ernc::domain::LayoutResolve located = layout_.locate(*definition->firmwarePath);
        if (!located.ok()) {
            skipped.push_back(BatchSkip{definition->id, BatchSkipReason::Unlocated});
            continue;
        }
        const ernc::domain::FieldLayout& field = located.field();
        if (!field.scalarType || field.slotCount == 0 || field.size != field.slotCount * 4U
            || !fitsFrameAddress(field.slot, field.slotCount)) {
            skipped.push_back(BatchSkip{definition->id, BatchSkipReason::NotScalar});
            continue;
        }
        if (definition->dataType && *definition->dataType != *field.scalarType) {
            skipped.push_back(BatchSkip{definition->id, BatchSkipReason::NotScalar});
            continue;
        }
        accepted.push_back(AcceptedField{definition->id, field.slot, field.slotCount, kindOf(*field.scalarType),
                                          field.slotCount == 1U});
    }
    std::sort(accepted.begin(), accepted.end(), [](const AcceptedField& left, const AcceptedField& right) {
        if (left.slot != right.slot) {
            return left.slot < right.slot;
        }
        return left.id.text() < right.id.text();
    });

    std::vector<AcceptedField> packed;
    for (const AcceptedField& field : accepted) {
        if (!packed.empty()) {
            const AcceptedField& previous = packed.back();
            const std::uint32_t end = previous.slot + previous.slotCount;
            if (field.slot < end) {
                skipped.push_back(BatchSkip{field.id, BatchSkipReason::Overlap});
                continue;
            }
        }
        packed.push_back(field);
    }

    std::vector<PendingField> fields;
    fields.reserve(packed.size());
    for (const AcceptedField& field : packed) {
        PendingField pending{field.id, field.slot, field.slotCount, field.kind, field.scalar, {}, {}, false, false};
        pending.payload.assign(static_cast<std::size_t>(field.slotCount) * 4U, 0);
        pending.got.assign(field.slotCount, 0);
        fields.push_back(std::move(pending));
    }

    std::vector<ActiveFrame> frames;
    std::vector<BatchFramePlan> plans;
    std::size_t index = 0;
    while (index < fields.size()) {
        const std::uint32_t runStart = fields[index].slot;
        std::uint32_t runEnd = fields[index].slot + fields[index].slotCount;
        std::size_t runLast = index;
        while (runLast + 1U < fields.size() && fields[runLast + 1U].slot == runEnd) {
            ++runLast;
            runEnd = fields[runLast].slot + fields[runLast].slotCount;
        }
        const ernc::domain::Result<std::vector<ernc::domain::SlotChunk>> planned = ernc::domain::planSlotChunks(
            runStart, runEnd - runStart, ernc::domain::TransferDirection::Read, ernc::domain::kMaxReadChunkSlots);
        if (!planned.ok() || planned.tryValue() == nullptr) {
            return ernc::domain::Result<BatchReadTicket>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
        for (const ernc::domain::SlotChunk& chunk : *planned.tryValue()) {
            ActiveFrame frame;
            frame.firstSlot = chunk.firstSlot;
            frame.slotCount = chunk.slotCount;
            BatchFramePlan plan;
            plan.firstSlot = static_cast<std::uint16_t>(chunk.firstSlot);
            plan.slotCount = chunk.slotCount;
            const std::uint32_t chunkEnd = chunk.firstSlot + chunk.slotCount;
            for (std::size_t fieldIndex = index; fieldIndex <= runLast; ++fieldIndex) {
                const PendingField& field = fields[fieldIndex];
                const std::uint32_t fieldEnd = field.slot + field.slotCount;
                if (field.slot < chunkEnd && chunk.firstSlot < fieldEnd) {
                    frame.fields.push_back(fieldIndex);
                    plan.parameters.push_back(field.id);
                }
            }
            frames.push_back(std::move(frame));
            plans.push_back(std::move(plan));
        }
        index = runLast + 1U;
    }

    std::vector<CommandId> commandIds;
    commandIds.reserve(frames.size());
    for (ActiveFrame& frame : frames) {
        CommandRequest request;
        request.op = CommandOp::Read;
        request.firstSlot = static_cast<std::uint16_t>(frame.firstSlot);
        request.slotCount = frame.slotCount;
        request.readBack = false;
        const ernc::domain::Result<CommandId> enqueued = scheduler_.enqueue(request);
        if (!enqueued.ok() || enqueued.tryValue() == nullptr) {
            if (!commandIds.empty()) {
                scheduler_.cancel();
            }
            const ernc::domain::ErrorCode code =
                enqueued.tryError() != nullptr ? *enqueued.tryError() : ernc::domain::ErrorCode::Unknown;
            return ernc::domain::Result<BatchReadTicket>::failure(code);
        }
        frame.id = *enqueued.tryValue();
        commandIds.push_back(frame.id);
    }

    const std::uint64_t raw = nextRaw_;
    if (nextRaw_ == std::numeric_limits<std::uint64_t>::max()) {
        nextRaw_ = 0;
    } else {
        ++nextRaw_;
    }
    skipped_ = std::move(skipped);
    fields_ = std::move(fields);
    frames_ = std::move(frames);
    images_.clear();
    completed_ = 0;
    failed_ = 0;
    sessionId_ = session_.currentId();
    return ernc::domain::Result<BatchReadTicket>::success(BatchReadTicket{raw, std::move(commandIds), std::move(plans)});
}

void ParameterBatchReader::apply(const CommandOutcome& outcome, ernc::domain::RuntimeTime readAt) {
    ActiveFrame* frame = nullptr;
    for (ActiveFrame& candidate : frames_) {
        if (candidate.id == outcome.id) {
            frame = &candidate;
            break;
        }
    }
    if (frame == nullptr || frame->done) {
        return;
    }
    frame->done = true;
    if (outcome.session != sessionId_ || !session_.acceptResult(outcome.session)) {
        return;
    }
    const auto* failed = std::get_if<FailedCommand>(&outcome.body);
    const auto* read = std::get_if<ReadCommandResult>(&outcome.body);
    const bool readable = failed == nullptr && read != nullptr && read->slotCount == frame->slotCount
                          && read->payload.size() == static_cast<std::size_t>(frame->slotCount) * 4U;
    if (!readable) {
        const ernc::domain::ErrorCode code =
            failed != nullptr ? failed->code : ernc::domain::ErrorCode::PayloadSizeMismatch;
        for (const std::size_t fieldIndex : frame->fields) {
            PendingField& field = fields_[fieldIndex];
            if (!field.published && !field.failed) {
                failField(field, code);
            }
        }
        return;
    }
    const std::uint32_t chunkEnd = frame->firstSlot + frame->slotCount;
    for (const std::size_t fieldIndex : frame->fields) {
        PendingField& field = fields_[fieldIndex];
        if (field.published || field.failed) {
            continue;
        }
        const std::uint32_t fieldEnd = field.slot + field.slotCount;
        const std::uint32_t overlapStart = field.slot > frame->firstSlot ? field.slot : frame->firstSlot;
        const std::uint32_t overlapEnd = fieldEnd < chunkEnd ? fieldEnd : chunkEnd;
        if (overlapStart >= overlapEnd) {
            continue;
        }
        const std::size_t dest = static_cast<std::size_t>(overlapStart - field.slot) * 4U;
        const std::size_t src = static_cast<std::size_t>(overlapStart - frame->firstSlot) * 4U;
        const std::size_t nbytes = static_cast<std::size_t>(overlapEnd - overlapStart) * 4U;
        std::copy_n(read->payload.begin() + static_cast<std::ptrdiff_t>(src), nbytes,
                    field.payload.begin() + static_cast<std::ptrdiff_t>(dest));
        for (std::uint32_t slot = overlapStart; slot < overlapEnd; ++slot) {
            field.got[slot - field.slot] = 1;
        }
        const bool complete = std::find(field.got.begin(), field.got.end(), 0) == field.got.end();
        if (complete) {
            publishField(field, readAt);
        }
    }
}

bool ParameterBatchReader::busy() const noexcept {
    for (const ActiveFrame& frame : frames_) {
        if (!frame.done) {
            return true;
        }
    }
    return false;
}

BatchReadProgress ParameterBatchReader::progress() const noexcept {
    return BatchReadProgress{fields_.size(), skipped_.size(), completed_, failed_};
}

const std::vector<BatchSkip>& ParameterBatchReader::skipped() const noexcept {
    return skipped_;
}

const BatchParameterImage* ParameterBatchReader::imageOf(const ernc::domain::ParameterId& id) const noexcept {
    for (const BatchParameterImage& image : images_) {
        if (image.id == id) {
            return &image;
        }
    }
    return nullptr;
}

void ParameterBatchReader::failField(PendingField& field, ernc::domain::ErrorCode code) {
    field.failed = true;
    store_.failObservation(field.id, code);
    ++failed_;
}

void ParameterBatchReader::publishField(PendingField& field, ernc::domain::RuntimeTime readAt) {
    if (field.scalar) {
        const ernc::domain::Result<ernc::domain::ScalarValue> decoded =
            ernc::protocol::decodeScalar(field.payload, field.kind);
        if (!decoded.ok() || decoded.tryValue() == nullptr) {
            failField(field, ernc::domain::ErrorCode::PayloadSizeMismatch);
            return;
        }
        store_.observe(field.id, *decoded.tryValue(), readAt);
        images_.push_back(BatchParameterImage{field.id, true, *decoded.tryValue(), {}});
        field.published = true;
        ++completed_;
        return;
    }
    const ernc::domain::Result<std::vector<ernc::domain::ScalarValue>> decoded =
        ernc::protocol::decodeScalars(field.payload, field.kind);
    if (!decoded.ok() || decoded.tryValue() == nullptr) {
        failField(field, ernc::domain::ErrorCode::PayloadSizeMismatch);
        return;
    }
    images_.push_back(BatchParameterImage{field.id, false, std::nullopt, *decoded.tryValue()});
    field.published = true;
    ++completed_;
}

}  // namespace ernc::application
