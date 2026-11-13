#include "inspector/inspector_panel.hpp"

#include "ernc/domain/catalog.hpp"
#include "inspector/inspector_context.hpp"
#include "theme/theme_tokens.hpp"

#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
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
    layout->setContentsMargins(kSpace8, kSpace8, kSpace8, kSpace8);
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    label = new QLabel(scroll);
    label->setTextFormat(Qt::PlainText);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setMargin(kSpace4);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setWordWrap(true);
    scroll->setWidget(label);
    layout->addWidget(scroll, 1);
    return page;
}

} // namespace

InspectorPanel::InspectorPanel(const ernc::domain::ParameterCatalog* catalog,
                               const ernc::domain::ParameterRuntimeStore* store, QWidget* parent)
    : QWidget(parent)
    , catalog_(catalog)
    , store_(store) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("erncInspectorTabs"));
    auto* propertiesPage = makePage(QStringLiteral("erncInspectorProperties"), tabs_, propertiesLabel_);
    scalarReadButton_ = new QPushButton(QStringLiteral("从设备读取"), propertiesPage);
    scalarReadButton_->setObjectName(QStringLiteral("erncInspectorReadScalar"));
    if (auto* pageLayout = qobject_cast<QVBoxLayout*>(propertiesPage->layout())) {
        pageLayout->insertWidget(0, scalarReadButton_, 0, Qt::AlignLeft);
    }
    connect(scalarReadButton_, &QPushButton::clicked, this, [this] {
        if (scalarReadHandler_ && !parameterId_.empty()) {
            scalarReadHandler_(parameterId_);
        }
    });
    tabs_->addTab(propertiesPage, QStringLiteral("属性"));
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
    const auto* state = store_ != nullptr && definition != nullptr ? store_->find(definition->id) : nullptr;
    const InspectorText text = describeInspector(parameterId_, definition, state);
    propertiesLabel_->setText(QString::fromStdString(text.properties));
    infoLabel_->setText(QString::fromStdString(text.info));
    diagnosticsLabel_->setText(QString::fromStdString(text.diagnostics));
}

void InspectorPanel::setOperationCenterOpener(std::function<void()> opener) {
    operationCenterOpener_ = std::move(opener);
}

void InspectorPanel::setScalarReadHandler(std::function<void(std::string_view)> handler) {
    scalarReadHandler_ = std::move(handler);
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
