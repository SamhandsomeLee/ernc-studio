#include "inspector/inspector_panel.hpp"

#include "ernc/domain/catalog.hpp"
#include "inspector/inspector_context.hpp"
#include "parameters/fake_parameter_rows.hpp"
#include "theme/theme_tokens.hpp"

#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

namespace ernc::presentation {
namespace {

/// @brief 做一个顶对齐、可换行的标签页。
/// @param objectName 页面的稳定对象名。调用方拥有原件，本函数复制它。
/// @param parent 页签控件。它通过 Qt 父子关系拥有返回的页面。
/// @param label 成功时写入页面里的文字控件。调用方不另接所有权。
/// @return 带边距的页面。
QWidget* makePage(const QString& objectName, QWidget* parent, QLabel*& label) {
    auto* page = new QWidget(parent);
    page->setObjectName(objectName);
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    label = new QLabel(page);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setWordWrap(true);
    layout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addStretch(1);
    return page;
}

} // namespace

InspectorPanel::InspectorPanel(const ernc::domain::ParameterCatalog* catalog, QWidget* parent)
    : QWidget(parent)
    , catalog_(catalog) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tabs = new QTabWidget(this);
    tabs->setObjectName(QStringLiteral("erncInspectorTabs"));
    tabs->addTab(makePage(QStringLiteral("erncInspectorProperties"), tabs, propertiesLabel_),
        QStringLiteral("Properties"));
    tabs->addTab(makePage(QStringLiteral("erncInspectorInfo"), tabs, infoLabel_),
        QStringLiteral("Info"));
    tabs->addTab(makePage(QStringLiteral("erncInspectorDiagnostics"), tabs, diagnosticsLabel_),
        QStringLiteral("Diagnostics"));
    layout->addWidget(tabs);
    showParameter({});
}

void InspectorPanel::showParameter(std::string_view parameterId) {
    parameterId_.assign(parameterId.begin(), parameterId.end());
    const ernc::domain::ParameterDefinition* definition = nullptr;
    if (catalog_ != nullptr && !parameterId_.empty()) {
        definition = catalog_->find(parameterId_);
    }
    const InspectorText text = describeInspector(parameterId_, definition, findFakeParameterRow(parameterId_));
    propertiesLabel_->setText(QString::fromStdString(text.properties));
    infoLabel_->setText(QString::fromStdString(text.info));
    diagnosticsLabel_->setText(QString::fromStdString(text.diagnostics));
}

} // namespace ernc::presentation
