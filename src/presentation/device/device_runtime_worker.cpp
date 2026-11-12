#include "device/device_runtime_worker.hpp"

#include "batch_operation_tags.hpp"
#include "write_verification.hpp"

#ifdef slots
#pragma push_macro("slots")
#undef slots
#define ERNC_RESTORE_QT_SLOTS_MACRO
#endif
#include "ernc/application/region_reader.hpp"
#include "ernc/application/region_writer.hpp"
#ifdef ERNC_RESTORE_QT_SLOTS_MACRO
#pragma pop_macro("slots")
#undef ERNC_RESTORE_QT_SLOTS_MACRO
#endif

#include <sstream>

namespace ernc::presentation {
namespace {

std::string jsonQuote(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 2U);
    out.push_back('"');
    for (const char ch : text) {
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
        }
        out.push_back(ch);
    }
    out.push_back('"');
    return out;
}

std::optional<ernc::domain::RegionDefinition> regionDefinitionFor(
    const ernc::domain::RegionCatalog& catalog, const std::string_view path) {
    if (const ernc::domain::RegionDefinition* region = catalog.find(path)) {
        return *region;
    }
    for (const std::string_view root : {std::string_view{"ERNC_TF"}, std::string_view{"check_data"}}) {
        const ernc::domain::RegionDefinition* parent = catalog.find(root);
        if (parent == nullptr) {
            continue;
        }
        for (const ernc::domain::SubRegion& child : parent->subregions) {
            if (child.path != path) {
                continue;
            }
            ernc::domain::RegionDefinition definition;
            definition.path = child.path;
            definition.slot = child.slot;
            definition.slotCount = child.slotCount;
            definition.size = child.size;
            definition.offset = child.offset;
            definition.typeName = child.typeName;
            definition.scalarType = child.scalarType;
            definition.shape = child.shape;
            definition.evidence = child.evidence;
            definition.writable = child.writable;
            return definition;
        }
    }
    return std::nullopt;
}

}  // namespace

DeviceRuntimeWorker::DeviceRuntimeWorker(const ernc::domain::ParameterCatalog& catalog,
                                         const ernc::domain::FirmwareLayout& layout,
                                         ernc::domain::ParameterRuntimeStore& store,
                                         ernc::domain::ArrayRuntimeStore& arrayStore,
                                         ernc::serial::SerialTransport* transport, QObject* parent)
    : QObject(parent), catalog_{catalog}, layout_{layout}, store_{store}, arrayStore_{arrayStore}, transport_{transport} {
    const ernc::domain::Result<ernc::domain::RegionCatalog> regions = ernc::domain::RegionCatalog::load(layout_);
    if (regions.ok() && regions.tryValue() != nullptr) {
        regionCatalog_ = regions.value();
    }
}

DeviceRuntimeWorker::~DeviceRuntimeWorker() = default;

void DeviceRuntimeWorker::attachDiagnosticLog(ernc::infrastructure::DiagnosticLog* log) noexcept {
    diagnosticLog_ = log;
    if (stack_ != nullptr) {
        stack_->attachDiagnosticLog(log);
    }
}

bool DeviceRuntimeWorker::doConnect() {
    if (transport_ == nullptr) {
        return false;
    }
    if (stack_ == nullptr) {
        stack_ = std::make_unique<SerialLinkedStack>(catalog_, layout_, store_, arrayStore_, *transport_);
        if (diagnosticLog_ != nullptr) {
            stack_->attachDiagnosticLog(diagnosticLog_);
        }
    }
    const ernc::domain::Result<ernc::domain::SessionId> connected = stack_->connectLink();
    if (connected.ok() && connected.tryValue() != nullptr) {
        regionReader_ = std::make_unique<ernc::application::RegionReader>(stack_->session(), stack_->scheduler());
        regionWriter_ = std::make_unique<ernc::application::RegionWriter>(stack_->session(), stack_->scheduler());
    }
    return connected.ok() && connected.tryValue() != nullptr;
}

void DeviceRuntimeWorker::doDisconnect() {
    regionReader_.reset();
    regionWriter_.reset();
    activeRegionPath_.clear();
    activeRegionIsWrite_ = false;
    if (stack_ != nullptr) {
        (void)stack_->disconnectLink();
    }
    activeRead_.reset();
    wasWriteBusy_ = false;
}

int DeviceRuntimeWorker::startRegionRead(const QString& path) {
    using ernc::domain::ErrorCode;
    if (stack_ == nullptr || !stack_->connected()) {
        return static_cast<int>(ErrorCode::DeviceDisconnected);
    }
    if (regionReader_ == nullptr || !regionCatalog_ || !activeRegionPath_.empty() || activeRead_.has_value()
        || stack_->batchReader().busy() || stack_->batchWriter().busy()) {
        return static_cast<int>(ErrorCode::InvalidParameter);
    }
    const std::optional<ernc::domain::RegionDefinition> region =
        regionDefinitionFor(*regionCatalog_, path.toStdString());
    if (!region) {
        return static_cast<int>(ErrorCode::InvalidParameter);
    }
    const ernc::domain::Result<void> started = regionReader_->start(*region);
    if (!started.ok()) {
        return static_cast<int>(started.tryError() != nullptr ? *started.tryError() : ErrorCode::Unknown);
    }
    activeRegionPath_ = region->path;
    activeRegionIsWrite_ = false;
    return 0;
}

void DeviceRuntimeWorker::cancelRegionRead() {
    if (activeRegionIsWrite_) {
        return;
    }
    if (regionReader_ != nullptr) {
        regionReader_->cancel();
    }
}

int DeviceRuntimeWorker::startRegionWrite(const QString& path, const QByteArray& bytes) {
    using ernc::domain::ErrorCode;
    if (stack_ == nullptr || !stack_->connected()) {
        return static_cast<int>(ErrorCode::DeviceDisconnected);
    }
    if (regionWriter_ == nullptr || !regionCatalog_ || !activeRegionPath_.empty() || activeRead_.has_value()
        || stack_->batchReader().busy() || stack_->batchWriter().busy()) {
        return static_cast<int>(ErrorCode::InvalidParameter);
    }
    const std::optional<ernc::domain::RegionDefinition> region =
        regionDefinitionFor(*regionCatalog_, path.toStdString());
    if (!region || bytes.size() != static_cast<qsizetype>(region->size)) {
        return static_cast<int>(ErrorCode::InvalidParameter);
    }
    const std::span<const std::uint8_t> payload(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                                static_cast<std::size_t>(bytes.size()));
    const ernc::domain::Result<void> started = regionWriter_->start(*region, payload);
    if (!started.ok()) {
        return static_cast<int>(started.tryError() != nullptr ? *started.tryError() : ErrorCode::Unknown);
    }
    activeRegionPath_ = region->path;
    activeRegionIsWrite_ = true;
    return 0;
}

void DeviceRuntimeWorker::cancelRegionWrite() {
    if (!activeRegionIsWrite_) {
        return;
    }
    if (regionWriter_ != nullptr) {
        regionWriter_->cancel();
    }
}

bool DeviceRuntimeWorker::isConnected() const {
    return stack_ != nullptr && stack_->connected();
}

bool DeviceRuntimeWorker::isWriteBusy() const {
    return stack_ != nullptr && stack_->batchWriter().busy();
}

quint64 DeviceRuntimeWorker::sessionIdRaw() const {
    if (stack_ == nullptr || !stack_->connected()) {
        return 0;
    }
    return stack_->session().currentId().raw();
}

bool DeviceRuntimeWorker::startBatchRead(const QString& pageId) {
    if (stack_ == nullptr || !stack_->connected() || stack_->batchReader().busy()) {
        return false;
    }
    ernc::application::BatchReadScope scope;
    if (pageId == kBatchReadAllTag || isNavigatorAllPage(pageId)) {
        scope.kind = ernc::application::BatchReadScopeKind::All;
        lastReadCategory_ = "all_scalars";
    } else {
        const std::string category = pageId.toStdString();
        if (category.empty()) {
            return false;
        }
        scope.kind = ernc::application::BatchReadScopeKind::Category;
        scope.category = category;
        lastReadCategory_ = category;
    }
    const ernc::domain::Result<ernc::application::BatchReadTicket> started = stack_->batchReader().start(scope);
    if (!started.ok() || started.tryValue() == nullptr) {
        return false;
    }
    activeRead_ = started.value();
    parametersTouchedThisRead_.clear();
    for (const ernc::application::BatchFramePlan& frame : activeRead_->frames()) {
        for (const ernc::domain::ParameterId& id : frame.parameters) {
            parametersTouchedThisRead_.push_back(QString::fromStdString(id.text()));
        }
    }
    return true;
}

bool DeviceRuntimeWorker::startArrayRead(const QString& parameterId) {
    if (stack_ == nullptr || !stack_->connected() || stack_->batchReader().busy()) {
        return false;
    }
    const ernc::domain::Result<ernc::domain::ParameterId> parsed = ernc::domain::ParameterId::parse(parameterId.toStdString());
    if (!parsed.ok()) {
        return false;
    }
    ernc::application::BatchReadScope scope;
    scope.kind = ernc::application::BatchReadScopeKind::Selected;
    scope.selected.push_back(parsed.value());
    lastReadCategory_ = "array:" + parameterId.toStdString();
    const ernc::domain::Result<ernc::application::BatchReadTicket> started = stack_->batchReader().start(scope);
    if (!started.ok() || started.tryValue() == nullptr) {
        return false;
    }
    activeRead_ = started.value();
    parametersTouchedThisRead_.clear();
    parametersTouchedThisRead_.push_back(parameterId);
    return true;
}

int DeviceRuntimeWorker::startBatchWrite(const QString& pageId) {
    if (stack_ == nullptr || !stack_->connected() || stack_->batchWriter().busy()) {
        return static_cast<int>(ernc::domain::ErrorCode::DeviceDisconnected);
    }
    ernc::domain::WriteScope scope;
    if (pageId == kBatchWriteAllTag || isNavigatorAllPage(pageId)) {
        scope.kind = ernc::domain::WriteScopeKind::All;
    } else {
        scope.kind = ernc::domain::WriteScopeKind::Category;
        scope.category = pageId.toStdString();
    }
    const ernc::domain::Result<void> started = stack_->batchWriter().start(scope);
    if (!started.ok()) {
        if (started.tryError() != nullptr) {
            return static_cast<int>(*started.tryError());
        }
        return static_cast<int>(ernc::domain::ErrorCode::Unknown);
    }
    wasWriteBusy_ = stack_->batchWriter().busy();
    return 0;
}

DeviceRuntimeWorker::PumpReport DeviceRuntimeWorker::doPump() {
    PumpReport report;
    if (stack_ == nullptr || !stack_->connected()) {
        return report;
    }
    stack_->scheduler().pump();
    pumpBatchRead();
    pumpBatchWrite(report);
    pumpRegionRead(report);
    pumpRegionWrite(report);
    finishBatchReadIfIdle(report);
    report.writerBusy = stack_->batchWriter().busy();
    report.writeItems = stack_->batchWriter().items();
    report.writeInflight = stack_->batchWriter().inFlight();
    if (report.writeInflight.has_value()) {
        report.writePhase = stack_->scheduler().phaseOf(*report.writeInflight);
    }
    return report;
}

void DeviceRuntimeWorker::pumpRegionRead(PumpReport& report) {
    if (activeRegionIsWrite_ || regionReader_ == nullptr || activeRegionPath_.empty() || stack_ == nullptr) {
        return;
    }
    if (regionReader_->busy()) {
        if (const std::optional<ernc::application::CommandId> command = regionReader_->inFlight()) {
            if (std::optional<ernc::application::CommandOutcome> outcome = stack_->scheduler().takeResult(*command)) {
                regionReader_->apply(*outcome);
            }
        }
    }
    const ernc::application::RegionReadProgress progress = regionReader_->progress();
    report.regionPath = activeRegionPath_;
    report.regionFilledBytes = progress.filledBytes;
    report.regionPlannedBytes = progress.plannedBytes;
    if (regionReader_->busy()) {
        return;
    }
    report.regionFinished = true;
    report.regionIsWrite = false;
    const ernc::application::RegionSnapshot* snapshot = regionReader_->snapshot();
    if (snapshot != nullptr && snapshot->complete) {
        report.regionSuccess = true;
        report.regionBytes = snapshot->bytes;
    } else {
        report.regionSuccess = false;
        report.regionErrorCode = static_cast<int>(progress.error.value_or(ernc::domain::ErrorCode::Unknown));
    }
    activeRegionPath_.clear();
}

void DeviceRuntimeWorker::pumpRegionWrite(PumpReport& report) {
    if (regionWriter_ == nullptr || activeRegionPath_.empty() || !activeRegionIsWrite_ || stack_ == nullptr) {
        return;
    }
    if (regionWriter_->busy()) {
        if (const std::optional<ernc::application::CommandId> command = regionWriter_->inFlight()) {
            if (std::optional<ernc::application::CommandOutcome> outcome = stack_->scheduler().takeResult(*command)) {
                regionWriter_->apply(*outcome);
            }
        }
    }
    const ernc::application::RegionWriteProgress progress = regionWriter_->progress();
    report.regionPath = activeRegionPath_;
    report.regionFilledBytes = progress.verifiedBytes;
    if (regionCatalog_) {
        if (const std::optional<ernc::domain::RegionDefinition> region =
                regionDefinitionFor(*regionCatalog_, activeRegionPath_)) {
            report.regionPlannedBytes = region->size;
        }
    }
    report.regionIsWrite = true;
    if (regionWriter_->busy()) {
        return;
    }
    report.regionFinished = true;
    const ernc::application::RegionWriteSnapshot* snapshot = regionWriter_->snapshot();
  if (snapshot != nullptr && snapshot->verified) {
        report.regionSuccess = true;
    } else {
        report.regionSuccess = false;
        report.regionErrorCode = static_cast<int>(progress.error.value_or(ernc::domain::ErrorCode::Unknown));
    }
    activeRegionPath_.clear();
    activeRegionIsWrite_ = false;
}

void DeviceRuntimeWorker::pumpBatchRead() {
    if (!activeRead_.has_value() || stack_ == nullptr) {
        return;
    }
    const RuntimeTime readAt = now();
    for (const ernc::application::CommandId id : activeRead_->commands()) {
        if (std::optional<ernc::application::CommandOutcome> outcome = stack_->scheduler().takeResult(id)) {
            stack_->batchReader().apply(*outcome, readAt);
        }
    }
}

void DeviceRuntimeWorker::pumpBatchWrite(PumpReport& report) {
    if (stack_ == nullptr) {
        return;
    }
    const bool busy = stack_->batchWriter().busy();
    if (busy) {
        if (const std::optional<ernc::application::CommandId> inflight = stack_->batchWriter().inFlight()) {
            if (std::optional<ernc::application::CommandOutcome> outcome = stack_->scheduler().takeResult(*inflight)) {
                stack_->batchWriter().apply(*outcome, now());
            }
        }
    }
    if (wasWriteBusy_ && !stack_->batchWriter().busy()) {
        std::size_t acked = 0;
        std::size_t failed = 0;
        for (const ernc::application::BatchWriteItem& item : stack_->batchWriter().items()) {
            if (item.kind == ernc::application::BatchWriteKind::Verified) {
                ++acked;
            } else if (item.kind == ernc::application::BatchWriteKind::Failed
                       || item.kind == ernc::application::BatchWriteKind::Mismatch) {
                ++failed;
            }
        }
        std::ostringstream detail;
        detail << R"({"acked":)" << acked << R"(,"failed":)" << failed << "}";
        report.writeFinished = true;
        report.writeSuccess = failed == 0U;
        report.writeDetailJson = detail.str();
    }
    wasWriteBusy_ = stack_->batchWriter().busy();
}

void DeviceRuntimeWorker::finishBatchReadIfIdle(PumpReport& report) {
    if (!activeRead_.has_value() || stack_ == nullptr || stack_->batchReader().busy()) {
        return;
    }
    const ernc::application::BatchReadProgress progress = stack_->batchReader().progress();
    std::ostringstream detail;
    if (lastReadCategory_ == "all_scalars") {
        detail << R"({"scope":"all_scalars","completed":)" << progress.completed << R"(,"failed":)" << progress.failed
               << R"(,"skipped":)" << progress.skipped << "}";
    } else {
        detail << R"({"scope":"category","category":)" << jsonQuote(lastReadCategory_) << R"(,"completed":)"
               << progress.completed << R"(,"failed":)" << progress.failed << R"(,"skipped":)" << progress.skipped << "}";
    }
    report.readFinished = true;
    report.readSuccess = progress.failed == 0U;
    report.readDetailJson = detail.str();
    report.parametersUpdated = parametersTouchedThisRead_;
    activeRead_.reset();
}

DeviceRuntimeWorker::RuntimeTime DeviceRuntimeWorker::now() const noexcept {
    return std::chrono::system_clock::now();
}

}  // namespace ernc::presentation
