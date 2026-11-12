#include "ernc/application/write_parameter.hpp"

#include "ernc/domain/write_plan.hpp"
#include "ernc/protocol/typed_codec.hpp"

#include <array>
#include <limits>
#include <utility>
#include <variant>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_application must not include Qt"
#endif

namespace ernc::application {
namespace {

/// @brief 把目录里的标量类型换成值的种类。
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

}  // namespace

ParameterWriter::ParameterWriter(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                                 ernc::domain::ParameterRuntimeStore& store, DeviceSession& session,
                                 CommandScheduler& scheduler)
    : catalog_{catalog}, layout_{layout}, store_{store}, session_{session}, scheduler_{scheduler} {}

bool ParameterWriter::isCanMsg(std::string_view path) noexcept {
    constexpr std::string_view prefix = "Can_Msg.";
    return path == "Can_Msg" || path.starts_with(prefix);
}

ernc::domain::Result<CommandId> ParameterWriter::write(const ernc::domain::ParameterId& id) {
    if (active_) {
        return ernc::domain::Result<CommandId>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::ParameterDefinition* definition = catalog_.find(id.text());
    if (definition == nullptr || !definition->firmwarePath || definition->firmwarePath->empty()) {
        return ernc::domain::Result<CommandId>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (isCanMsg(*definition->firmwarePath)) {
        return ernc::domain::Result<CommandId>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }

    ernc::domain::WriteScope scope;
    scope.kind = ernc::domain::WriteScopeKind::Selected;
    scope.selected.push_back(id);
    const ernc::domain::WritePlan plan = ernc::domain::buildWritePlan(catalog_, store_, layout_, scope);
    const ernc::domain::PlannedWrite* chosen = nullptr;
    const ernc::domain::RejectedWrite* rejected = nullptr;
    for (const ernc::domain::PlannedWrite& item : plan.included) {
        if (item.id == id) {
            chosen = &item;
        }
    }
    for (const ernc::domain::RejectedWrite& item : plan.excluded) {
        if (item.id == id) {
            rejected = &item;
        }
    }
    if (rejected != nullptr) {
        const ernc::domain::ErrorCode code = rejected->reason == ernc::domain::WriteExclusion::ReadOnly
                                                 ? ernc::domain::ErrorCode::ReadOnlyViolation
                                                 : ernc::domain::ErrorCode::InvalidParameter;
        return ernc::domain::Result<CommandId>::failure(code);
    }
    if (chosen == nullptr) {
        return ernc::domain::Result<CommandId>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }

    const ernc::domain::LayoutResolve located = layout_.locate(chosen->firmwarePath);
    if (!located.ok()) {
        return ernc::domain::Result<CommandId>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::FieldLayout& field = located.field();
    if (!field.scalarType || field.slotCount != 1U || field.size != 4U
        || field.slot > std::numeric_limits<std::uint16_t>::max()) {
        return ernc::domain::Result<CommandId>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    if (definition->dataType && *definition->dataType != *field.scalarType) {
        return ernc::domain::Result<CommandId>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    const ernc::domain::ParameterRuntimeState* state = store_.find(id);
    if (state == nullptr || !state->stagedValue) {
        return ernc::domain::Result<CommandId>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }

    const std::array<std::uint8_t, 4> encoded = ernc::protocol::encodeScalar(chosen->stagedValue);
    CommandRequest request;
    request.op = CommandOp::Write;
    request.firstSlot = static_cast<std::uint16_t>(field.slot);
    request.slotCount = 1;
    request.readBack = false;
    request.payload.assign(encoded.begin(), encoded.end());
    const ernc::domain::Result<CommandId> enqueued = scheduler_.enqueue(request);
    if (!enqueued.ok()) {
        return enqueued;
    }
    const CommandId* commandId = enqueued.tryValue();
    if (commandId == nullptr) {
        return ernc::domain::Result<CommandId>::failure(ernc::domain::ErrorCode::Unknown);
    }
    active_ = ActiveWrite{*commandId, id, session_.currentId(), state->stagedAt, kindOf(*field.scalarType)};
    return ernc::domain::Result<CommandId>::success(*commandId);
}

void ParameterWriter::apply(const CommandOutcome& outcome, ernc::domain::RuntimeTime readAt) {
    if (!active_ || outcome.id != active_->id) {
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
    if (std::get_if<WriteCommandResult>(&outcome.body) != nullptr) {
        const std::optional<ernc::domain::RuntimeTime> stagedAt = active_->stagedAt;
        const ernc::domain::ParameterId parameter = active_->parameter;
        const ernc::domain::ParameterRuntimeState* state = store_.find(parameter);
        if (state != nullptr && state->stagedAt == stagedAt) {
            store_.markClean(parameter);
        }
        active_.reset();
        return;
    }
    const auto* verification = std::get_if<WriteVerification>(&outcome.body);
    if (verification == nullptr) {
        failActive(ernc::domain::ErrorCode::InvalidParameter);
        return;
    }
    const std::optional<ernc::domain::RuntimeTime> stagedAt = active_->stagedAt;
    const ernc::domain::ParameterId parameter = active_->parameter;
    const ernc::domain::ScalarValue::Kind kind = active_->kind;
    if (verification->phase == WritePhase::Verified) {
        const ernc::domain::Result<ernc::domain::ScalarValue> decoded =
            ernc::protocol::decodeScalar(verification->actual, kind);
        if (!decoded.ok() || decoded.tryValue() == nullptr) {
            failActive(ernc::domain::ErrorCode::PayloadSizeMismatch);
            return;
        }
        store_.observe(parameter, *decoded.tryValue(), readAt);
        const ernc::domain::ParameterRuntimeState* state = store_.find(parameter);
        if (state != nullptr && state->stagedAt == stagedAt) {
            store_.markClean(parameter);
        }
        active_.reset();
        return;
    }
    if (verification->actual.size() == 4U) {
        const ernc::domain::Result<ernc::domain::ScalarValue> decoded =
            ernc::protocol::decodeScalar(verification->actual, kind);
        if (decoded.ok() && decoded.tryValue() != nullptr) {
            store_.observe(parameter, *decoded.tryValue(), readAt);
        }
    }
    failActive(ernc::domain::ErrorCode::ReadBackMismatch);
}

bool ParameterWriter::busy() const noexcept {
    return active_.has_value();
}

void ParameterWriter::failActive(ernc::domain::ErrorCode code) {
    if (!active_) {
        return;
    }
    store_.failObservation(active_->parameter, code);
    active_.reset();
}

}  // namespace ernc::application
