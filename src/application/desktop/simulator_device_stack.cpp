#include "ernc/application/simulator_device_stack.hpp"
#include "ernc/application/simulator_can_msg_pulse.hpp"
#include "ernc/application/simulator_mirror_seed.hpp"

#include "memory_transport.hpp"
#include "slave_image.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstring>

namespace ernc::application {
namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::SessionId;

void storeLittleEndian(std::uint32_t bits, std::uint8_t* out) {
    out[0] = static_cast<std::uint8_t>(bits & 0xFFU);
    out[1] = static_cast<std::uint8_t>((bits >> 8U) & 0xFFU);
    out[2] = static_cast<std::uint8_t>((bits >> 16U) & 0xFFU);
    out[3] = static_cast<std::uint8_t>((bits >> 24U) & 0xFFU);
}

}  // namespace

std::uint32_t maxLogicalSlotForCatalog(const ernc::domain::ParameterCatalog& catalog,
                                        const ernc::domain::FirmwareLayout& layout) {
    std::uint32_t end = 1U;
    for (const ernc::domain::ParameterDefinition& definition : catalog.entries()) {
        if (!definition.firmwarePath.has_value()) {
            continue;
        }
        const ernc::domain::LayoutResolve resolved = layout.locate(*definition.firmwarePath);
        if (!resolved.ok() || resolved.tryField() == nullptr) {
            continue;
        }
        const ernc::domain::FieldLayout& field = resolved.field();
        const std::uint32_t slotEnd = field.slot + field.slotCount;
        if (slotEnd > end) {
            end = slotEnd;
        }
    }
    return end;
}

std::vector<std::uint8_t> makeZeroSimulatorImage(const std::uint32_t slotCount) {
    if (slotCount == 0U) {
        return {};
    }
    return std::vector<std::uint8_t>(static_cast<std::size_t>(slotCount) * 4U, 0U);
}

void seedSimulatorDemoBytes(const std::span<std::uint8_t> bytes) {
    if (bytes.size() < 12U) {
        return;
    }
    storeLittleEndian(0x3f800000U, bytes.data());
    storeLittleEndian(0x3f800000U, bytes.data() + 4U);
    storeLittleEndian(0x3f800000U, bytes.data() + 8U);
}

SimulatorDeviceStack::SimulatorDeviceStack(const ernc::domain::ParameterCatalog& catalog,
                                             const ernc::domain::FirmwareLayout& layout,
                                             ernc::domain::ParameterRuntimeStore& store,
                                             ernc::domain::ArrayRuntimeStore& arrayStore)
    : catalog_{catalog}, layout_{layout}, store_{store}, arrayStore_{arrayStore} {}

SimulatorDeviceStack::~SimulatorDeviceStack() {
    reset();
}

ernc::domain::Result<SessionId> SimulatorDeviceStack::connectSimulator() {
    if (connected()) {
        return Result<SessionId>::failure(ErrorCode::InvalidParameter);
    }
    const std::uint32_t slots = maxLogicalSlotForCatalog(catalog_, layout_);
    std::vector<std::uint8_t> mirror = makeZeroSimulatorImage(slots);
    if (mirror.empty()) {
        return Result<SessionId>::failure(ErrorCode::InvalidParameter);
    }
    seedSimulatorMirrorDemoProfile(catalog_, layout_, mirror);
    image_ = std::make_unique<ernc::simulator::SlaveImage>(std::move(mirror));
    transport_ = std::make_unique<ernc::simulator::MemorySlaveTransport>(*image_);
    session_ = std::make_unique<DeviceSession>(*transport_);
    scheduler_ = std::make_unique<CommandScheduler>(*session_, *transport_);
    if (diagnosticLog_ != nullptr) {
        session_->attachDiagnosticLog(diagnosticLog_);
        scheduler_->attachDiagnosticLog(diagnosticLog_);
    }
    batchReader_ = std::make_unique<ParameterBatchReader>(catalog_, layout_, store_, *session_, *scheduler_, &arrayStore_);
    batchWriter_ = std::make_unique<ParameterBatchWriter>(catalog_, layout_, store_, *session_, *scheduler_);
    parameterReader_ =
        std::make_unique<ParameterReader>(catalog_, layout_, store_, *session_, *scheduler_);
    return session_->connect();
}

Result<void> SimulatorDeviceStack::disconnect() {
    if (!connected()) {
        return Result<void>::success();
    }
  scheduler_->cancel();
    const Result<void> ended = session_->disconnect();
    reset();
    return ended;
}

bool SimulatorDeviceStack::connected() const noexcept {
    return session_ != nullptr && session_->state() == SessionState::Connected;
}

DeviceSession& SimulatorDeviceStack::session() {
    return *session_;
}

const DeviceSession& SimulatorDeviceStack::session() const {
    return *session_;
}

CommandScheduler& SimulatorDeviceStack::scheduler() {
    return *scheduler_;
}

ParameterBatchReader& SimulatorDeviceStack::batchReader() {
    return *batchReader_;
}

ParameterBatchWriter& SimulatorDeviceStack::batchWriter() {
    return *batchWriter_;
}

const ParameterBatchWriter& SimulatorDeviceStack::batchWriter() const {
    return *batchWriter_;
}

ParameterReader& SimulatorDeviceStack::parameterReader() {
    return *parameterReader_;
}

const ParameterReader& SimulatorDeviceStack::parameterReader() const {
    return *parameterReader_;
}

bool SimulatorDeviceStack::applyCanMsgPulse(bool high) {
    if (image_ == nullptr) {
        return false;
    }
    return writeCanMsgMirrorLevel(*image_, layout_, high);
}

void SimulatorDeviceStack::attachDiagnosticLog(ernc::infrastructure::DiagnosticLog* log) noexcept {
    diagnosticLog_ = log;
    if (session_ != nullptr) {
        session_->attachDiagnosticLog(log);
    }
    if (scheduler_ != nullptr) {
        scheduler_->attachDiagnosticLog(log);
    }
}

void SimulatorDeviceStack::reset() {
    parameterReader_.reset();
    batchWriter_.reset();
    batchReader_.reset();
    scheduler_.reset();
    session_.reset();
    transport_.reset();
    image_.reset();
}

}  // namespace ernc::application
