#include "ernc/application/transfer_cancellation.hpp"

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_application must not include Qt"
#endif

namespace ernc::application {

std::optional<RegionTransferRecord> noteCancelledRead(const RegionReader& reader) {
    const std::optional<RegionReader::RegionReadPrefix> prefix = reader.partial();
    const RegionReadProgress progress = reader.progress();
    if (!prefix || progress.error != ernc::domain::ErrorCode::OperationCancelled || reader.snapshot() != nullptr) {
        return std::nullopt;
    }
    RegionTransferRecord record;
    record.kind = RegionTransferKind::Read;
    record.path = prefix->path;
    record.originSlot = prefix->originSlot;
    record.completedSlots = prefix->slots;
    record.prefix = prefix->bytes;
    record.diagnosis = ernc::domain::ErrorCode::OperationCancelled;
    record.deviceMayBePartial = false;
    return record;
}

std::optional<RegionTransferRecord> noteCancelledWrite(const RegionWriter& writer) {
    const std::optional<RegionWriter::RegionWritePrefix> prefix = writer.partial();
    const RegionWriteProgress progress = writer.progress();
    if (!prefix || progress.error != ernc::domain::ErrorCode::OperationCancelled || writer.snapshot() != nullptr) {
        return std::nullopt;
    }
    RegionTransferRecord record;
    record.kind = RegionTransferKind::Write;
    record.path = prefix->path;
    record.originSlot = prefix->originSlot;
    record.completedSlots = prefix->slots;
    record.prefix = prefix->bytes;
    record.diagnosis = ernc::domain::ErrorCode::OperationCancelled;
    record.deviceMayBePartial = true;
    return record;
}

}  // namespace ernc::application
