#include "device/device_controller.hpp"

#include "batch_operation_tags.hpp"
#include "device/device_runtime_worker.hpp"
#include "parameters/write_plan_dialog.hpp"
#include "serial_transport.hpp"

#include "ernc/application/batch_write.hpp"
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
#include "ernc/domain/array_parameter.hpp"
#include "ernc/domain/array_runtime_store.hpp"
#include "ernc/infrastructure/logging/diagnostic_log.hpp"

#include <QMetaObject>

#include <sstream>

namespace ernc::presentation {
namespace {

constexpr std::size_t kCanMsgElementCount = 72U;

using ernc::application::BatchReadScope;
using ernc::application::BatchReadScopeKind;
using ernc::application::CommandId;
using ernc::application::CommandOutcome;
using ernc::domain::ErrorCode;
using ernc::domain::ParameterId;
using ernc::domain::Result;
using ernc::domain::SessionId;
using ernc::domain::WriteScope;
using ernc::domain::WriteScopeKind;

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

/// @brief 按完整路径取得父区域或直接子字段的独立定义。
/// @param catalog 区域目录，只在本次调用期间借用。
/// @param path 固件完整路径。
/// @return 找到时调用方拥有定义副本；未知路径为空。
std::optional<ernc::domain::RegionDefinition> regionDefinitionFor(
    const ernc::domain::RegionCatalog& catalog, const std::string_view path) {
    if (const ernc::domain::RegionDefinition* region = catalog.find(path)) {
        return *region;
    }
    for (const std::string_view root : {std::string_view{"ERNC_TF"}, std::string_view{"check_data"}}) {
        const ernc::domain::RegionDefinition* region = catalog.find(root);
        if (region == nullptr) {
            continue;
        }
        for (const ernc::domain::SubRegion& child : region->subregions) {
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

DeviceController::DeviceController(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                                   ernc::domain::ParameterRuntimeStore& store, ernc::domain::ArrayRuntimeStore& arrayStore,
                                   QObject* parent)
    : QObject(parent), catalog_{catalog}, layout_{layout}, store_{store}, arrayStore_{arrayStore},
              stack_{catalog, layout, store, arrayStore} {
    qRegisterMetaType<DeviceRuntimeWorker::PumpReport>("DeviceRuntimeWorker::PumpReport");
    const Result<ernc::domain::RegionCatalog> regions = ernc::domain::RegionCatalog::load(layout_);
    if (regions.ok() && regions.tryValue() != nullptr) {
        regionCatalog_ = regions.value();
    }
    canMsgPulseTimer_.setInterval(1000);
    canMsgPulseTimer_.setSingleShot(false);
    connect(&canMsgPulseTimer_, &QTimer::timeout, this, &DeviceController::onCanMsgPulseTick);
}

DeviceController::~DeviceController() {
    tearDownAll();
}

void DeviceController::setUiRecorder(const UiRecorder recorder) {
    uiRecorder_ = std::move(recorder);
}

void DeviceController::attachSessionDiagnosticLog(ernc::infrastructure::DiagnosticLog* log) noexcept {
    diagnosticLog_ = log;
    stack_.attachDiagnosticLog(log);
}

bool DeviceController::connected() const noexcept {
    if (linkKind_ == LinkKind::Simulator) {
        return stack_.connected();
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        bool live = false;
        QMetaObject::invokeMethod(
            serialWorker_, "isConnected", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, live));
        return live;
    }
    return false;
}

SessionId DeviceController::sessionId() const noexcept {
    if (linkKind_ == LinkKind::Simulator && stack_.connected()) {
        return stack_.session().currentId();
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        quint64 raw = 0;
        QMetaObject::invokeMethod(
            serialWorker_, "sessionIdRaw", Qt::BlockingQueuedConnection, Q_RETURN_ARG(quint64, raw));
        return SessionId::fromRaw(raw);
    }
    return SessionId::none();
}

ernc::application::CommandScheduler* DeviceController::scheduler() noexcept {
    if (linkKind_ != LinkKind::Simulator || !stack_.connected()) {
        return nullptr;
    }
    return &stack_.scheduler();
}

ernc::application::ParameterBatchWriter* DeviceController::batchWriter() noexcept {
    if (linkKind_ != LinkKind::Simulator || !stack_.connected()) {
        return nullptr;
    }
    return &stack_.batchWriter();
}

bool DeviceController::writeBusy() const noexcept {
    if (linkKind_ == LinkKind::Simulator && stack_.connected()) {
        return stack_.batchWriter().busy();
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        bool busy = false;
        QMetaObject::invokeMethod(
            serialWorker_, "isWriteBusy", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, busy));
        return busy;
    }
    return false;
}

void DeviceController::tearDownSerial() {
    if (serialWorker_ != nullptr && serialTransport_ != nullptr) {
        QMetaObject::invokeMethod(serialWorker_, "doDisconnect", Qt::BlockingQueuedConnection);
        delete serialWorker_;
        serialWorker_ = nullptr;
    }
    serialTransport_.reset();
    if (linkKind_ == LinkKind::Serial) {
        linkKind_ = LinkKind::None;
    }
}

void DeviceController::tearDownAll() {
    wasWriteBusy_ = false;
    stopCanMsgSimulationPulse();
    simulatorRegionReader_.reset();
    simulatorRegionWriter_.reset();
    activeRegionPath_.clear();
    activeRegionIsWrite_ = false;
    if (linkKind_ == LinkKind::Simulator && stack_.connected()) {
        stack_.scheduler().cancel();
        (void)stack_.disconnect();
    }
    tearDownSerial();
    activeRead_.reset();
    activeCanMsgRead_.reset();
    canMsgSnapshot_.clear();
    canMsgSnapshotValid_ = false;
    linkKind_ = LinkKind::None;
}

void DeviceController::connectSimulator() {
    if (linkKind_ == LinkKind::Simulator && stack_.connected()) {
        recordUi("device.simulator.connect", R"({"reason":"already_connected"})", false, ErrorCode::InvalidParameter);
        return;
    }
    tearDownSerial();
    if (stack_.connected()) {
        stack_.scheduler().cancel();
        (void)stack_.disconnect();
    }
    if (diagnosticLog_ != nullptr) {
        stack_.attachDiagnosticLog(diagnosticLog_);
    }
    const std::uint32_t mirrorSlots = ernc::application::maxLogicalSlotForCatalog(catalog_, layout_);
    const Result<SessionId> connected = stack_.connectSimulator();
    if (!connected.ok() || connected.tryValue() == nullptr) {
        const ErrorCode code = connected.tryError() != nullptr ? *connected.tryError() : ErrorCode::Unknown;
        std::ostringstream detail;
        detail << R"({"transport":"simulator","slots":)" << mirrorSlots << R"(,"error":")" << static_cast<int>(code)
               << "\"}";
        recordUi("device.simulator.connect", detail.str(), false, code);
        emit connectionChanged(false, SessionId::none());
        return;
    }
    linkKind_ = LinkKind::Simulator;
    simulatorRegionReader_ = std::make_unique<ernc::application::RegionReader>(stack_.session(), stack_.scheduler());
    simulatorRegionWriter_ = std::make_unique<ernc::application::RegionWriter>(stack_.session(), stack_.scheduler());
    std::ostringstream detail;
    detail << R"({"transport":"simulator","slots":)" << mirrorSlots << R"(,"session_id":)" << connected.value().raw()
           << "}";
    recordUi("device.simulator.connect", detail.str(), true);
    emit connectionChanged(true, connected.value());
    emit operationLogRefreshRequested();
    startCanMsgSimulationPulse();
}

void DeviceController::startCanMsgSimulationPulse() {
    if (linkKind_ != LinkKind::Simulator || !stack_.connected()) {
        return;
    }
    canMsgPulseHigh_ = true;
    (void)stack_.applyCanMsgPulse(canMsgPulseHigh_);
    canMsgPulseTimer_.start();
}

void DeviceController::stopCanMsgSimulationPulse() {
    canMsgPulseTimer_.stop();
}

void DeviceController::onCanMsgPulseTick() {
    if (linkKind_ != LinkKind::Simulator || !stack_.connected()) {
        canMsgPulseTimer_.stop();
        return;
    }
    canMsgPulseHigh_ = !canMsgPulseHigh_;
    (void)stack_.applyCanMsgPulse(canMsgPulseHigh_);
}

void DeviceController::connectSerial(const QString& portName, const int baudRate) {
    if (portName.isEmpty() || baudRate <= 0) {
        recordUi("device.serial.connect", R"({"reason":"invalid_endpoint"})", false, ErrorCode::InvalidParameter);
        return;
    }
    tearDownAll();
    serialTransport_ = std::make_unique<ernc::serial::SerialTransport>(portName.toStdString(), baudRate);
    serialTransport_->start();
    serialWorker_ = new DeviceRuntimeWorker(catalog_, layout_, store_, arrayStore_, serialTransport_.get());
    serialWorker_->moveToThread(serialTransport_->thread());
    if (diagnosticLog_ != nullptr) {
        serialWorker_->attachDiagnosticLog(diagnosticLog_);
    }
    bool ok = false;
    QMetaObject::invokeMethod(serialWorker_, "doConnect", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, ok));
    if (!ok) {
        std::ostringstream detail;
        detail << R"({"transport":"serial","port":)" << jsonQuote(portName.toStdString()) << R"(,"baud":)" << baudRate
               << R"(,"error":)" << static_cast<int>(ErrorCode::SerialOpenFailed) << "}";
        recordUi("device.serial.connect", detail.str(), false, ErrorCode::SerialOpenFailed);
        tearDownSerial();
        emit connectionChanged(false, SessionId::none());
        return;
    }
    linkKind_ = LinkKind::Serial;
    const SessionId session = sessionId();
    std::ostringstream detail;
    detail << R"({"transport":"serial","port":)" << jsonQuote(portName.toStdString()) << R"(,"baud":)" << baudRate
           << R"(,"session_id":)" << session.raw() << "}";
    recordUi("device.serial.connect", detail.str(), true);
    emit connectionChanged(true, session);
    emit operationLogRefreshRequested();
}

void DeviceController::disconnectDevice() {
    if (linkKind_ == LinkKind::None || !connected()) {
        recordUi("device.disconnect", R"({"reason":"not_connected"})", true);
        emit connectionChanged(false, SessionId::none());
        return;
    }
    const SessionId ending = sessionId();
    const std::string transport = linkKind_ == LinkKind::Serial ? "serial" : "simulator";
    tearDownAll();
    activeRead_.reset();
    std::ostringstream detail;
    detail << R"({"transport":)" << jsonQuote(transport) << R"(,"session_id":)" << ending.raw() << "}";
    recordUi("device.disconnect", detail.str(), true);
    emit connectionChanged(false, SessionId::none());
    emit operationLogRefreshRequested();
}

Result<void> DeviceController::startRegionRead(const QString& path) {
    if (!connected()) {
        return Result<void>::failure(ErrorCode::DeviceDisconnected);
    }
    if (!regionCatalog_ || !activeRegionPath_.empty()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const std::optional<ernc::domain::RegionDefinition> region =
        regionDefinitionFor(*regionCatalog_, path.toStdString());
    if (!region) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        int code = static_cast<int>(ErrorCode::Unknown);
        QMetaObject::invokeMethod(serialWorker_, "startRegionRead", Qt::BlockingQueuedConnection,
                                  Q_RETURN_ARG(int, code), Q_ARG(QString, path));
        if (code != 0) {
            return Result<void>::failure(static_cast<ErrorCode>(code));
        }
        activeRegionPath_ = region->path;
        activeRegionIsWrite_ = false;
        emit regionReadProgress(path, 0, region->size);
        return Result<void>::success();
    }
    if (linkKind_ != LinkKind::Simulator || simulatorRegionReader_ == nullptr || activeRead_.has_value()
        || activeCanMsgRead_.has_value() || stack_.batchReader().busy() || stack_.parameterReader().busy()
        || stack_.batchWriter().busy()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const Result<void> started = simulatorRegionReader_->start(*region);
    if (!started.ok()) {
        return started;
    }
    activeRegionPath_ = region->path;
    activeRegionIsWrite_ = false;
    emit regionReadProgress(path, 0, region->size);
    finishRegionReadSimulator();
    return Result<void>::success();
}

void DeviceController::cancelRegionRead() {
    if (activeRegionPath_.empty() || activeRegionIsWrite_) {
        return;
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        QMetaObject::invokeMethod(serialWorker_, "cancelRegionRead", Qt::QueuedConnection);
        return;
    }
    if (simulatorRegionReader_ != nullptr) {
        simulatorRegionReader_->cancel();
    }
}

Result<ernc::application::RegionWritePreview> DeviceController::previewRegionWrite(const QString& path,
                                                                                   const QByteArray& bytes) {
    if (!regionCatalog_) {
        return Result<ernc::application::RegionWritePreview>::failure(ErrorCode::InvalidParameter);
    }
    const std::optional<ernc::domain::RegionDefinition> region =
        regionDefinitionFor(*regionCatalog_, path.toStdString());
    if (!region) {
        return Result<ernc::application::RegionWritePreview>::failure(ErrorCode::InvalidParameter);
    }
    const std::span<const std::uint8_t> payload(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                                 static_cast<std::size_t>(bytes.size()));
    return ernc::application::previewRegionTransfer(*region, payload);
}

Result<void> DeviceController::startRegionWrite(const QString& path, const QByteArray& bytes) {
    if (!connected()) {
        return Result<void>::failure(ErrorCode::DeviceDisconnected);
    }
    if (!regionCatalog_ || !activeRegionPath_.empty()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const std::optional<ernc::domain::RegionDefinition> region =
        regionDefinitionFor(*regionCatalog_, path.toStdString());
    if (!region || bytes.size() != static_cast<qsizetype>(region->size)) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
  const std::span<const std::uint8_t> payload(reinterpret_cast<const std::uint8_t*>(bytes.constData()),
                                                 static_cast<std::size_t>(bytes.size()));
    const Result<ernc::application::RegionWritePreview> preview =
        ernc::application::previewRegionTransfer(*region, payload);
    if (!preview.ok() || preview.tryValue() == nullptr || !preview.value().commitPermitted) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        int code = static_cast<int>(ErrorCode::Unknown);
        QMetaObject::invokeMethod(serialWorker_, "startRegionWrite", Qt::BlockingQueuedConnection, Q_RETURN_ARG(int, code),
                                  Q_ARG(QString, path), Q_ARG(QByteArray, bytes));
        if (code != 0) {
            return Result<void>::failure(static_cast<ErrorCode>(code));
        }
        activeRegionPath_ = region->path;
        activeRegionIsWrite_ = true;
        emit regionWriteProgress(path, 0, region->size);
        return Result<void>::success();
    }
    if (linkKind_ != LinkKind::Simulator || simulatorRegionWriter_ == nullptr || activeRead_.has_value()
        || activeCanMsgRead_.has_value() || stack_.batchReader().busy() || stack_.parameterReader().busy()
        || stack_.batchWriter().busy()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const Result<void> started = simulatorRegionWriter_->start(*region, payload);
    if (!started.ok()) {
        return started;
    }
    activeRegionPath_ = region->path;
    activeRegionIsWrite_ = true;
    emit regionWriteProgress(path, 0, region->size);
    finishRegionWriteSimulator();
    return Result<void>::success();
}

void DeviceController::cancelRegionWrite() {
    if (activeRegionPath_.empty() || !activeRegionIsWrite_) {
        return;
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        QMetaObject::invokeMethod(serialWorker_, "cancelRegionWrite", Qt::QueuedConnection);
        return;
    }
    if (simulatorRegionWriter_ != nullptr) {
        simulatorRegionWriter_->cancel();
    }
}

void DeviceController::readCategory(const std::string& pageId) {
    if (pageId == "ALL") {
        readAllScalars();
        return;
    }
    if (!connected()) {
        recordUi("parameter.batch_read.start", R"({"reason":"disconnected"})", false, ErrorCode::DeviceDisconnected);
        emit readPageFinished(false, QStringLiteral("未连接设备"));
        return;
    }
    if (pageId.empty()) {
        recordUi("parameter.batch_read.start", R"({"reason":"empty_page"})", false, ErrorCode::InvalidParameter);
        emit readPageFinished(false, QStringLiteral("未选择页面"));
        return;
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        bool started = false;
        QMetaObject::invokeMethod(
            serialWorker_, "startBatchRead", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, started),
            Q_ARG(QString, QString::fromStdString(pageId)));
        if (!started) {
            recordUi("parameter.batch_read.start", R"({"reason":"start_failed"})", false, ErrorCode::InvalidParameter);
            emit readPageFinished(false, QStringLiteral("批量读入队失败"));
            return;
        }
        std::ostringstream detail;
        detail << R"({"scope":"category","category":)" << jsonQuote(pageId) << R"(,"transport":"serial"})";
        recordUi("parameter.batch_read.start", detail.str(), true);
        return;
    }
    if (stack_.batchReader().busy() || activeCanMsgRead_.has_value() || stack_.parameterReader().busy()) {
        recordUi("parameter.batch_read.start", R"({"reason":"reader_busy"})", false, ErrorCode::InvalidParameter);
        return;
    }
    BatchReadScope scope;
    scope.kind = BatchReadScopeKind::Category;
    scope.category = pageId;
    const Result<ernc::application::BatchReadTicket> started = stack_.batchReader().start(scope);
    if (!started.ok() || started.tryValue() == nullptr) {
        const ErrorCode code = started.tryError() != nullptr ? *started.tryError() : ErrorCode::Unknown;
        std::ostringstream detail;
        detail << R"({"scope":"category","category":)" << jsonQuote(pageId) << R"(,"error":)" << static_cast<int>(code)
               << "}";
        recordUi("parameter.batch_read.start", detail.str(), false, code);
        emit readPageFinished(false, QStringLiteral("批量读入队失败"));
        return;
    }
    activeRead_ = started.value();
    lastReadCategory_ = pageId;
    parametersTouchedThisRead_.clear();
    for (const ernc::application::BatchFramePlan& frame : activeRead_->frames()) {
        for (const ParameterId& id : frame.parameters) {
            parametersTouchedThisRead_.push_back(QString::fromStdString(id.text()));
        }
    }
    std::ostringstream detail;
    detail << R"({"scope":"category","category":)" << jsonQuote(pageId) << R"(,"frames":)" << activeRead_->frames().size()
           << R"(,"commands":)" << activeRead_->commands().size() << R"(,"transport":"simulator"})";
    recordUi("parameter.batch_read.start", detail.str(), true);
    pumpBatchReadSimulator();
    finishBatchReadIfIdleSimulator();
}

void DeviceController::readAllScalars() {
    if (!connected()) {
        recordUi("parameter.batch_read.start", R"({"reason":"disconnected"})", false, ErrorCode::DeviceDisconnected);
        emit readPageFinished(false, QStringLiteral("未连接设备"));
        return;
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        bool started = false;
        QMetaObject::invokeMethod(
            serialWorker_, "startBatchRead", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, started),
            Q_ARG(QString, kBatchReadAllTag));
        if (!started) {
            recordUi("parameter.batch_read.start", R"({"reason":"start_failed"})", false, ErrorCode::InvalidParameter);
            emit readPageFinished(false, QStringLiteral("批量读入队失败"));
            return;
        }
        recordUi("parameter.batch_read.start", R"({"scope":"all_scalars","transport":"serial"})", true);
        return;
    }
    if (stack_.batchReader().busy() || activeCanMsgRead_.has_value() || stack_.parameterReader().busy()) {
        recordUi("parameter.batch_read.start", R"({"reason":"reader_busy"})", false, ErrorCode::InvalidParameter);
        return;
    }
    BatchReadScope scope;
    scope.kind = BatchReadScopeKind::All;
    const Result<ernc::application::BatchReadTicket> started = stack_.batchReader().start(scope);
    if (!started.ok() || started.tryValue() == nullptr) {
        const ErrorCode code = started.tryError() != nullptr ? *started.tryError() : ErrorCode::Unknown;
        recordUi("parameter.batch_read.start", R"({"scope":"all_scalars","error":)" + std::to_string(static_cast<int>(code)) + "}",
                  false, code);
        emit readPageFinished(false, QStringLiteral("批量读入队失败"));
        return;
    }
    activeRead_ = started.value();
    lastReadCategory_ = "all_scalars";
    parametersTouchedThisRead_.clear();
    for (const ernc::application::BatchFramePlan& frame : activeRead_->frames()) {
        for (const ParameterId& id : frame.parameters) {
            parametersTouchedThisRead_.push_back(QString::fromStdString(id.text()));
        }
    }
    std::ostringstream detail;
    detail << R"({"scope":"all_scalars","frames":)" << activeRead_->frames().size() << R"(,"commands":)"
           << activeRead_->commands().size() << R"(,"transport":"simulator"})";
    recordUi("parameter.batch_read.start", detail.str(), true);
    pumpBatchReadSimulator();
    finishBatchReadIfIdleSimulator();
}

void DeviceController::pumpTick() {
    if (linkKind_ == LinkKind::Simulator) {
        pumpSimulator();
    } else if (linkKind_ == LinkKind::Serial) {
        pumpSerial();
    }
}

void DeviceController::pumpSimulator() {
    if (!stack_.connected()) {
        return;
    }
    stack_.scheduler().pump();
    pumpBatchReadSimulator();
    pumpParameterReadSimulator();
    pumpBatchWriteSimulator();
    pumpArrayIoSimulator();
    pumpRegionReadSimulator();
    pumpRegionWriteSimulator();
    if (WritePlanDialog* dialog = writePlanDialog_) {
        dialog->noteProgress();
    }
    finishBatchReadIfIdleSimulator();
    finishCanMsgReadIfIdleSimulator();
    finishRegionReadSimulator();
    finishRegionWriteSimulator();
}

void DeviceController::pumpRegionReadSimulator() {
    if (simulatorRegionReader_ == nullptr || activeRegionPath_.empty() || !simulatorRegionReader_->busy()) {
        return;
    }
    if (const std::optional<CommandId> command = simulatorRegionReader_->inFlight()) {
        if (std::optional<CommandOutcome> outcome = stack_.scheduler().takeResult(*command)) {
            simulatorRegionReader_->apply(*outcome);
        }
    }
    const ernc::application::RegionReadProgress progress = simulatorRegionReader_->progress();
    emit regionReadProgress(QString::fromStdString(activeRegionPath_), progress.filledBytes, progress.plannedBytes);
}

void DeviceController::finishRegionReadSimulator() {
    if (simulatorRegionReader_ == nullptr || activeRegionPath_.empty() || simulatorRegionReader_->busy()) {
        return;
    }
    const QString path = QString::fromStdString(activeRegionPath_);
    const ernc::application::RegionSnapshot* snapshot = simulatorRegionReader_->snapshot();
    if (snapshot != nullptr && snapshot->complete) {
        const QByteArray bytes(reinterpret_cast<const char*>(snapshot->bytes.data()),
                               static_cast<qsizetype>(snapshot->bytes.size()));
        emit regionReadProgress(path, snapshot->bytes.size(), snapshot->bytes.size());
        emit regionReadFinished(true, path, bytes, static_cast<int>(ErrorCode::Unknown));
    } else {
        const ernc::application::RegionReadProgress progress = simulatorRegionReader_->progress();
        const ErrorCode code = progress.error.value_or(ErrorCode::Unknown);
        emit regionReadFinished(false, path, {}, static_cast<int>(code));
    }
    activeRegionPath_.clear();
    activeRegionIsWrite_ = false;
}

void DeviceController::pumpRegionWriteSimulator() {
    if (simulatorRegionWriter_ == nullptr || activeRegionPath_.empty() || !activeRegionIsWrite_
        || !simulatorRegionWriter_->busy()) {
        return;
    }
    if (const std::optional<CommandId> command = simulatorRegionWriter_->inFlight()) {
        if (std::optional<CommandOutcome> outcome = stack_.scheduler().takeResult(*command)) {
            simulatorRegionWriter_->apply(*outcome);
        }
    }
    const ernc::application::RegionWriteProgress progress = simulatorRegionWriter_->progress();
    std::size_t plannedBytes = progress.verifiedBytes;
    if (regionCatalog_) {
        if (const std::optional<ernc::domain::RegionDefinition> region =
                regionDefinitionFor(*regionCatalog_, activeRegionPath_)) {
            plannedBytes = region->size;
        }
    }
    emit regionWriteProgress(QString::fromStdString(activeRegionPath_), progress.verifiedBytes, plannedBytes);
}

void DeviceController::finishRegionWriteSimulator() {
    if (simulatorRegionWriter_ == nullptr || activeRegionPath_.empty() || !activeRegionIsWrite_
        || simulatorRegionWriter_->busy()) {
        return;
    }
    const QString path = QString::fromStdString(activeRegionPath_);
    const ernc::application::RegionWriteSnapshot* snapshot = simulatorRegionWriter_->snapshot();
    if (snapshot != nullptr && snapshot->verified) {
        emit regionWriteProgress(path, snapshot->bytes.size(), snapshot->bytes.size());
        emit regionWriteFinished(true, path, static_cast<int>(ErrorCode::Unknown));
    } else {
        const ernc::application::RegionWriteProgress progress = simulatorRegionWriter_->progress();
        const ErrorCode code = progress.error.value_or(ErrorCode::Unknown);
        emit regionWriteFinished(false, path, static_cast<int>(code));
    }
    activeRegionPath_.clear();
    activeRegionIsWrite_ = false;
}

void DeviceController::readCanMsg() {
    if (!connected()) {
        recordUi("parameter.can_msg_read.start", R"({"reason":"disconnected"})", false, ErrorCode::DeviceDisconnected);
        emit canMsgUpdated(false);
        return;
    }
    if (linkKind_ != LinkKind::Simulator) {
        recordUi("parameter.can_msg_read.start", R"({"reason":"serial_unsupported"})", false, ErrorCode::InvalidParameter);
        return;
    }
    if (activeRead_.has_value() || stack_.batchReader().busy()) {
        return;
    }
    if (activeCanMsgRead_.has_value() || stack_.parameterReader().busy()) {
        return;
    }
    const Result<ParameterId> parsed = ParameterId::parse("Can_Msg");
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        recordUi("parameter.can_msg_read.start", R"({"reason":"invalid_id"})", false, ErrorCode::InvalidParameter);
        emit canMsgUpdated(false);
        return;
    }
    const Result<ernc::application::ReadTicket> started = stack_.parameterReader().read(*parsed.tryValue());
    if (!started.ok() || started.tryValue() == nullptr) {
        const ErrorCode code = started.tryError() != nullptr ? *started.tryError() : ErrorCode::Unknown;
        recordUi("parameter.can_msg_read.start", "{\"scope\":\"Can_Msg\"}", false, code);
        emit canMsgUpdated(false);
        return;
    }
    activeCanMsgRead_ = started.value();
    recordUi("parameter.can_msg_read.start", R"({"scope":"Can_Msg","transport":"simulator"})", true);
    pumpParameterReadSimulator();
    finishCanMsgReadIfIdleSimulator();
}

bool DeviceController::canMsgReadBusy() const noexcept {
    if (linkKind_ == LinkKind::Simulator && stack_.connected()) {
        return activeCanMsgRead_.has_value() || stack_.parameterReader().busy();
    }
    return false;
}

void DeviceController::pumpParameterReadSimulator() {
    if (!activeCanMsgRead_.has_value()) {
        return;
    }
    for (const CommandId id : activeCanMsgRead_->commands()) {
        if (std::optional<CommandOutcome> outcome = stack_.scheduler().takeResult(id)) {
            stack_.parameterReader().apply(*outcome, now());
        }
    }
}

void DeviceController::finishCanMsgReadIfIdleSimulator() {
    if (!activeCanMsgRead_.has_value() || stack_.parameterReader().busy()) {
        return;
    }
    const ernc::application::ParameterReadImage* image = stack_.parameterReader().image();
    bool success = false;
    canMsgSnapshot_.clear();
    canMsgSnapshotValid_ = false;
    if (image != nullptr && image->elements.size() == kCanMsgElementCount) {
        canMsgSnapshot_.reserve(image->elements.size());
        for (const ernc::domain::ScalarValue& element : image->elements) {
            const Result<float> number = element.asFloat();
            if (!number.ok()) {
                canMsgSnapshot_.clear();
                break;
            }
            canMsgSnapshot_.push_back(number.value());
        }
        if (canMsgSnapshot_.size() == kCanMsgElementCount) {
            canMsgSnapshotValid_ = true;
            success = true;
        }
    }
    std::ostringstream detail;
    detail << R"({"scope":"Can_Msg","elements":)" << canMsgSnapshot_.size() << "}";
    recordUi("parameter.can_msg_read.finish", detail.str(), success,
             success ? ErrorCode::Unknown : ErrorCode::ResponseMismatch);
    emit canMsgUpdated(success);
    activeCanMsgRead_.reset();
}

void DeviceController::pumpSerial() {
    if (serialWorker_ == nullptr) {
        return;
    }
    DeviceRuntimeWorker::PumpReport report;
    QMetaObject::invokeMethod(
        serialWorker_, "doPump", Qt::BlockingQueuedConnection, Q_RETURN_ARG(DeviceRuntimeWorker::PumpReport, report));
    if (report.readFinished) {
        recordUi("parameter.batch_read.finish", report.readDetailJson, report.readSuccess,
                 report.readSuccess ? ErrorCode::Unknown : ErrorCode::ResponseMismatch);
        emit parametersUpdated(sessionId(), report.parametersUpdated);
        emit readPageFinished(report.readSuccess, QString::fromStdString(report.readDetailJson));
        emit operationLogRefreshRequested();
    }
    if (report.writeFinished) {
        recordUi("parameter.batch_write.finish", report.writeDetailJson, report.writeSuccess,
                 report.writeSuccess ? ErrorCode::Unknown : ErrorCode::ResponseMismatch);
        publishWriteResult(report.writeItems);
        emit operationLogRefreshRequested();
    }
    if (report.regionPlannedBytes > 0U && !report.regionPath.empty()) {
        const QString path = QString::fromStdString(report.regionPath);
        if (report.regionIsWrite) {
            emit regionWriteProgress(path, report.regionFilledBytes, report.regionPlannedBytes);
        } else {
            emit regionReadProgress(path, report.regionFilledBytes, report.regionPlannedBytes);
        }
    }
    if (report.regionFinished) {
        const QString path = QString::fromStdString(report.regionPath);
        if (report.regionIsWrite) {
            emit regionWriteFinished(report.regionSuccess, path, report.regionErrorCode);
        } else {
            const QByteArray bytes(reinterpret_cast<const char*>(report.regionBytes.data()),
                                   static_cast<qsizetype>(report.regionBytes.size()));
            emit regionReadFinished(report.regionSuccess, path, bytes, report.regionErrorCode);
        }
        activeRegionPath_.clear();
        activeRegionIsWrite_ = false;
    }
    if (WritePlanDialog* dialog = writePlanDialog_) {
        dialog->noteProgressFromWorker(report.writeItems, report.writeInflight, report.writePhase);
    }
}

ErrorCode DeviceController::startWriteCategory(const QString& pageId) {
    if (isNavigatorAllPage(pageId)) {
        return startWriteAllDirty();
    }
    if (linkKind_ != LinkKind::Serial || serialWorker_ == nullptr) {
        return ErrorCode::DeviceDisconnected;
    }
    int code = static_cast<int>(ErrorCode::Unknown);
    QMetaObject::invokeMethod(
        serialWorker_, "startBatchWrite", Qt::BlockingQueuedConnection, Q_RETURN_ARG(int, code), Q_ARG(QString, pageId));
    if (code == 0) {
        std::ostringstream detail;
        detail << R"({"scope":"category","category":)" << jsonQuote(pageId.toStdString()) << R"(,"transport":"serial"})";
        recordUi("parameter.batch_write.start", detail.str(), true);
        return ErrorCode::Unknown;
    }
    recordUi("parameter.batch_write.start", "{\"transport\":\"serial\"}", false, static_cast<ErrorCode>(code));
    return static_cast<ErrorCode>(code);
}

ernc::domain::ErrorCode DeviceController::startWriteAllDirty() {
    if (!connected()) {
        recordUi("parameter.batch_write.start", R"({"reason":"disconnected"})", false, ErrorCode::DeviceDisconnected);
        return ErrorCode::DeviceDisconnected;
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        int code = static_cast<int>(ErrorCode::Unknown);
        QMetaObject::invokeMethod(serialWorker_, "startBatchWrite", Qt::BlockingQueuedConnection, Q_RETURN_ARG(int, code),
                                  Q_ARG(QString, kBatchWriteAllTag));
        if (code == 0) {
            recordUi("parameter.batch_write.start", R"({"scope":"all_dirty","transport":"serial"})", true);
            return ErrorCode::Unknown;
        }
        recordUi("parameter.batch_write.start", R"({"scope":"all_dirty","transport":"serial"})", false,
                  static_cast<ErrorCode>(code));
        return static_cast<ErrorCode>(code);
    }
    if (linkKind_ == LinkKind::Simulator && stack_.connected()) {
        if (stack_.batchWriter().busy()) {
            return ErrorCode::InvalidParameter;
        }
        WriteScope scope;
        scope.kind = WriteScopeKind::All;
        const Result<void> started = stack_.batchWriter().start(scope);
        if (!started.ok()) {
            const ErrorCode code = started.tryError() != nullptr ? *started.tryError() : ErrorCode::Unknown;
            recordUi("parameter.batch_write.start", R"({"scope":"all_dirty","transport":"simulator"})", false, code);
            return code;
        }
        recordUi("parameter.batch_write.start", R"({"scope":"all_dirty","transport":"simulator"})", true);
        wasWriteBusy_ = stack_.batchWriter().busy();
        return ErrorCode::Unknown;
    }
    return ErrorCode::DeviceDisconnected;
}

void DeviceController::refreshWritePlanFromWorker(WritePlanDialog* dialog) {
    if (dialog == nullptr || serialWorker_ == nullptr) {
        return;
    }
    DeviceRuntimeWorker::PumpReport report;
    QMetaObject::invokeMethod(
        serialWorker_, "doPump", Qt::BlockingQueuedConnection, Q_RETURN_ARG(DeviceRuntimeWorker::PumpReport, report));
    dialog->noteProgressFromWorker(report.writeItems, report.writeInflight, report.writePhase);
}

void DeviceController::setWritePlanDialog(WritePlanDialog* dialog) noexcept {
    writePlanDialog_ = dialog;
}

void DeviceController::recordUi(const std::string_view action, const std::string_view detail, const bool success,
                                const ErrorCode error) const {
    if (uiRecorder_.record) {
        uiRecorder_.record(action, detail, success, error);
    }
}

void DeviceController::pumpBatchReadSimulator() {
    if (!activeRead_.has_value()) {
        return;
    }
    const RuntimeTime readAt = now();
    for (const CommandId id : activeRead_->commands()) {
        if (std::optional<CommandOutcome> outcome = stack_.scheduler().takeResult(id)) {
            stack_.batchReader().apply(*outcome, readAt);
        }
    }
}

void DeviceController::pumpBatchWriteSimulator() {
    const bool busy = stack_.batchWriter().busy();
    if (busy) {
        if (const std::optional<CommandId> inflight = stack_.batchWriter().inFlight()) {
            if (std::optional<CommandOutcome> outcome = stack_.scheduler().takeResult(*inflight)) {
                stack_.batchWriter().apply(*outcome, now());
            }
        }
    }
    if ((busy || wasWriteBusy_) && !stack_.batchWriter().busy()) {
        std::size_t acked = 0;
        std::size_t failed = 0;
        for (const ernc::application::BatchWriteItem& item : stack_.batchWriter().items()) {
            if (item.kind == ernc::application::BatchWriteKind::Verified) {
                ++acked;
            } else if (item.kind == ernc::application::BatchWriteKind::Failed
                       || item.kind == ernc::application::BatchWriteKind::Mismatch) {
                ++failed;
            }
        }
        std::ostringstream detail;
        detail << R"({"acked":)" << acked << R"(,"failed":)" << failed << "}";
        recordUi("parameter.batch_write.finish", detail.str(), failed == 0U,
                 failed == 0U ? ErrorCode::Unknown : ErrorCode::ResponseMismatch);
        publishWriteResult(stack_.batchWriter().items());
        emit operationLogRefreshRequested();
    }
    wasWriteBusy_ = stack_.batchWriter().busy();
}

void DeviceController::publishWriteResult(const std::vector<ernc::application::BatchWriteItem>& items) {
    QStringList parameters;
    int acked = 0;
    int failed = 0;
    for (const auto& item : items) {
        switch (item.kind) {
        case ernc::application::BatchWriteKind::Verified:
            ++acked;
            break;
        case ernc::application::BatchWriteKind::Failed:
        case ernc::application::BatchWriteKind::Mismatch:
            ++failed;
            break;
        case ernc::application::BatchWriteKind::Excluded:
        case ernc::application::BatchWriteKind::NotOneSlot:
            continue;
        }
        parameters.append(QString::fromStdString(item.id.text()));
    }
    if (!parameters.isEmpty()) {
        emit parametersUpdated(sessionId(), parameters);
    }
    emit writeBatchFinished(acked, failed);
}

void DeviceController::finishBatchReadIfIdleSimulator() {
    if (!activeRead_.has_value() || stack_.batchReader().busy()) {
        return;
    }
    const ernc::application::BatchReadProgress progress = stack_.batchReader().progress();
    std::ostringstream detail;
    if (lastReadCategory_ == "all_scalars") {
        detail << R"({"scope":"all_scalars","completed":)" << progress.completed << R"(,"failed":)" << progress.failed
               << R"(,"skipped":)" << progress.skipped << "}";
    } else {
        detail << R"({"scope":"category","category":)" << jsonQuote(lastReadCategory_) << R"(,"completed":)"
               << progress.completed << R"(,"failed":)" << progress.failed << R"(,"skipped":)" << progress.skipped << "}";
    }
    const bool success = progress.failed == 0U;
    recordUi("parameter.batch_read.finish", detail.str(), success,
             success ? ErrorCode::Unknown : ErrorCode::ResponseMismatch);
    if (stack_.connected()) {
        emit parametersUpdated(stack_.session().currentId(), parametersTouchedThisRead_);
    }
    emit readPageFinished(success, QString::fromStdString(detail.str()));
    emit operationLogRefreshRequested();
    activeRead_.reset();
}

DeviceController::RuntimeTime DeviceController::now() const noexcept {
    return std::chrono::system_clock::now();
}

void DeviceController::readArrayParameter(const QString& parameterIdQString) {
    const QByteArray utf8 = parameterIdQString.toUtf8();
    const Result<ParameterId> parsed = ParameterId::parse(std::string_view{utf8.constData(), static_cast<std::size_t>(utf8.size())});
    if (!parsed.ok()) {
        recordUi("parameter.array_read.start", R"({"reason":"invalid_id"})", false, ErrorCode::InvalidParameter);
        return;
    }
    const ParameterId id = parsed.value();
    const ernc::domain::ParameterDefinition* definition = catalog_.find(id.text());
    if (definition == nullptr || !ernc::domain::shouldUseCompactArrayUi(definition, layout_)) {
        recordUi("parameter.array_read.start", R"({"reason":"not_array"})", false, ErrorCode::InvalidParameter);
        return;
    }
    if (!connected()) {
        recordUi("parameter.array_read.start", R"({"reason":"disconnected"})", false, ErrorCode::DeviceDisconnected);
        return;
    }
    if (linkKind_ == LinkKind::Serial && serialWorker_ != nullptr) {
        bool started = false;
        QMetaObject::invokeMethod(
            serialWorker_, "startArrayRead", Qt::BlockingQueuedConnection, Q_RETURN_ARG(bool, started),
            Q_ARG(QString, parameterIdQString));
        recordUi("parameter.array_read.start", "{\"transport\":\"serial\"}", started,
                 started ? ErrorCode::Unknown : ErrorCode::InvalidParameter);
        return;
    }
    if (activeArrayIo_.has_value() || stack_.batchReader().busy()) {
        recordUi("parameter.array_read.start", R"({"reason":"busy"})", false, ErrorCode::InvalidParameter);
        return;
    }
    const ernc::domain::LayoutResolve resolved = layout_.locate(*definition->firmwarePath);
    if (!resolved.ok()) {
        recordUi("parameter.array_read.start", R"({"reason":"unlocated"})", false, ErrorCode::InvalidParameter);
        return;
    }
    const ernc::domain::FieldLayout& field = resolved.field();
    const ernc::domain::ScalarValue::Kind kind = ernc::domain::scalarKindForField(field);
    ernc::domain::ArrayState& state = arrayStore_.ensure(id, field.slotCount, kind);
    auto transfer = std::make_unique<ernc::application::ArrayTransfer>(catalog_, layout_, state, stack_.session(),
                                                                       stack_.scheduler());
    const Result<void> started = transfer->read(id);
    if (!started.ok()) {
        const ErrorCode code = started.tryError() != nullptr ? *started.tryError() : ErrorCode::Unknown;
        recordUi("parameter.array_read.start", "{\"transport\":\"simulator\"}", false, code);
        return;
    }
    activeArrayIo_ = ActiveArrayIo{id, true, std::move(transfer)};
    recordUi("parameter.array_read.start", "{\"transport\":\"simulator\"}", true);
    pumpArrayIoSimulator();
}

void DeviceController::writeArrayParameter(const QString& parameterIdQString) {
    const QByteArray utf8 = parameterIdQString.toUtf8();
    const Result<ParameterId> parsed = ParameterId::parse(std::string_view{utf8.constData(), static_cast<std::size_t>(utf8.size())});
    if (!parsed.ok()) {
        recordUi("parameter.array_write.start", R"({"reason":"invalid_id"})", false, ErrorCode::InvalidParameter);
        return;
    }
    const ParameterId id = parsed.value();
    const ernc::domain::ParameterDefinition* definition = catalog_.find(id.text());
    if (definition == nullptr || !ernc::domain::shouldUseCompactArrayUi(definition, layout_)) {
        recordUi("parameter.array_write.start", R"({"reason":"not_array"})", false, ErrorCode::InvalidParameter);
        return;
    }
    if (!connected()) {
        recordUi("parameter.array_write.start", R"({"reason":"disconnected"})", false, ErrorCode::DeviceDisconnected);
        return;
    }
    if (linkKind_ == LinkKind::Serial) {
        recordUi("parameter.array_write.start", R"({"reason":"serial_not_supported"})", false, ErrorCode::InvalidParameter);
        return;
    }
    if (activeArrayIo_.has_value() || stack_.batchReader().busy() || stack_.batchWriter().busy()) {
        recordUi("parameter.array_write.start", R"({"reason":"busy"})", false, ErrorCode::InvalidParameter);
        return;
    }
    const ernc::domain::ArrayState* existing = arrayStore_.find(id);
    if (existing == nullptr || arrayStore_.dirtyCount(id) == 0U) {
        recordUi("parameter.array_write.start", R"({"reason":"nothing_dirty"})", false, ErrorCode::InvalidParameter);
        return;
    }
    const ernc::domain::LayoutResolve resolved = layout_.locate(*definition->firmwarePath);
    if (!resolved.ok()) {
        recordUi("parameter.array_write.start", R"({"reason":"unlocated"})", false, ErrorCode::InvalidParameter);
        return;
    }
    const ernc::domain::FieldLayout& field = resolved.field();
    const ernc::domain::ScalarValue::Kind kind = ernc::domain::scalarKindForField(field);
    ernc::domain::ArrayState& state = arrayStore_.ensure(id, field.slotCount, kind);
    auto transfer = std::make_unique<ernc::application::ArrayTransfer>(catalog_, layout_, state, stack_.session(),
                                                                       stack_.scheduler());
    const Result<void> started = transfer->write(id);
    if (!started.ok()) {
        const ErrorCode code = started.tryError() != nullptr ? *started.tryError() : ErrorCode::Unknown;
        recordUi("parameter.array_write.start", "{\"transport\":\"simulator\"}", false, code);
        return;
    }
    activeArrayIo_ = ActiveArrayIo{id, false, std::move(transfer)};
    recordUi("parameter.array_write.start", "{\"transport\":\"simulator\"}", true);
    pumpArrayIoSimulator();
}

void DeviceController::pumpArrayIoSimulator() {
    if (!activeArrayIo_.has_value() || activeArrayIo_->transfer == nullptr || !stack_.connected()) {
        return;
    }
    if (const std::optional<CommandId> inflight = activeArrayIo_->transfer->inFlight()) {
        if (std::optional<CommandOutcome> outcome = stack_.scheduler().takeResult(*inflight)) {
            activeArrayIo_->transfer->apply(*outcome);
        }
    }
    if (!activeArrayIo_->transfer->busy()) {
        const ernc::application::ArrayTransferProgress progress = activeArrayIo_->transfer->progress();
        const bool success = progress.status == ernc::application::ArrayTransferStatus::Completed;
        std::ostringstream detail;
        detail << R"({"id":)" << jsonQuote(activeArrayIo_->id.text()) << R"(,"status":)"
               << static_cast<int>(progress.status) << "}";
        recordUi(activeArrayIo_->reading ? "parameter.array_read.finish" : "parameter.array_write.finish", detail.str(),
                 success,
                 success ? ErrorCode::Unknown
                         : (progress.error ? *progress.error : ErrorCode::Unknown));
        if (success) {
            emit parametersUpdated(stack_.session().currentId(),
                                   {QString::fromStdString(activeArrayIo_->id.text())});
        }
        activeArrayIo_.reset();
    }
}

}  // namespace ernc::presentation
