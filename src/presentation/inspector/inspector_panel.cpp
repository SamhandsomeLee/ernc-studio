#include "inspector/inspector_panel.hpp"

#include "ernc/domain/catalog.hpp"
#include "inspector/inspector_context.hpp"
#include "parameters/fake_parameter_rows.hpp"
#include "theme/theme_tokens.hpp"

#include <QLabel>
#include <QPushButton>
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
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("erncInspectorTabs"));
    tabs_->addTab(makePage(QStringLiteral("erncInspectorProperties"), tabs_, propertiesLabel_),
        QStringLiteral("属性"));
    tabs_->addTab(makePage(QStringLiteral("erncInspectorInfo"), tabs_, infoLabel_),
        QStringLiteral("信息"));
    auto* diagnosticsPage = makePage(QStringLiteral("erncInspectorDiagnostics"), tabs_, diagnosticsLabel_);
    auto* openOperationCenter = new QPushButton(QStringLiteral("操作中心"), diagnosticsPage);
    openOperationCenter->setObjectName(QStringLiteral("erncOpenOperationCenter"));
    if (auto* pageLayout = qobject_cast<QVBoxLayout*>(diagnosticsPage->layout())) {
        pageLayout->insertWidget(1, openOperationCenter, 0, Qt::AlignLeft);
    }
    connect(openOperationCenter, &QPushButton::clicked, this, [this] {
        if (operationCenterOpener_) {
            operationCenterOpener_();
        }
    });
    tabs_->addTab(diagnosticsPage, QStringLiteral("诊断"));
    layout->addWidget(tabs_);
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

void InspectorPanel::setOperationCenterOpener(std::function<void()> opener) {
    operationCenterOpener_ = std::move(opener);
}

void InspectorPanel::showNamedPage(const char* objectName) {
    if (tabs_ == nullptr || objectName == nullptr) {
        return;
    }
    const QString name = QString::fromUtf8(objectName);
    for (int index = 0; index < tabs_->count(); ++index) {
        QWidget* page = tabs_->widget(index);
        if (page != nullptr && page->objectName() == name) {
            tabs_->setCurrentIndex(index);
            return;
        }
    }
}

void InspectorPanel::showPropertiesPage() {
    showNamedPage("erncInspectorProperties");
}

void InspectorPanel::showDiagnosticsPage() {
    showNamedPage("erncInspectorDiagnostics");
}

} // namespace ernc::presentation
