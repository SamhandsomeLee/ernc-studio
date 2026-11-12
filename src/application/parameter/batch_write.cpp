#include "ernc/application/batch_write.hpp"

#include <string_view>
#include <utility>
#include <variant>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_application must not include Qt"
#endif

namespace ernc::application {
namespace {

/// @brief 判断固件路径是不是 Can_Msg 整段或它的子路径。
/// @param path 目录中的固件路径，只在本次调用期间借用。
/// @return 路径等于 Can_Msg，或以 Can_Msg. 开头时为 true。
bool isCanMsg(std::string_view path) noexcept {
    constexpr std::string_view prefix = "Can_Msg.";
    return path == "Can_Msg" || path.starts_with(prefix);
}

}  // namespace

ParameterBatchWriter::ParameterBatchWriter(const ernc::domain::ParameterCatalog& catalog,
                                           const ernc::domain::FirmwareLayout& layout,
                                           ernc::domain::ParameterRuntimeStore& store, DeviceSession& session,
                                           CommandScheduler& scheduler)
    : catalog_{catalog}
    , layout_{layout}
    , store_{store}
    , session_{session}
    , writer_{catalog, layout, store, session, scheduler} {}

ernc::domain::Result<void> ParameterBatchWriter::start(const ernc::domain::WriteScope& scope) {
    if (busy()) {
        return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    items_.clear();
    queue_.clear();
    next_ = 0;
    current_.reset();
    command_.reset();

    const ernc::domain::WritePlan plan = ernc::domain::buildWritePlan(catalog_, store_, layout_, scope);
    for (const ernc::domain::RejectedWrite& rejected : plan.excluded) {
        items_.push_back(BatchWriteItem{rejected.id, BatchWriteKind::Excluded, rejected.reason, std::nullopt});
    }
    for (const ernc::domain::PlannedWrite& included : plan.included) {
        if (!isOneSlot(included)) {
            items_.push_back(BatchWriteItem{included.id, BatchWriteKind::NotOneSlot, std::nullopt, std::nullopt});
            continue;
        }
        queue_.push_back(included.id);
    }
    return submitNext();
}

void ParameterBatchWriter::apply(const CommandOutcome& outcome, ernc::domain::RuntimeTime readAt) {
    if (!current_) {
        return;
    }
    const bool accepted = session_.acceptResult(outcome.session);
    writer_.apply(outcome, readAt);
    if (writer_.busy()) {
        return;
    }
    const ernc::domain::ParameterId id = *current_;
    current_.reset();
    command_.reset();
    if (!accepted) {
        record(id, BatchWriteKind::Failed, ernc::domain::ErrorCode::DeviceDisconnected);
        while (next_ < queue_.size()) {
            record(queue_[next_], BatchWriteKind::Failed, ernc::domain::ErrorCode::DeviceDisconnected);
            ++next_;
        }
        return;
    }
    if (const auto* failed = std::get_if<FailedCommand>(&outcome.body)) {
        record(id, BatchWriteKind::Failed, failed->code);
    } else if (std::get_if<WriteCommandResult>(&outcome.body) != nullptr) {
        record(id, BatchWriteKind::Verified, std::nullopt);
    } else if (const auto* verification = std::get_if<WriteVerification>(&outcome.body)) {
        if (verification->phase == WritePhase::Verified) {
            record(id, BatchWriteKind::Verified, std::nullopt);
        } else {
            record(id, BatchWriteKind::Mismatch, ernc::domain::ErrorCode::ReadBackMismatch);
        }
    } else {
        record(id, BatchWriteKind::Failed, ernc::domain::ErrorCode::InvalidParameter);
    }
    submitNext();
}

bool ParameterBatchWriter::busy() const noexcept {
    return current_.has_value() || next_ < queue_.size();
}

std::optional<CommandId> ParameterBatchWriter::inFlight() const noexcept {
    return command_;
}

const std::vector<BatchWriteItem>& ParameterBatchWriter::items() const noexcept {
    return items_;
}

bool ParameterBatchWriter::isOneSlot(const ernc::domain::PlannedWrite& item) const {
    if (isCanMsg(item.firmwarePath)) {
        return false;
    }
    const ernc::domain::LayoutResolve located = layout_.locate(item.firmwarePath);
    if (!located.ok()) {
        return false;
    }
    const ernc::domain::FieldLayout& field = located.field();
    return field.scalarType.has_value() && field.slotCount == 1U && field.size == 4U;
}

ernc::domain::Result<void> ParameterBatchWriter::submitNext() {
    while (next_ < queue_.size()) {
        const ernc::domain::ParameterId id = queue_[next_];
        ++next_;
        const ernc::domain::Result<CommandId> started = writer_.write(id);
        if (started.ok() && started.tryValue() != nullptr) {
            current_ = id;
            command_ = *started.tryValue();
            return ernc::domain::Result<void>::success();
        }
        const ernc::domain::ErrorCode code =
            started.tryError() != nullptr ? *started.tryError() : ernc::domain::ErrorCode::Unknown;
        record(id, BatchWriteKind::Failed, code);
        if (code == ernc::domain::ErrorCode::DeviceDisconnected) {
            while (next_ < queue_.size()) {
                record(queue_[next_], BatchWriteKind::Failed, ernc::domain::ErrorCode::DeviceDisconnected);
                ++next_;
            }
            return ernc::domain::Result<void>::failure(ernc::domain::ErrorCode::DeviceDisconnected);
        }
    }
    return ernc::domain::Result<void>::success();
}

void ParameterBatchWriter::record(const ernc::domain::ParameterId& id, BatchWriteKind kind,
                                  std::optional<ernc::domain::ErrorCode> error) {
    items_.push_back(BatchWriteItem{id, kind, std::nullopt, error});
}

}  // namespace ernc::application
