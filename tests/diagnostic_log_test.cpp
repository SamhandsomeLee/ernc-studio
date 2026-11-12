#include "ernc/infrastructure/logging/diagnostic_log.hpp"
#include "retry_policy.hpp"
#include "scheduler.hpp"
#include "session.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {

using ernc::application::CommandId;
using ernc::application::CommandOp;
using ernc::application::CommandOutcome;
using ernc::application::CommandPriority;
using ernc::application::CommandRequest;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::FailedCommand;
using ernc::application::ReadCommandResult;
using ernc::application::ReadRetryLimit;
using ernc::application::RetryTime;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::SessionId;
using ernc::infrastructure::DiagnosticLog;
using ernc::infrastructure::LogEvent;
using ernc::infrastructure::LogEventKind;
using ernc::infrastructure::WireDirection;
using ernc::infrastructure::WireTraceRecord;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;

constexpr std::chrono::milliseconds kFixtureTimeout{100};
const RetryTime kStart = RetryTime{std::chrono::milliseconds{0}};

constexpr std::uint8_t kOneSlotResponse[] = {0x09, 0x01, 0x03, 0x04, 0x78, 0x56, 0x34, 0x12, 0x94, 0x4E};

/// @brief 供调度器与诊断日志联调用的假端口。不打开真实串口。
class Fake final : public IDeviceTransport {
public:
    Result<void> open() override {
        open_ = true;
        return Result<void>::success();
    }

    Result<void> close() override {
        if (!open_) {
            return Result<void>::success();
        }
        open_ = false;
        TransportStep step;
        step.kind = TransportStep::Kind::Closed;
        queued_.push_back(std::move(step));
        return Result<void>::success();
    }

    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        if (!open_) {
            return Result<void>::failure(ErrorCode::DeviceDisconnected);
        }
        submitted.emplace_back(bytes.begin(), bytes.end());
        return Result<void>::success();
    }

    TransportStep poll() override {
        if (queued_.empty()) {
            return TransportStep{};
        }
        TransportStep step = std::move(queued_.front());
        queued_.erase(queued_.begin());
        return step;
    }

    void queueBytes(std::span<const std::uint8_t> bytes) {
        TransportStep step;
        step.kind = TransportStep::Kind::Bytes;
        step.bytes.assign(bytes.begin(), bytes.end());
        queued_.push_back(std::move(step));
    }

    std::vector<std::vector<std::uint8_t>> submitted;

private:
    bool open_{false};
    std::vector<TransportStep> queued_;
};

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool connectSession(DeviceSession& session) {
    const Result<SessionId> connected = session.connect();
    return connected.ok() && connected.tryError() == nullptr && connected.tryValue() != nullptr &&
           connected.tryValue()->valid();
}

CommandRequest readRequest(std::uint16_t firstSlot, std::uint16_t slotCount) {
    CommandRequest request;
    request.op = CommandOp::Read;
    request.priority = CommandPriority::Normal;
    request.firstSlot = firstSlot;
    request.slotCount = slotCount;
    request.slaveAddress = 0x01;
    return request;
}

bool enqueueRead(CommandScheduler& scheduler, std::uint16_t firstSlot, std::uint16_t slotCount, CommandId& id) {
    const Result<CommandId> result = scheduler.enqueue(readRequest(firstSlot, slotCount));
    const CommandId* held = result.tryValue();
    if (!result.ok() || result.tryError() != nullptr || held == nullptr || !held->valid()) {
        return false;
    }
    id = *held;
    return true;
}

std::vector<LogEvent> eventsForSession(const DiagnosticLog& log, std::uint64_t sessionId) {
    std::vector<LogEvent> matched;
    for (const LogEvent& event : log.recentEvents(log.eventCount())) {
        if (event.sessionId == sessionId) {
            matched.push_back(event);
        }
    }
    return matched;
}

bool hasEventKind(const std::vector<LogEvent>& events, LogEventKind kind) {
    for (const LogEvent& event : events) {
        if (event.kind == kind) {
            return true;
        }
    }
    return false;
}

bool regionProgressThrottle() {
    DiagnosticLog log;
    using Clock = std::chrono::steady_clock;
    const Clock::time_point t0 = Clock::time_point{};
    const Clock::time_point tEarly = t0 + std::chrono::milliseconds{100};
    const Clock::time_point tLate = t0 + std::chrono::milliseconds{600};

    log.resetRegionProgressThrottle();
    if (!log.shouldEmitRegionProgress(t0, 1, 10)) {
        return fail("region_progress_throttle");
    }
    if (log.shouldEmitRegionProgress(tEarly, 1, 10)) {
        return fail("region_progress_throttle");
    }
    if (!log.shouldEmitRegionProgress(tEarly, 2, 10)) {
        return fail("region_progress_throttle");
    }
    if (!log.shouldEmitRegionProgress(tLate, 2, 10)) {
        return fail("region_progress_throttle");
    }
    if (log.shouldEmitRegionProgress(t0, 0, 0)) {
        return fail("region_progress_throttle");
    }
    return true;
}

bool wireLinksCommand() {
    Fake fake;
    DiagnosticLog log;
    DeviceSession session(fake);
    session.attachDiagnosticLog(&log);
    CommandScheduler scheduler(session, fake);
    scheduler.attachDiagnosticLog(&log);
    if (!connectSession(session)) {
        return fail("wire_links_command");
    }

    CommandId id = CommandId::none();
    if (!enqueueRead(scheduler, 0, 1, id)) {
        return fail("wire_links_command");
    }
    fake.queueBytes(kOneSlotResponse);
    scheduler.pump();

    const std::vector<LogEvent> events = log.eventsForCommand(id.raw());
    const std::vector<WireTraceRecord> wire = log.wireForCommand(id.raw());
    if (!hasEventKind(events, LogEventKind::CommandEnqueued) ||
        !hasEventKind(events, LogEventKind::CommandFinished)) {
        return fail("wire_links_command");
    }
    bool sawTx = false;
    bool sawRx = false;
    for (const WireTraceRecord& record : wire) {
        if (record.commandId != id.raw()) {
            return fail("wire_links_command");
        }
        if (record.direction == WireDirection::Tx) {
            sawTx = !record.frameHex.empty();
        }
        if (record.direction == WireDirection::Rx) {
            sawRx = !record.frameHex.empty();
        }
    }
    if (!sawTx || !sawRx) {
        return fail("wire_links_command");
    }
    const std::optional<CommandOutcome> outcome = scheduler.takeResult(id);
    if (!outcome.has_value()) {
        return fail("wire_links_command");
    }
    return true;
}

bool retryDistinctTxAttempts() {
    Fake fake;
    DiagnosticLog diagnostic;
    DeviceSession session(fake);
    session.attachDiagnosticLog(&diagnostic);
    CommandScheduler scheduler(session, fake);
    scheduler.attachDiagnosticLog(&diagnostic);
    if (!connectSession(session)) {
        return fail("retry_distinct_tx_attempts");
    }

    CommandId id = CommandId::none();
    if (!enqueueRead(scheduler, 0, 1, id)) {
        return fail("retry_distinct_tx_attempts");
    }
    const ReadRetryLimit limit{kFixtureTimeout, 2};
    scheduler.pump(kStart, limit);
    scheduler.pump(kStart + kFixtureTimeout, limit);

    const std::vector<WireTraceRecord> wire = diagnostic.wireForCommand(id.raw());
    std::size_t txCount = 0;
    std::uint8_t sawAttempt1 = 0;
    std::uint8_t sawAttempt2 = 0;
    for (const WireTraceRecord& record : wire) {
        if (record.direction != WireDirection::Tx) {
            continue;
        }
        ++txCount;
        if (record.attempt == 1U) {
            sawAttempt1 = 1U;
        }
        if (record.attempt == 2U) {
            sawAttempt2 = 1U;
        }
    }
    if (txCount != 2U || sawAttempt1 == 0U || sawAttempt2 == 0U) {
        return fail("retry_distinct_tx_attempts");
    }
    if (fake.submitted.size() != 2 || fake.submitted[0] != fake.submitted[1]) {
        return fail("retry_distinct_tx_attempts");
    }
    scheduler.pump(kStart + kFixtureTimeout + kFixtureTimeout, limit);
    const std::vector<LogEvent> events = diagnostic.eventsForCommand(id.raw());
    bool finishedWithRetryExhausted = false;
    for (const LogEvent& event : events) {
        if (event.kind == LogEventKind::CommandFinished && !event.success &&
            event.error == ErrorCode::RetryExhausted) {
            finishedWithRetryExhausted = true;
        }
    }
    if (!finishedWithRetryExhausted) {
        return fail("retry_distinct_tx_attempts");
    }
    return true;
}

bool exportReplayBundle() {
    Fake fake;
    DiagnosticLog log;
    DeviceSession session(fake);
    session.attachDiagnosticLog(&log);
    CommandScheduler scheduler(session, fake);
    scheduler.attachDiagnosticLog(&log);
    if (!connectSession(session)) {
        return fail("export_replay_bundle");
    }

    CommandId id = CommandId::none();
    if (!enqueueRead(scheduler, 3, 1, id)) {
        return fail("export_replay_bundle");
    }
    fake.queueBytes(kOneSlotResponse);
    scheduler.pump();
    scheduler.takeResult(id);

    const ernc::infrastructure::DiagnosticExport bundle = log.exportJsonl();
    const std::string idToken = "\"command_id\":" + std::to_string(id.raw());
    if (bundle.operationsJsonl.find(idToken) == std::string::npos ||
        bundle.wireJsonl.find(idToken) == std::string::npos) {
        return fail("export_replay_bundle");
    }
    if (bundle.wireJsonl.find("\"frame_hex\":\"") == std::string::npos) {
        return fail("export_replay_bundle");
    }
    return true;
}

bool failedCommandRecordsSlotAndError() {
    Fake fake;
    DiagnosticLog log;
    DeviceSession session(fake);
    session.attachDiagnosticLog(&log);
    CommandScheduler scheduler(session, fake);
    scheduler.attachDiagnosticLog(&log);
    if (!connectSession(session)) {
        return fail("failed_command_records_slot_and_error");
    }

    CommandId id = CommandId::none();
    if (!enqueueRead(scheduler, 5, 2, id)) {
        return fail("failed_command_records_slot_and_error");
    }
    const ReadRetryLimit limit{kFixtureTimeout, 1};
    scheduler.pump(kStart, limit);
    scheduler.pump(kStart + kFixtureTimeout, limit);
    scheduler.takeResult(id);

    const std::vector<LogEvent> events = log.eventsForCommand(id.raw());
    for (const LogEvent& event : events) {
        if (event.kind != LogEventKind::CommandFinished) {
            continue;
        }
        if (event.firstSlot == 5U && event.slotCount == 2U && !event.success &&
            event.error == ErrorCode::ResponseTimeout) {
            return true;
        }
    }
    return fail("failed_command_records_slot_and_error");
}

bool logAppendAndQueryBySession() {
    DiagnosticLog log;
    Fake fake;
    DeviceSession session(fake);
    session.attachDiagnosticLog(&log);
    CommandScheduler scheduler(session, fake);
    scheduler.attachDiagnosticLog(&log);

    if (!connectSession(session)) {
        return fail("log_append_and_query_by_session");
    }
    const std::uint64_t firstSession = session.currentId().raw();

    CommandId id = CommandId::none();
    if (!enqueueRead(scheduler, 0, 1, id)) {
        return fail("log_append_and_query_by_session");
    }
    const Result<void> disconnected = session.disconnect();
    if (!disconnected.ok() || disconnected.tryError() != nullptr) {
        return fail("log_append_and_query_by_session");
    }
    scheduler.pump();

    if (!connectSession(session)) {
        return fail("log_append_and_query_by_session");
    }
    const std::uint64_t secondSession = session.currentId().raw();
    if (firstSession == secondSession) {
        return fail("log_append_and_query_by_session");
    }

    LogEvent foreign;
    foreign.kind = LogEventKind::JobStarted;
    foreign.sessionId = 999U;
    foreign.jobId = 42U;
    foreign.jobKind = "parameter.read";
    log.appendEvent(std::move(foreign));

    const std::vector<LogEvent> firstEvents = eventsForSession(log, firstSession);
    const std::vector<LogEvent> secondEvents = eventsForSession(log, secondSession);
    if (!hasEventKind(firstEvents, LogEventKind::SessionConnected) ||
        !hasEventKind(firstEvents, LogEventKind::CommandFinished)) {
        return fail("log_append_and_query_by_session");
    }
    if (!hasEventKind(secondEvents, LogEventKind::SessionConnected)) {
        return fail("log_append_and_query_by_session");
    }
    for (const LogEvent& event : secondEvents) {
        if (event.sessionId != secondSession) {
            return fail("log_append_and_query_by_session");
        }
    }
    if (eventsForSession(log, 999U).size() != 1U) {
        return fail("log_append_and_query_by_session");
    }
    bool sawDisconnect = false;
    for (const LogEvent& event : log.recentEvents(log.eventCount())) {
        if (event.kind == LogEventKind::SessionDisconnected) {
            sawDisconnect = true;
        }
    }
    if (!sawDisconnect) {
        return fail("log_append_and_query_by_session");
    }
    return true;
}

bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "region_progress_throttle") {
        return regionProgressThrottle();
    }
    if (name == "wire_links_command") {
        return wireLinksCommand();
    }
    if (name == "retry_distinct_tx_attempts") {
        return retryDistinctTxAttempts();
    }
    if (name == "export_replay_bundle") {
        return exportReplayBundle();
    }
    if (name == "failed_command_records_slot_and_error") {
        return failedCommandRecordsSlotAndError();
    }
    if (name == "log_append_and_query_by_session") {
        return logAppendAndQueryBySession();
    }
    recognized = false;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_diagnostic_log_tests <case>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
