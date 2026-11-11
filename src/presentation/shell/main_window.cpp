#include "shell/main_window.hpp"

#include "inspector/inspector_panel.hpp"
#include "navigator/navigator_model.hpp"
#include "operation_center/operation_center_panel.hpp"
#include "task_cards/task_card_panel.hpp"
#include "theme/theme_tokens.hpp"
#include "workspace/editor_tabs.hpp"

#include <QAbstractItemView>
#include <QDockWidget>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenuBar>
#include <QStatusBar>
#include <QTreeView>
#include <QVBoxLayout>

namespace ernc::presentation {
namespace {

/// 设计 v1.2 第 17.3 节的起步宽度，单位是逻辑像素。不是 Siemens 官方尺寸。
constexpr int kProjectTreeWidth = 260;
constexpr int kTaskCardsWidth = 300;

/// 检查器的起步高度，单位是逻辑像素。落在设计给出的 220–300 之内。
constexpr int kInspectorHeight = 240;

/// 沿用现有入口的窗口大小。1920×1080 和窄屏收起规则留给后续布局任务。
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 800;

/// @brief 把项目树的当前节点送进两个工作页共用的选择。
/// @param tabs 工作页签，调用方拥有。空指针时不改。
/// @param current 当前树索引。无效索引不清除已有的页面或参数 id。
void applyTreeSelection(EditorTabs* tabs, const QModelIndex& current) {
    if (tabs == nullptr || !current.isValid()) {
        return;
    }
    const QString kind = current.data(NavigatorModel::KindRole).toString();
    const QString id = current.data(Qt::DisplayRole).toString();
    if (kind == QStringLiteral("参数")) {
        tabs->selectParameter(id);
        return;
    }
    if (kind == QStringLiteral("页面")) {
        tabs->selectPage(id);
    }
}

} // namespace

MainWindow::MainWindow(ernc::domain::CatalogLoad catalog, QString catalogPath, QWidget* parent)
    : QMainWindow(parent) {
    setObjectName(QStringLiteral("erncMainWindow"));
    setWindowTitle(QStringLiteral("ERNC Studio"));
    resize(kWindowWidth, kWindowHeight);

    auto* workArea = new QWidget(this);
    workArea->setObjectName(QStringLiteral("erncWorkArea"));
    auto* workLayout = new QVBoxLayout(workArea);
    workLayout->setContentsMargins(0, 0, 0, 0);
    auto* editorTabs = new EditorTabs(workArea);
    workLayout->addWidget(editorTabs);
    setCentralWidget(workArea);

    QDockWidget* projectTree = addPlaceholderDock(
        QStringLiteral("erncDockProjectTree"), QStringLiteral("项目树"), Qt::LeftDockWidgetArea);
    QDockWidget* inspector = addPlaceholderDock(
        QStringLiteral("erncDockInspector"), QStringLiteral("检查器"), Qt::BottomDockWidgetArea);
    QDockWidget* taskCards = addPlaceholderDock(
        QStringLiteral("erncDockTaskCards"), QStringLiteral("任务卡"), Qt::RightDockWidgetArea);
    QDockWidget* operationCenter = addPlaceholderDock(
        QStringLiteral("erncDockOperationCenter"), QStringLiteral("操作中心"), Qt::RightDockWidgetArea);
    resizeDocks({projectTree, taskCards}, {kProjectTreeWidth, kTaskCardsWidth}, Qt::Horizontal);
    resizeDocks({inspector}, {kInspectorHeight}, Qt::Vertical);
    tabifyDockWidget(taskCards, operationCenter);
    taskCards->raise();

    const ernc::domain::ParameterCatalog* loadedCatalog = nullptr;
    if (!catalog.ok()) {
        showNavigatorError(projectTree, QStringLiteral("参数目录无法加载：") + catalogPath);
    } else {
        auto* model = new NavigatorModel(std::move(catalog));
        if (!model->ok()) {
            const QString error = model->error();
            delete model;
            showNavigatorError(projectTree, error);
        } else {
            loadedCatalog = model->catalog();
            auto* tree = new QTreeView(projectTree);
            tree->setObjectName(QStringLiteral("erncProjectNavigator"));
            tree->setHeaderHidden(true);
            tree->setSelectionMode(QAbstractItemView::SingleSelection);
            model->setParent(tree);
            tree->setModel(model);
            QWidget* previous = projectTree->widget();
            projectTree->setWidget(tree);
            delete previous;
            QObject::connect(tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [editorTabs](const QModelIndex& current, const QModelIndex&) {
                    applyTreeSelection(editorTabs, current);
                });
        }
    }

    auto* inspectorPanel = new InspectorPanel(loadedCatalog, inspector);
    QWidget* previousInspector = inspector->widget();
    inspector->setWidget(inspectorPanel);
    delete previousInspector;
    editorTabs->setParameterObserver([inspectorPanel](std::string_view parameterId) {
        inspectorPanel->showParameter(parameterId);
    });
    inspectorPanel->setOperationCenterOpener([operationCenter] {
        operationCenter->show();
        operationCenter->raise();
    });

    auto* taskCardPanel = new TaskCardPanel(taskCards);
    QWidget* previousTasks = taskCards->widget();
    taskCards->setWidget(taskCardPanel);
    delete previousTasks;

    auto* operationPanel = new OperationCenterPanel(operationCenter);
    QWidget* previousOperation = operationCenter->widget();
    operationCenter->setWidget(operationPanel);
    delete previousOperation;

    QStatusBar* bar = statusBar();
    bar->setObjectName(QStringLiteral("erncStatusBar"));
    bar->showMessage(QStringLiteral("未连接"));

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    viewMenu->addAction(projectTree->toggleViewAction());
    viewMenu->addAction(inspector->toggleViewAction());
    viewMenu->addAction(taskCards->toggleViewAction());
    viewMenu->addAction(operationCenter->toggleViewAction());
}

QDockWidget* MainWindow::addPlaceholderDock(
    const QString& objectName,
    const QString& title,
    Qt::DockWidgetArea area) {
    auto* dock = new QDockWidget(title, this);
    dock->setObjectName(objectName);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::TopDockWidgetArea
        | Qt::BottomDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
        | QDockWidget::DockWidgetClosable);

    auto* label = new QLabel(title, dock);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    dock->setWidget(label);
    addDockWidget(area, dock);
    return dock;
}

void MainWindow::showNavigatorError(QDockWidget* dock, const QString& message) {
    auto* label = new QLabel(message, dock);
    label->setObjectName(QStringLiteral("erncProjectNavigatorError"));
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    QWidget* previous = dock->widget();
    dock->setWidget(label);
    delete previous;
}

} // namespace ernc::presentation
