#include "ernc/infrastructure/logging/diagnostic_log.hpp"

#include <cstdio>
#include <fstream>
#include <span>
#include <sstream>

namespace ernc::infrastructure {
namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;

std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char character : text) {
        if (character == '"' || character == '\\') {
            out.push_back('\\');
        }
        out.push_back(character);
    }
    return out;
}

std::string eventKindToken(LogEventKind kind) {
    switch (kind) {
        case LogEventKind::SessionConnected:
            return "session.connected";
        case LogEventKind::SessionDisconnected:
            return "session.disconnected";
        case LogEventKind::JobStarted:
            return "job.started";
        case LogEventKind::JobProgress:
            return "job.progress";
        case LogEventKind::JobFinished:
            return "job.finished";
        case LogEventKind::CommandEnqueued:
            return "command.enqueued";
        case LogEventKind::CommandFinished:
            return "command.finished";
    }
    return "unknown";
}

}  // namespace

DiagnosticLog::DiagnosticLog(std::size_t eventCapacity, std::size_t wireCapacity)
    : eventCapacity_(eventCapacity), wireCapacity_(wireCapacity) {}

void DiagnosticLog::appendEvent(LogEvent event) {
    if (activeJobId_ != 0U && event.jobId == 0U) {
        event.jobId = activeJobId_;
    }
    events_.push_back(std::move(event));
    trimEvents();
}

void DiagnosticLog::appendWire(WireTraceRecord record) {
    if (activeJobId_ != 0U && record.jobId == 0U) {
        record.jobId = activeJobId_;
    }
    wire_.push_back(std::move(record));
    trimWire();
}

std::uint64_t DiagnosticLog::startJob(std::string_view jobKind, std::uint64_t sessionId) {
    const std::uint64_t jobId = nextJobId_++;
    LogEvent event;
    event.kind = LogEventKind::JobStarted;
    event.jobId = jobId;
    event.sessionId = sessionId;
    event.jobKind = std::string(jobKind);
    appendEvent(std::move(event));
    activeJobId_ = jobId;
    return jobId;
}

void DiagnosticLog::finishJob(std::uint64_t jobId, bool success, ErrorCode error) {
    LogEvent event;
    event.kind = LogEventKind::JobFinished;
    event.jobId = jobId;
    event.success = success;
    event.error = error;
    appendEvent(std::move(event));
    if (activeJobId_ == jobId) {
        activeJobId_ = 0;
    }
}

std::vector<LogEvent> DiagnosticLog::eventsForCommand(std::uint64_t commandId) const {
    std::vector<LogEvent> matched;
    for (const LogEvent& event : events_) {
        if (event.commandId == commandId) {
            matched.push_back(event);
        }
    }
    return matched;
}

std::vector<LogEvent> DiagnosticLog::recentEvents(std::size_t maxCount) const {
    std::vector<LogEvent> recent;
    if (maxCount == 0U || events_.empty()) {
        return recent;
    }
    const std::size_t start = events_.size() > maxCount ? events_.size() - maxCount : 0U;
    recent.reserve(events_.size() - start);
    for (std::size_t index = start; index < events_.size(); ++index) {
        recent.push_back(events_[index]);
    }
    return recent;
}

std::vector<WireTraceRecord> DiagnosticLog::wireForCommand(std::uint64_t commandId) const {
    std::vector<WireTraceRecord> matched;
    for (const WireTraceRecord& record : wire_) {
        if (record.commandId == commandId) {
            matched.push_back(record);
        }
    }
    return matched;
}

DiagnosticExport DiagnosticLog::exportJsonl() const {
    DiagnosticExport bundle;
    std::ostringstream operations;
    for (const LogEvent& event : events_) {
        operations << "{\"kind\":\"" << eventKindToken(event.kind) << "\",\"session_id\":" << event.sessionId
                   << ",\"job_id\":" << event.jobId << ",\"command_id\":" << event.commandId << ",\"first_slot\":"
                   << event.firstSlot << ",\"slot_count\":" << event.slotCount << ",\"attempt\":"
                   << static_cast<unsigned>(event.attempt) << ",\"success\":" << (event.success ? "true" : "false")
                   << ",\"error\":\"" << jsonEscape(errorCodeToken(event.error)) << "\",\"duration_ms\":"
                   << event.durationMs << ",\"job_kind\":\"" << jsonEscape(event.jobKind) << "\"}\n";
    }
    std::ostringstream wire;
    for (const WireTraceRecord& record : wire_) {
        wire << "{\"session_id\":" << record.sessionId << ",\"job_id\":" << record.jobId
             << ",\"command_id\":" << record.commandId << ",\"attempt\":" << static_cast<unsigned>(record.attempt)
             << ",\"direction\":\"" << (record.direction == WireDirection::Tx ? "tx" : "rx") << "\",\"first_slot\":"
             << record.firstSlot << ",\"slot_count\":" << record.slotCount << ",\"frame_hex\":\""
             << jsonEscape(record.frameHex) << "\"}\n";
    }
    bundle.operationsJsonl = operations.str();
    bundle.wireJsonl = wire.str();
    return bundle;
}

bool DiagnosticLog::shouldEmitRegionProgress(std::chrono::steady_clock::time_point now, std::size_t filledChunks,
                                             std::size_t plannedChunks) {
    if (plannedChunks == 0U) {
        return false;
    }
    const std::size_t percent = (filledChunks * 100U) / plannedChunks;
    if (!lastRegionUiEmit_) {
        lastRegionUiEmit_ = now;
        lastRegionPercentEmitted_ = percent;
        return true;
    }
    const auto elapsed = now - *lastRegionUiEmit_;
    if (percent != lastRegionPercentEmitted_ || elapsed >= std::chrono::milliseconds{500}) {
        lastRegionUiEmit_ = now;
        lastRegionPercentEmitted_ = percent;
        return true;
    }
    return false;
}

void DiagnosticLog::resetRegionProgressThrottle() noexcept {
    lastRegionUiEmit_.reset();
    lastRegionPercentEmitted_ = 100;
}

void DiagnosticLog::trimEvents() {
    while (events_.size() > eventCapacity_) {
        events_.pop_front();
    }
}

void DiagnosticLog::trimWire() {
    while (wire_.size() > wireCapacity_) {
        wire_.pop_front();
    }
}

std::string bytesToHex(std::span<const std::uint8_t> bytes) {
    std::string hex;
    hex.reserve(bytes.size() * 2U);
    for (std::uint8_t byte : bytes) {
        char buffer[3];
        std::snprintf(buffer, sizeof(buffer), "%02x", static_cast<unsigned>(byte));
        hex.append(buffer);
    }
    return hex;
}

Result<void> DiagnosticLog::writeExportFiles(const std::filesystem::path& directory, const DiagnosticLog& log) {
    const DiagnosticExport bundle = log.exportJsonl();
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return Result<void>::failure(ErrorCode::FileIoFailed);
    }
    const std::filesystem::path operations = directory / "operations.jsonl";
    const std::filesystem::path wire = directory / "wire_trace.jsonl";
    {
        std::ofstream output(operations, std::ios::binary | std::ios::trunc);
        if (!output) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
        output.write(bundle.operationsJsonl.data(), static_cast<std::streamsize>(bundle.operationsJsonl.size()));
    }
    {
        std::ofstream output(wire, std::ios::binary | std::ios::trunc);
        if (!output) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
        output.write(bundle.wireJsonl.data(), static_cast<std::streamsize>(bundle.wireJsonl.size()));
    }
    return Result<void>::success();
}

std::string errorCodeToken(ErrorCode code) {
    switch (code) {
        case ErrorCode::Unknown:
            return "Unknown";
        case ErrorCode::InvalidParameter:
            return "InvalidParameter";
        case ErrorCode::DeviceDisconnected:
            return "DeviceDisconnected";
        case ErrorCode::CrcMismatch:
            return "CrcMismatch";
        case ErrorCode::ResponseMismatch:
            return "ResponseMismatch";
        case ErrorCode::ResponseTimeout:
            return "ResponseTimeout";
        case ErrorCode::ModbusException:
            return "ModbusException";
        case ErrorCode::OperationCancelled:
            return "OperationCancelled";
        case ErrorCode::ReadBackMismatch:
            return "ReadBackMismatch";
        case ErrorCode::SerialOpenFailed:
            return "SerialOpenFailed";
        case ErrorCode::SerialIoFailed:
            return "SerialIoFailed";
        case ErrorCode::ReadOnlyViolation:
            return "ReadOnlyViolation";
        case ErrorCode::FileIoFailed:
            return "FileIoFailed";
        case ErrorCode::DatabaseFailed:
            return "DatabaseFailed";
        case ErrorCode::FrameLengthInvalid:
            return "FrameLengthInvalid";
        case ErrorCode::RetryExhausted:
            return "RetryExhausted";
        case ErrorCode::PayloadSizeMismatch:
            return "PayloadSizeMismatch";
        case ErrorCode::RegionDecodeFailed:
            return "RegionDecodeFailed";
    }
    return "Unknown";
}

}  // namespace ernc::infrastructure
