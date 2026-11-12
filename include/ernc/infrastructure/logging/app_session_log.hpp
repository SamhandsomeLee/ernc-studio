#pragma once

#include "ernc/infrastructure/logging/diagnostic_log.hpp"

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace ernc::infrastructure {

/// @brief 一次桌面运行的会话日志：内存环形缓冲 + 追加写 `operations.jsonl`。
///
/// 不依赖 Qt。sessionDirectory 由调用方用本机路径规则建好。
class AppSessionLog {
public:
    /// @brief 绑定本次运行的输出目录。
    /// @param sessionDirectory 本次会话目录。不存在时会创建。调用方保有路径字符串的生命周期语义由本类复制保存。
    /// @param sessionId 桌面会话号，写入每条事件。
    explicit AppSessionLog(std::filesystem::path sessionDirectory, std::uint64_t sessionId);

    /// @brief 记录 UI 操作并落盘一行 JSON。
    /// @param action 稳定动作名，例如 `region.import.bin`。
    /// @param detail 复现用 JSON 对象文本，不含外层花括号时可被包装。
    /// @param success 操作是否成功。
    /// @param error 失败时的错误码。成功时通常为 Unknown。
    void recordUiAction(std::string_view action, std::string_view detail, bool success,
                        ernc::domain::ErrorCode error = ernc::domain::ErrorCode::Unknown);

    /// @brief 借出结构化缓冲，供操作中心摘要和设备层 attach。
    DiagnosticLog& diagnosticLog() noexcept;

    /// @brief 本机上的会话目录。
    const std::filesystem::path& sessionDirectory() const noexcept;

    /// @brief 写出 `operations.jsonl` 快照与 `wire_trace.jsonl`（可为空）。
    ernc::domain::Result<void> exportSnapshot() const;

    /// @brief 把一行纯文本追加到 `session.log`，给人直接阅读。
    void appendReadableLine(std::string_view line);

private:
    void appendJsonlRecord(const LogEvent& event);

    DiagnosticLog diagnosticLog_;
    std::filesystem::path sessionDirectory_;
    std::uint64_t sessionId_;
};

}  // namespace ernc::infrastructure
