#include "device/device_runtime_worker.hpp"

#include "write_verification.hpp"

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

}  // namespace

DeviceRuntimeWorker::DeviceRuntimeWorker(const ernc::domain::ParameterCatalog& catalog,
                                         const ernc::domain::FirmwareLayout& layout,
                                         ernc::domain::ParameterRuntimeStore& store,
                                         ernc::serial::SerialTransport* transport, QObject* parent)
    : QObject(parent), catalog_{catalog}, layout_{layout}, store_{store}, transport_{transport} {}

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
        stack_ = std::make_unique<SerialLinkedStack>(catalog_, layout_, store_, *transport_);
        if (diagnosticLog_ != nullptr) {
            stack_->attachDiagnosticLog(diagnosticLog_);
        }
    }
    const ernc::domain::Result<ernc::domain::SessionId> connected = stack_->connectLink();
    return connected.ok() && connected.tryValue() != nullptr;
}

void DeviceRuntimeWorker::doDisconnect() {
    if (stack_ != nullptr) {
        (void)stack_->disconnectLink();
    }
    activeRead_.reset();
    wasWriteBusy_ = false;
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
    const std::string category = pageId.toStdString();
    if (category.empty()) {
        return false;
    }
    ernc::application::BatchReadScope scope;
    scope.kind = ernc::application::BatchReadScopeKind::Category;
    scope.category = category;
    const ernc::domain::Result<ernc::application::BatchReadTicket> started = stack_->batchReader().start(scope);
    if (!started.ok() || started.tryValue() == nullptr) {
        return false;
    }
    activeRead_ = started.value();
    lastReadCategory_ = category;
    parametersTouchedThisRead_.clear();
    for (const ernc::application::BatchFramePlan& frame : activeRead_->frames()) {
        for (const ernc::domain::ParameterId& id : frame.parameters) {
            parametersTouchedThisRead_.push_back(QString::fromStdString(id.text()));
        }
    }
    return true;
}

int DeviceRuntimeWorker::startBatchWrite(const QString& pageId) {
    if (stack_ == nullptr || !stack_->connected() || stack_->batchWriter().busy()) {
        return static_cast<int>(ernc::domain::ErrorCode::DeviceDisconnected);
    }
    ernc::domain::WriteScope scope;
    scope.kind = ernc::domain::WriteScopeKind::Category;
    scope.category = pageId.toStdString();
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
    finishBatchReadIfIdle(report);
    report.writerBusy = stack_->batchWriter().busy();
    report.writeItems = stack_->batchWriter().items();
    report.writeInflight = stack_->batchWriter().inFlight();
    if (report.writeInflight.has_value()) {
        report.writePhase = stack_->scheduler().phaseOf(*report.writeInflight);
    }
    return report;
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
        std::size_t verified = 0;
        std::size_t failed = 0;
        for (const ernc::application::BatchWriteItem& item : stack_->batchWriter().items()) {
            if (item.kind == ernc::application::BatchWriteKind::Verified) {
                ++verified;
            } else if (item.kind == ernc::application::BatchWriteKind::Failed
                       || item.kind == ernc::application::BatchWriteKind::Mismatch) {
                ++failed;
            }
        }
        std::ostringstream detail;
        detail << R"({"verified":)" << verified << R"(,"failed":)" << failed << "}";
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
    detail << R"({"scope":"category","category":)" << jsonQuote(lastReadCategory_) << R"(,"completed":)"
           << progress.completed << R"(,"failed":)" << progress.failed << R"(,"skipped":)" << progress.skipped << "}";
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
