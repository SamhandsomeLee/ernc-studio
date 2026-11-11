#include "task_cards/task_card_panel.hpp"

#include "theme/theme_tokens.hpp"

#include <QLabel>
#include <QToolBox>
#include <QVBoxLayout>

namespace ernc::presentation {
namespace {

/// @brief 做一张只含说明文字的任务卡页面。
/// @param text 页面正文。调用方拥有原件，本函数复制它。它不是一个可点击的命令。
/// @param parent 工具盒。它通过 Qt 父子关系拥有返回的页面。
/// @return 带边距的页面。
QWidget* makeCardPage(const QString& text, QWidget* parent) {
    auto* page = new QWidget(parent);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    auto* label = new QLabel(text, page);
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addStretch(1);
    return page;
}

} // namespace

TaskCardPanel::TaskCardPanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* cards = new QToolBox(this);
    cards->setObjectName(QStringLiteral("erncTaskCards"));
    cards->addItem(makeCardPage(QStringLiteral("读取当前参数  未接入\n写入已修改参数  未接入"), cards),
        QStringLiteral("常用操作"));
    cards->addItem(makeCardPage(QStringLiteral("未接入"), cards), QStringLiteral("批量传输"));
    cards->addItem(makeCardPage(QStringLiteral("未接入"), cards), QStringLiteral("收藏与分析"));
    cards->setCurrentIndex(0);
    layout->addWidget(cards);
}

} // namespace ernc::presentation
