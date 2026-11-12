#pragma once

#include "operation_center/operation_log.hpp"

#include <QWidget>

#include <functional>

class QLabel;
class QPlainTextEdit;

namespace ernc::infrastructure {
class AppSessionLog;
}

namespace ernc::presentation {

/// 操作中心：诊断摘要、进度占位和会话操作日志。
class OperationCenterPanel final : public QWidget {
public:
    explicit OperationCenterPanel(QWidget* parent = nullptr);

    /// @brief 绑定当前会话日志并刷新文本框。
    /// @param log 会话日志。只借用，必须比本控件活得久。
    /// @param onRefresh 刷新后通知。可为空。
    void bindSessionLog(ernc::infrastructure::AppSessionLog* log, std::function<void()> onRefresh = {});

    /// @brief 从结构化日志重画操作摘要。
    void refreshFromSession();

private:
    ernc::infrastructure::AppSessionLog* sessionLog_ = nullptr;
    std::function<void()> onRefresh_;
    OperationLog log_;
    QLabel* diagnosisLabel_ = nullptr;
    QPlainTextEdit* logView_ = nullptr;
};

} // namespace ernc::presentation
