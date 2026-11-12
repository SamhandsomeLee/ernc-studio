#pragma once

#include "ernc/infrastructure/logging/diagnostic_log.hpp"
#include "operation_center/operation_log.hpp"

#include <string>

namespace ernc::presentation {

/// @brief 把结构化诊断日志压缩成操作中心可显示的文字。
class DiagnosticLogPresenter {
public:
    static std::string summarizeRecent(const ernc::infrastructure::DiagnosticLog& log, std::size_t maxLines = 32);

    /// @brief 用最近事件刷新 `OperationLog`（Job/Command 摘要，不含逐帧 hex）。
    static void refreshOperationLog(const ernc::infrastructure::DiagnosticLog& log, OperationLog& target,
                                    std::size_t maxLines = 32);
};

}  // namespace ernc::presentation
