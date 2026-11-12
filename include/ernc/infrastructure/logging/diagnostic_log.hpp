#pragma once

#include "ernc/domain/types.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::infrastructure {

enum class LogEventKind {
    SessionConnected,
    SessionDisconnected,
    JobStarted,
    JobProgress,
    JobFinished,
    CommandEnqueued,
    CommandFinished,
    /// 桌面上的用户操作或离线导入。细节放在 detail，便于导出后复现。
    UiAction,
};

struct LogEvent {
    LogEventKind kind{LogEventKind::CommandEnqueued};
    std::uint64_t sessionId{0};
    std::uint64_t jobId{0};
    std::uint64_t commandId{0};
    std::uint16_t firstSlot{0};
    std::uint16_t slotCount{0};
    ernc::domain::ErrorCode error{ernc::domain::ErrorCode::Unknown};
    std::uint64_t durationMs{0};
    std::string jobKind;
    /// JSON 或 key=value 载荷。UiAction 时记录路径、字节长度等复现字段。
    std::string detail;
    std::uint8_t attempt{0};
    bool success{false};
};

enum class WireDirection {
    Tx,
    Rx,
};

struct WireTraceRecord {
    std::uint64_t sessionId{0};
    std::uint64_t jobId{0};
    std::uint64_t commandId{0};
    std::uint8_t attempt{0};
    WireDirection direction{WireDirection::Tx};
    std::string frameHex;
    std::uint16_t firstSlot{0};
    std::uint16_t slotCount{0};
};

struct DiagnosticExport {
    std::string operationsJsonl;
    std::string wireJsonl;
};

/// @brief 结构化操作日志与 Modbus 线迹；环形缓冲，供查询与诊断导出。
class DiagnosticLog {
public:
    static constexpr std::size_t kDefaultEventCapacity = 4096;
    static constexpr std::size_t kDefaultWireCapacity = 8192;

    explicit DiagnosticLog(std::size_t eventCapacity = kDefaultEventCapacity,
                           std::size_t wireCapacity = kDefaultWireCapacity);

    void appendEvent(LogEvent event);
    void appendWire(WireTraceRecord record);

    /// @brief 记录一次 UI/离线操作并写入 detail。
    void appendUiAction(std::uint64_t sessionId, std::string_view action, std::string_view detail, bool success,
                        ernc::domain::ErrorCode error = ernc::domain::ErrorCode::Unknown);

    /// @return 新 Job 的 id。`jobKind` 描述用户操作，例如 `parameter.read`。
    std::uint64_t startJob(std::string_view jobKind, std::uint64_t sessionId);

    void finishJob(std::uint64_t jobId, bool success, ernc::domain::ErrorCode error = ernc::domain::ErrorCode::Unknown);

    void setActiveJob(std::uint64_t jobId) noexcept { activeJobId_ = jobId; }
    std::uint64_t activeJob() const noexcept { return activeJobId_; }

    [[nodiscard]] std::vector<LogEvent> eventsForCommand(std::uint64_t commandId) const;
    [[nodiscard]] std::vector<WireTraceRecord> wireForCommand(std::uint64_t commandId) const;
    [[nodiscard]] std::vector<LogEvent> recentEvents(std::size_t maxCount) const;
    [[nodiscard]] DiagnosticExport exportJsonl() const;

    [[nodiscard]] std::size_t eventCount() const noexcept { return events_.size(); }
    [[nodiscard]] std::size_t wireCount() const noexcept { return wire_.size(); }

    /// @brief Region 类 Job 进度 UI 节流：默认最多每 500ms 或百分比变化时放行。
    [[nodiscard]] bool shouldEmitRegionProgress(std::chrono::steady_clock::time_point now, std::size_t filledChunks,
                                                std::size_t plannedChunks);

    void resetRegionProgressThrottle() noexcept;

    /// @brief 写入 `<directory>/operations.jsonl` 与 `wire_trace.jsonl`。
    static ernc::domain::Result<void> writeExportFiles(const std::filesystem::path& directory,
                                                       const DiagnosticLog& log);

private:
    void trimEvents();
    void trimWire();

    std::size_t eventCapacity_;
    std::size_t wireCapacity_;
    std::deque<LogEvent> events_;
    std::deque<WireTraceRecord> wire_;
    std::uint64_t nextJobId_{1};
    std::uint64_t activeJobId_{0};
    std::optional<std::chrono::steady_clock::time_point> lastRegionUiEmit_;
    std::size_t lastRegionPercentEmitted_{100};
};

std::string bytesToHex(std::span<const std::uint8_t> bytes);
std::string errorCodeToken(ernc::domain::ErrorCode code);

}  // namespace ernc::infrastructure
