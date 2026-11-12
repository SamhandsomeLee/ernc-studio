#include "pages/switch_state.hpp"

#include "theme/theme_tokens.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace ernc::presentation {

SwitchStatePage::SwitchStatePage(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("erncSwitchStatePage"));
    labels_[0] = QStringLiteral("状态 0");
    labels_[1] = QStringLiteral("状态 1");
    labels_[2] = QStringLiteral("状态 2");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(kSpace4);
    for (const QString& label : labels_) {
        auto* line = new QHBoxLayout();
        auto* name = new QLabel(label, this);
        auto* value = new QLabel(QStringLiteral("未读取"), this);
        auto* time = new QLabel(QStringLiteral("—"), this);
        line->addWidget(name);
        line->addWidget(value);
        line->addWidget(time);
        line->addStretch(1);
        root->addLayout(line);
    }
    root->addStretch(1);
}

QString SwitchStatePage::stateLabel(int index) const {
    if (index < 0 || index >= 3) {
        return {};
    }
    return labels_[index];
}

QString SwitchStatePage::stateValue(int index) const {
    if (index < 0 || index >= 3) {
        return {};
    }
    return QStringLiteral("未读取");
}

QString SwitchStatePage::updatedAtText() const {
    return QStringLiteral("—");
}

}  // namespace ernc::presentation
