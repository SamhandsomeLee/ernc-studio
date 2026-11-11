#include "workspace/editor_tabs.hpp"

#include "theme/theme_tokens.hpp"
#include "workspace/editor_selection.hpp"

#include <QLabel>
#include <QVBoxLayout>

namespace ernc::presentation {
namespace {

/// @brief 做一个只显示选择文字的工作页。
/// @param objectName 页面的稳定对象名。调用方拥有原件，本函数复制它。
/// @param parent 页签控件。它通过 Qt 父子关系拥有返回的页面。
/// @param label 成功时写入页面里的文字控件。调用方不另接所有权。
/// @return 已经设置边距的页面。
QWidget* makeView(const QString& objectName, QWidget* parent, QLabel*& label) {
    auto* view = new QWidget(parent);
    view->setObjectName(objectName);
    auto* layout = new QVBoxLayout(view);
    layout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    label = new QLabel(view);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setWordWrap(true);
    layout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addStretch(1);
    return view;
}

} // namespace

EditorTabs::EditorTabs(QWidget* parent)
    : QTabWidget(parent) {
    setObjectName(QStringLiteral("erncEditorTabs"));
    setTabsClosable(false);
    setMovable(false);

    addTab(makeView(QStringLiteral("erncFunctionView"), this, functionLabel_),
        QStringLiteral("Function View"));
    addTab(makeView(QStringLiteral("erncParameterView"), this, parameterLabel_),
        QStringLiteral("Parameter View"));
    setCurrentIndex(0);
    refresh();
}

void EditorTabs::selectParameter(const QString& parameterId) {
    selection_.selectParameter(parameterId.toStdString());
    refresh();
}

void EditorTabs::selectPage(const QString& pageId) {
    selection_.selectPage(pageId.toStdString());
    refresh();
}

void EditorTabs::refresh() {
    const QString page = selection_.pageId().empty()
        ? QStringLiteral("未选择页面")
        : QString::fromStdString(selection_.pageId());
    const QString parameter = selection_.parameterId().empty()
        ? QStringLiteral("未选择参数")
        : QString::fromStdString(selection_.parameterId());
    const QString text = QStringLiteral("当前页面 %1\n参数 %2").arg(page, parameter);
    functionLabel_->setText(text);
    parameterLabel_->setText(text);
}

} // namespace ernc::presentation
