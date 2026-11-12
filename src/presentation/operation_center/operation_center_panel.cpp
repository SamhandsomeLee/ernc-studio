#include "operation_center/operation_center_panel.hpp"

#include "ernc/infrastructure/logging/app_session_log.hpp"
#include "ernc/presentation/diagnostics/diagnostic_log_presenter.hpp"
#include "theme/theme_tokens.hpp"

#include <QLabel>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace ernc::presentation {
OperationCenterPanel::OperationCenterPanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    layout->setSpacing(kSpace8);

    diagnosisLabel_ = new QLabel(QStringLiteral("尚未绑定会话日志。"), this);
    diagnosisLabel_->setObjectName(QStringLiteral("erncOperationDiagnosis"));
    diagnosisLabel_->setWordWrap(true);
    layout->addWidget(diagnosisLabel_);

    auto* progressRow = new QHBoxLayout();
    progressRow->setSpacing(kSpace8);
    auto* progress = new QProgressBar(this);
    progress->setObjectName(QStringLiteral("erncOperationProgress"));
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setTextVisible(false);
    progressRow->addWidget(progress, 1);
    auto* progressText = new QLabel(QStringLiteral("没有进行中的设备任务。"), this);
    progressText->setWordWrap(true);
    progressRow->addWidget(progressText, 1);
    layout->addLayout(progressRow);

    auto* cancel = new QPushButton(QStringLiteral("取消"), this);
    cancel->setObjectName(QStringLiteral("erncOperationCancel"));
    cancel->setEnabled(false);
    layout->addWidget(cancel, 0, Qt::AlignLeft);

    logView_ = new QPlainTextEdit(this);
    logView_->setObjectName(QStringLiteral("erncOperationLog"));
    logView_->setReadOnly(true);
    logView_->setMaximumBlockCount(static_cast<int>(OperationLog::kCapacity));
    layout->addWidget(logView_, 1);
}

void OperationCenterPanel::bindSessionLog(ernc::infrastructure::AppSessionLog* log, std::function<void()> onRefresh) {
    sessionLog_ = log;
    onRefresh_ = std::move(onRefresh);
    refreshFromSession();
}

void OperationCenterPanel::refreshFromSession() {
    if (sessionLog_ == nullptr) {
        diagnosisLabel_->setText(QStringLiteral("尚未绑定会话日志。"));
        return;
    }
    const QString directory = QString::fromStdWString(sessionLog_->sessionDirectory().wstring());
    diagnosisLabel_->setText(QStringLiteral("会话日志目录：%1\noperations.jsonl 追加写入；退出时再导出快照。").arg(directory));
    log_ = OperationLog{};
    DiagnosticLogPresenter::refreshOperationLog(sessionLog_->diagnosticLog(), log_, 48);
    logView_->setPlainText(QString::fromStdString(log_.text()));
    if (onRefresh_) {
        onRefresh_();
    }
}

}  // namespace ernc::presentation
