#include "task_cards/task_card_panel.hpp"

#include "theme/theme_tokens.hpp"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ernc::presentation {
namespace {

/// @brief 构造当前页和全工程操作区，按钮只转发操作意图。
/// @param panel 借用的信号发出者，由调用方拥有。
/// @param parent 通过 Qt 父子关系拥有返回的操作区。
QWidget* makeCardPage(TaskCardPanel* panel, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    layout->setSpacing(kSpace8);

    auto* readCurrent = new QPushButton(QStringLiteral("读取当前页"), page);
    readCurrent->setObjectName(QStringLiteral("erncTaskReadCurrent"));
    auto* writeCurrent = new QPushButton(QStringLiteral("写入当前页"), page);
    writeCurrent->setObjectName(QStringLiteral("erncTaskWriteModified"));
    layout->addWidget(readCurrent);
    layout->addWidget(writeCurrent);
    layout->addSpacing(kSpace4);

    auto* hint = new QLabel(
        QStringLiteral("全工程操作仅处理标量。写入前可查看范围和跳过项。"), page);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* readAll = new QPushButton(QStringLiteral("读取全部可读参数"), page);
    readAll->setObjectName(QStringLiteral("erncTaskReadAll"));
    auto* writeAll = new QPushButton(QStringLiteral("全工程写入计划"), page);
    writeAll->setObjectName(QStringLiteral("erncTaskWriteAll"));

    layout->addWidget(readAll);
    layout->addWidget(writeAll);
    layout->addStretch(1);

    QObject::connect(readCurrent, &QPushButton::clicked, panel, &TaskCardPanel::readCurrentPageRequested);
    QObject::connect(writeCurrent, &QPushButton::clicked, panel, &TaskCardPanel::writeCurrentPageRequested);
    QObject::connect(readAll, &QPushButton::clicked, panel, &TaskCardPanel::readAllScalarsRequested);
    QObject::connect(writeAll, &QPushButton::clicked, panel, &TaskCardPanel::writeAllDirtyRequested);

    return page;
}

}  // namespace

TaskCardPanel::TaskCardPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("erncTaskCards"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(makeCardPage(this, this));
}

}  // namespace ernc::presentation
