#include "theme/theme_sample.hpp"

#include "theme/status_badge.hpp"
#include "theme/theme_tokens.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace ernc::presentation {

QWidget* createThemeSample(QWidget* parent) {
    auto* sample = new QWidget(parent);
    sample->setObjectName(QStringLiteral("erncThemeSample"));

    auto* header = new QLabel(QStringLiteral("ERNC Studio"), sample);
    header->setObjectName(QStringLiteral("erncSampleHeader"));

    auto* title = new QLabel(QStringLiteral("状态"), sample);
    title->setObjectName(QStringLiteral("erncGroupTitle"));

    auto* caption = new QLabel(
        QStringLiteral("每个状态都有图标和文字。颜色只是补充。这里显示的是固定说法，不是设备当前值，也不连接设备。"),
        sample);
    caption->setObjectName(QStringLiteral("erncSecondaryText"));
    caption->setWordWrap(true);

    constexpr int kColumns = 2;
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(kSpace16);
    grid->setVerticalSpacing(kSpace8);
    grid->setColumnStretch(kColumns, 1);
    int index = 0;
    for (const StatusPresentation& status : kStatusPresentations) {
        grid->addWidget(new StatusBadge(status.kind, sample), index / kColumns, index % kColumns, Qt::AlignLeft);
        ++index;
    }

    auto* body = new QVBoxLayout;
    body->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace24);
    body->setSpacing(kSpace12);
    body->addWidget(title);
    body->addWidget(caption);
    body->addLayout(grid);
    body->addStretch(1);

    auto* layout = new QVBoxLayout(sample);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    layout->addLayout(body);
    return sample;
}

} // namespace ernc::presentation
