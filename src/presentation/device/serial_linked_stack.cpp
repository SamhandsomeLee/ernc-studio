#include "device/serial_linked_stack.hpp"

#include "serial_transport.hpp"

namespace ernc::presentation {

SerialLinkedStack::SerialLinkedStack(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                                     ernc::domain::ParameterRuntimeStore& store, ernc::domain::ArrayRuntimeStore& arrayStore,
                                     ernc::serial::SerialTransport& transport)
    : catalog_{catalog}, layout_{layout}, store_{store}, arrayStore_{arrayStore}, transport_{transport} {}

SerialLinkedStack::~SerialLinkedStack() {
    resetKeepTransport();
}

ernc::domain::Result<ernc::domain::SessionId> SerialLinkedStack::connectLink() {
    if (connected()) {
        return ernc::domain::Result<ernc::domain::SessionId>::failure(ernc::domain::ErrorCode::InvalidParameter);
    }
    session_ = std::make_unique<ernc::application::DeviceSession>(transport_);
    scheduler_ = std::make_unique<ernc::application::CommandScheduler>(*session_, transport_);
    batchReader_ = std::make_unique<ernc::application::ParameterBatchReader>(catalog_, layout_, store_, *session_, *scheduler_,
                                                                             &arrayStore_);
    batchWriter_ = std::make_unique<ernc::application::ParameterBatchWriter>(catalog_, layout_, store_, *session_, *scheduler_);
    parameterReader_ =
        std::make_unique<ernc::application::ParameterReader>(catalog_, layout_, store_, *session_, *scheduler_);
    if (diagnosticLog_ != nullptr) {
        session_->attachDiagnosticLog(diagnosticLog_);
        scheduler_->attachDiagnosticLog(diagnosticLog_);
    }
    return session_->connect();
}

ernc::domain::Result<void> SerialLinkedStack::disconnectLink() {
    if (!connected()) {
        return ernc::domain::Result<void>::success();
    }
    if (scheduler_ != nullptr) {
        scheduler_->cancel();
    }
    const ernc::domain::Result<void> ended = session_->disconnect();
    resetKeepTransport();
    return ended;
}

bool SerialLinkedStack::connected() const noexcept {
    return session_ != nullptr && session_->state() == ernc::application::SessionState::Connected;
}

ernc::application::DeviceSession& SerialLinkedStack::session() {
    return *session_;
}

const ernc::application::DeviceSession& SerialLinkedStack::session() const {
    return *session_;
}

ernc::application::CommandScheduler& SerialLinkedStack::scheduler() {
    return *scheduler_;
}

ernc::application::ParameterBatchReader& SerialLinkedStack::batchReader() {
    return *batchReader_;
}

ernc::application::ParameterBatchWriter& SerialLinkedStack::batchWriter() {
    return *batchWriter_;
}

ernc::application::ParameterReader& SerialLinkedStack::parameterReader() {
    return *parameterReader_;
}

const ernc::application::ParameterReader& SerialLinkedStack::parameterReader() const {
    return *parameterReader_;
}

void SerialLinkedStack::attachDiagnosticLog(ernc::infrastructure::DiagnosticLog* log) noexcept {
    diagnosticLog_ = log;
    if (session_ != nullptr) {
        session_->attachDiagnosticLog(log);
    }
    if (scheduler_ != nullptr) {
        scheduler_->attachDiagnosticLog(log);
    }
}

void SerialLinkedStack::resetKeepTransport() {
    parameterReader_.reset();
    batchWriter_.reset();
    batchReader_.reset();
    scheduler_.reset();
    session_.reset();
}

}  // namespace ernc::presentation
