#include "task_cards/task_card_panel.hpp"

#include "theme/theme_tokens.hpp"

#include <QLabel>
#include <QPushButton>
#include <QToolBox>
#include <QVBoxLayout>

namespace ernc::presentation {
namespace {

QWidget* makeCardPage(TaskCardPanel* panel, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    layout->setSpacing(kSpace8);

    auto* hint = new QLabel(
        QStringLiteral("标量批量 FC03/FC16。读取/写入「所有」时跳过 check_data 与 ERNC_TF 等区域，不触发传函 BIN 传输。"), page);
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* readCurrent = new QPushButton(QStringLiteral("读取当前参数"), page);
    readCurrent->setObjectName(QStringLiteral("erncTaskReadCurrent"));
    auto* writeModified = new QPushButton(QStringLiteral("写入修改参数"), page);
    writeModified->setObjectName(QStringLiteral("erncTaskWriteModified"));
    auto* readAll = new QPushButton(QStringLiteral("读取所有参数"), page);
    readAll->setObjectName(QStringLiteral("erncTaskReadAll"));
    auto* writeAll = new QPushButton(QStringLiteral("写入所有参数"), page);
    writeAll->setObjectName(QStringLiteral("erncTaskWriteAll"));

    layout->addWidget(readCurrent);
    layout->addWidget(writeModified);
    layout->addWidget(readAll);
    layout->addWidget(writeAll);
    layout->addStretch(1);

    QObject::connect(readCurrent, &QPushButton::clicked, panel, &TaskCardPanel::readCurrentPageRequested);
    QObject::connect(writeModified, &QPushButton::clicked, panel, &TaskCardPanel::writeModifiedPageRequested);
    QObject::connect(readAll, &QPushButton::clicked, panel, &TaskCardPanel::readAllScalarsRequested);
    QObject::connect(writeAll, &QPushButton::clicked, panel, &TaskCardPanel::writeAllDirtyRequested);

    return page;
}

}  // namespace

TaskCardPanel::TaskCardPanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* cards = new QToolBox(this);
    cards->setObjectName(QStringLiteral("erncTaskCards"));
    cards->addItem(makeCardPage(this, cards), QStringLiteral("常用操作"));
    {
        auto* favorites = new QWidget(cards);
        auto* favLayout = new QVBoxLayout(favorites);
        favLayout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
        auto* label = new QLabel(QStringLiteral("未接入"), favorites);
        label->setWordWrap(true);
        favLayout->addWidget(label);
        favLayout->addStretch(1);
        cards->addItem(favorites, QStringLiteral("收藏与分析"));
    }
    cards->setCurrentIndex(0);
    layout->addWidget(cards);
}

}  // namespace ernc::presentation
