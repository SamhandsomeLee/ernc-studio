#include "workspace/editor_tabs.hpp"

#include "parameters/fake_parameter_rows.hpp"
#include "parameters/parameter_filter_proxy.hpp"
#include "parameters/parameter_table_model.hpp"
#include "theme/theme_tokens.hpp"
#include "workspace/editor_selection.hpp"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QTableView>
#include <QVBoxLayout>

namespace ernc::presentation {
namespace {

/// @brief 做一个显示当前选择的 Function View。
/// @param parent 页签控件。它通过 Qt 父子关系拥有返回的页面。
/// @param label 成功时写入页面里的文字控件。调用方不另接所有权。
/// @return 已经设置边距的页面。
QWidget* makeFunctionView(QWidget* parent, QLabel*& label) {
    auto* view = new QWidget(parent);
    view->setObjectName(QStringLiteral("erncFunctionView"));
    auto* layout = new QVBoxLayout(view);
    layout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    label = new QLabel(view);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setWordWrap(true);
    layout->addWidget(label, 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addStretch(1);
    return view;
}

/// @brief 做一个显示目录和运行状态的 Parameter View。
/// @param parent 页签控件。它通过 Qt 父子关系拥有返回的页面。
/// @param catalog 参数目录，只借用。空指针时表格没有行。
/// @param store 运行状态，只借用。空指针时不显示数值。
/// @param search 成功时写入搜索框。调用方不另接所有权。
/// @param proxy 成功时写入筛选代理。调用方不另接所有权。
/// @return 带说明、搜索和表格的页面。表格不连接设备，选中行由调用方接到共用选择。
QWidget* makeParameterView(QWidget* parent, const ernc::domain::ParameterCatalog* catalog,
                           ernc::domain::ParameterRuntimeStore* store, QLineEdit*& search, ParameterFilterProxy*& proxy) {
    auto* view = new QWidget(parent);
    view->setObjectName(QStringLiteral("erncParameterView"));
    auto* layout = new QVBoxLayout(view);
    layout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    layout->setSpacing(kSpace8);

    auto* caption = new QLabel(QStringLiteral("参数表显示目录和运行状态。没有读数时 Read 为 —。"), view);
    caption->setWordWrap(true);
    layout->addWidget(caption);

    auto* filters = new QHBoxLayout();
    search = new QLineEdit(view);
    search->setObjectName(QStringLiteral("erncParameterSearch"));
    search->setPlaceholderText(QStringLiteral("搜索参数、路径或说明"));
    search->setClearButtonEnabled(true);
    auto* dirtyOnly = new QCheckBox(QStringLiteral("只看待写入"), view);
    dirtyOnly->setObjectName(QStringLiteral("erncParameterDirtyOnly"));
    filters->addWidget(search, 1);
    filters->addWidget(dirtyOnly);
    layout->addLayout(filters);

    auto* table = new QTableView(view);
    table->setObjectName(QStringLiteral("erncParameterTable"));
    auto* model = new ParameterTableModel(catalog, store, table);
    proxy = new ParameterFilterProxy(catalog, store, table);
    proxy->setSourceModel(model);
    table->setModel(proxy);
    table->setSortingEnabled(true);
    proxy->sort(-1);
    table->horizontalHeader()->setSortIndicatorShown(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    layout->addWidget(table, 1);
    QObject::connect(search, &QLineEdit::textChanged, proxy, &ParameterFilterProxy::setQuery);
    QObject::connect(dirtyOnly, &QCheckBox::toggled, proxy, &ParameterFilterProxy::setDirtyOnly);
    return view;
}

} // namespace

EditorTabs::EditorTabs(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                         QWidget* parent)
    : QTabWidget(parent) {
    setObjectName(QStringLiteral("erncEditorTabs"));
    setTabsClosable(false);
    setMovable(false);

    addTab(makeFunctionView(this, functionLabel_), QStringLiteral("Function View"));
    auto* parameterView = makeParameterView(this, catalog, store, parameterSearch_, parameterProxy_);
    addTab(parameterView, QStringLiteral("Parameter View"));
    auto* table = parameterView->findChild<QTableView*>(QStringLiteral("erncParameterTable"));
    connect(table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
        &EditorTabs::onParameterRowChanged);
    setCurrentIndex(0);
    refresh();
}

void EditorTabs::onParameterRowChanged(const QModelIndex& current, const QModelIndex& previous) {
    (void)previous;
    if (!current.isValid()) {
        return;
    }
    const QModelIndex idIndex = current.sibling(
        current.row(), static_cast<int>(FakeParameterColumn::Parameter));
    selectParameter(idIndex.data(Qt::DisplayRole).toString());
}

void EditorTabs::setParameterObserver(std::function<void(std::string_view)> observer) {
    parameterObserver_ = std::move(observer);
}

void EditorTabs::selectParameter(const QString& parameterId) {
    const std::string incoming = parameterId.toStdString();
    if (incoming.empty()) {
        return;
    }
    selection_.selectParameter(incoming);
    refresh();
    if (parameterObserver_) {
        parameterObserver_(selection_.parameterId());
    }
}

void EditorTabs::selectPage(const QString& pageId) {
    selection_.selectPage(pageId.toStdString());
    if (parameterProxy_ != nullptr) {
        parameterProxy_->setPage(pageId);
    }
    refresh();
}

void EditorTabs::showNamedPage(const QString& objectName) {
    for (int index = 0; index < count(); ++index) {
        QWidget* page = widget(index);
        if (page != nullptr && page->objectName() == objectName) {
            setCurrentIndex(index);
            return;
        }
    }
}

void EditorTabs::showFunctionView() {
    showNamedPage(QStringLiteral("erncFunctionView"));
}

void EditorTabs::showParameterView() {
    showNamedPage(QStringLiteral("erncParameterView"));
}

void EditorTabs::focusParameterTable() {
    showParameterView();
    if (parameterSearch_ != nullptr) {
        parameterSearch_->setFocus();
        parameterSearch_->selectAll();
    }
}

void EditorTabs::focusCurrentPage() {
    if (QWidget* page = currentWidget()) {
        page->setFocus();
    }
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
}

} // namespace ernc::presentation
