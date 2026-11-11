#include "operation_center/operation_center_panel.hpp"

#include "theme/theme_tokens.hpp"

#include <QLabel>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace ernc::presentation {
namespace {

/// @brief 做一条标明不是通信记录的示例行。
/// @param number 示例序号，从 1 开始。没有物理单位。
/// @return 调用方拥有的一行文本。
std::string sampleLine(int number) {
    return "示例 " + std::to_string(number) + "。不是通信记录。";
}

} // namespace

OperationCenterPanel::OperationCenterPanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    layout->setSpacing(kSpace8);

    auto* diagnosis = new QLabel(QStringLiteral("没有设备诊断。"), this);
    diagnosis->setObjectName(QStringLiteral("erncOperationDiagnosis"));
    diagnosis->setWordWrap(true);
    layout->addWidget(diagnosis);

    auto* progressRow = new QHBoxLayout();
    progressRow->setSpacing(kSpace8);
    auto* progress = new QProgressBar(this);
    progress->setObjectName(QStringLiteral("erncOperationProgress"));
    // 刻度只用来把空任务画成空条。0 不是设备完成百分比。
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setTextVisible(false);
    progressRow->addWidget(progress, 1);
    auto* progressText = new QLabel(QStringLiteral("没有进行中的任务。"), this);
    progressText->setWordWrap(true);
    progressRow->addWidget(progressText, 1);
    layout->addLayout(progressRow);

    auto* cancel = new QPushButton(QStringLiteral("取消"), this);
    cancel->setObjectName(QStringLiteral("erncOperationCancel"));
    cancel->setEnabled(false);
    layout->addWidget(cancel, 0, Qt::AlignLeft);

    for (int number = 1; number <= 3; ++number) {
        log_.append(sampleLine(number));
    }
    auto* logView = new QPlainTextEdit(this);
    logView->setObjectName(QStringLiteral("erncOperationLog"));
    logView->setReadOnly(true);
    logView->setMaximumBlockCount(static_cast<int>(OperationLog::kCapacity));
    logView->setPlainText(QString::fromStdString(log_.text()));
    layout->addWidget(logView, 1);
}

} // namespace ernc::presentation
