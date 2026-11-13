#include "ernc/infrastructure/logging/app_session_log.hpp"

#include <fstream>
#include <sstream>

namespace ernc::infrastructure {
namespace {

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
        case LogEventKind::UiAction:
            return "ui.action";
    }
    return "unknown";
}

std::string formatJsonlLine(const LogEvent& event) {
    std::ostringstream line;
    line << "{\"kind\":\"" << eventKindToken(event.kind) << "\",\"session_id\":" << event.sessionId
         << ",\"job_id\":" << event.jobId << ",\"command_id\":" << event.commandId << ",\"success\":"
         << (event.success ? "true" : "false") << ",\"error\":\"" << jsonEscape(errorCodeToken(event.error))
         << "\",\"action\":\"" << jsonEscape(event.jobKind) << "\",\"detail\":" << event.detail << '}';
    return line.str();
}

}  // namespace

AppSessionLog::AppSessionLog(std::filesystem::path sessionDirectory, std::uint64_t sessionId)
    : sessionDirectory_(std::move(sessionDirectory))
    , sessionId_(sessionId) {
    std::error_code error;
    std::filesystem::create_directories(sessionDirectory_, error);
}

void AppSessionLog::recordUiAction(std::string_view action, std::string_view detail, bool success,
                                   ernc::domain::ErrorCode error) {
    LogEvent event;
    event.kind = LogEventKind::UiAction;
    event.sessionId = sessionId_;
    event.jobKind = std::string(action);
    event.detail = std::string(detail);
    event.success = success;
    event.error = error;
    appendJsonlRecord(event);
    diagnosticLog_.appendEvent(std::move(event));
}

DiagnosticLog& AppSessionLog::diagnosticLog() noexcept {
    return diagnosticLog_;
}

const std::filesystem::path& AppSessionLog::sessionDirectory() const noexcept {
    return sessionDirectory_;
}

ernc::domain::Result<void> AppSessionLog::exportSnapshot() const {
    return DiagnosticLog::writeExportFiles(sessionDirectory_, diagnosticLog_);
}

void AppSessionLog::appendReadableLine(std::string_view line) {
    if (sessionDirectory_.empty()) {
        return;
    }
    const std::filesystem::path readable = sessionDirectory_ / "session.log";
    std::ofstream output(readable, std::ios::app | std::ios::binary);
    if (!output) {
        return;
    }
    output.write(line.data(), static_cast<std::streamsize>(line.size()));
    output.put('\n');
    output.flush();
}

void AppSessionLog::appendJsonlRecord(const LogEvent& event) {
    if (sessionDirectory_.empty()) {
        return;
    }
    const std::filesystem::path operations = sessionDirectory_ / "operations.jsonl";
    std::ofstream output(operations, std::ios::app | std::ios::binary);
    if (!output) {
        return;
    }
    const std::string line = formatJsonlLine(event);
    output.write(line.data(), static_cast<std::streamsize>(line.size()));
    output.put('\n');
    output.flush();
}

}  // namespace ernc::infrastructure
