#include "ernc/presentation/diagnostics/diagnostic_log_presenter.hpp"

#include <sstream>

namespace ernc::presentation {
namespace {

std::string lineForEvent(const ernc::infrastructure::LogEvent& event) {
    std::ostringstream line;
    switch (event.kind) {
        case ernc::infrastructure::LogEventKind::SessionConnected:
            line << "会话已连接 session=" << event.sessionId;
            break;
        case ernc::infrastructure::LogEventKind::SessionDisconnected:
            line << "会话已断开";
            break;
        case ernc::infrastructure::LogEventKind::JobStarted:
            line << "任务开始 " << event.jobKind << " job=" << event.jobId;
            break;
        case ernc::infrastructure::LogEventKind::JobProgress:
            line << "任务进度 job=" << event.jobId;
            break;
        case ernc::infrastructure::LogEventKind::JobFinished:
            line << "任务结束 job=" << event.jobId << (event.success ? " 成功" : " 失败");
            break;
        case ernc::infrastructure::LogEventKind::CommandEnqueued:
            line << "命令入队 cmd=" << event.commandId << " slot=" << event.firstSlot << "+"
                 << event.slotCount;
            break;
        case ernc::infrastructure::LogEventKind::CommandFinished:
            line << "命令结束 cmd=" << event.commandId << " slot=" << event.firstSlot << "+"
                 << event.slotCount << (event.success ? " 成功" : " 失败")
                 << " err=" << ernc::infrastructure::errorCodeToken(event.error);
            break;
    }
    return line.str();
}

}  // namespace

std::string DiagnosticLogPresenter::summarizeRecent(const ernc::infrastructure::DiagnosticLog& log,
                                                    std::size_t maxLines) {
    OperationLog scratch;
    refreshOperationLog(log, scratch, maxLines);
    return scratch.text();
}

void DiagnosticLogPresenter::refreshOperationLog(const ernc::infrastructure::DiagnosticLog& log, OperationLog& target,
                                                 std::size_t maxLines) {
    target = OperationLog{};
    for (const ernc::infrastructure::LogEvent& event : log.recentEvents(maxLines)) {
        target.append(lineForEvent(event));
    }
}

}  // namespace ernc::presentation
