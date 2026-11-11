#include "workspace/editor_tabs.hpp"

#include "parameters/fake_parameter_rows.hpp"
#include "parameters/parameter_table_model.hpp"
#include "theme/theme_tokens.hpp"
#include "workspace/editor_selection.hpp"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
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

/// @brief 做一个显示固定假数据表的 Parameter View。
/// @param parent 页签控件。它通过 Qt 父子关系拥有返回的页面。
/// @return 带说明和表格的页面。表格不连接设备，选中行由调用方接到共用选择。
QWidget* makeParameterView(QWidget* parent) {
    auto* view = new QWidget(parent);
    view->setObjectName(QStringLiteral("erncParameterView"));
    auto* layout = new QVBoxLayout(view);
    layout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    layout->setSpacing(kSpace8);

    auto* caption = new QLabel(QStringLiteral("以下是固定假数据，不是设备当前值。"), view);
    caption->setWordWrap(true);
    layout->addWidget(caption);

    auto* table = new QTableView(view);
    table->setObjectName(QStringLiteral("erncParameterTable"));
    auto* model = new ParameterTableModel(table);
    table->setModel(model);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    layout->addWidget(table, 1);
    return view;
}

} // namespace

EditorTabs::EditorTabs(QWidget* parent)
    : QTabWidget(parent) {
    setObjectName(QStringLiteral("erncEditorTabs"));
    setTabsClosable(false);
    setMovable(false);

    addTab(makeFunctionView(this, functionLabel_), QStringLiteral("Function View"));
    auto* parameterView = makeParameterView(this);
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
}

} // namespace ernc::presentation
