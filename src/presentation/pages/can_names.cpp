#include "pages/can_names.hpp"

#include "theme/theme_tokens.hpp"

#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QVBoxLayout>

namespace ernc::presentation {
namespace {

/// @brief 取出 JSON 里每一行 K 列的名称。
/// @param text 整个文件的 UTF-8 文本。调用方拥有原件。
/// @return 非空名称。顺序与文件一致。不解释行号。
std::vector<QString> namesFromText(const QString& text) {
    std::vector<QString> names;
    const QString key = QStringLiteral("\"K\": \"");
    int position = 0;
    while (position >= 0) {
        position = text.indexOf(key, position);
        if (position < 0) {
            break;
        }
        position += key.size();
        const int end = text.indexOf(QLatin1Char('"'), position);
        if (end < 0) {
            break;
        }
        const QString name = text.mid(position, end - position);
        if (!name.isEmpty()) {
            names.push_back(name);
        }
        position = end + 1;
    }
    return names;
}

}  // namespace

CanNamePage::CanNamePage(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("erncCanNamePage"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("erncCanNameScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("erncCanNameContent"));
    auto* list = new QVBoxLayout(content);
    list->setContentsMargins(0, 0, 0, 0);
    list->setSpacing(kSpace4);
    list->addStretch(1);
    scroll->setWidget(content);
    root->addWidget(scroll);
}

void CanNamePage::loadFile(const QString& path) {
    names_.clear();
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        names_ = namesFromText(QString::fromUtf8(file.readAll()));
    }
    auto* scroll = findChild<QScrollArea*>(QStringLiteral("erncCanNameScroll"));
    if (scroll == nullptr) {
        return;
    }
    auto* content = scroll->widget();
    if (content == nullptr) {
        return;
    }
    auto* list = qobject_cast<QVBoxLayout*>(content->layout());
    if (list == nullptr) {
        return;
    }
    while (QLayoutItem* child = list->takeAt(0)) {
        delete child->widget();
        delete child;
    }
    for (const QString& name : names_) {
        auto* line = new QHBoxLayout();
        auto* nameLabel = new QLabel(name, content);
        auto* status = new QLabel(QStringLiteral("未接入"), content);
        auto* freshness = new QLabel(QStringLiteral("无"), content);
        line->addWidget(nameLabel, 1);
        line->addWidget(status);
        line->addWidget(freshness);
        list->addLayout(line);
    }
    list->addStretch(1);
}

int CanNamePage::nameCount() const {
    return static_cast<int>(names_.size());
}

QString CanNamePage::nameAt(int index) const {
    if (index < 0 || index >= nameCount()) {
        return {};
    }
    return names_[static_cast<std::size_t>(index)];
}

QString CanNamePage::statusAt(int index) const {
    if (index < 0 || index >= nameCount()) {
        return {};
    }
    return QStringLiteral("未接入");
}

QString CanNamePage::freshnessAt(int index) const {
    if (index < 0 || index >= nameCount()) {
        return {};
    }
    return QStringLiteral("无");
}

}  // namespace ernc::presentation
