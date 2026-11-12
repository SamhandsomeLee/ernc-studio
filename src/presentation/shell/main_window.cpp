#include "shell/layout_settings.hpp"
#include "shell/main_window.hpp"

#include "inspector/inspector_panel.hpp"
#include "navigator/navigator_model.hpp"
#include "operation_center/operation_center_panel.hpp"
#include "task_cards/task_card_panel.hpp"
#include "theme/theme_tokens.hpp"
#include "workspace/editor_tabs.hpp"

#include "ernc/domain/region_catalog.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTreeView>
#include <QVBoxLayout>

#include <filesystem>
#include <string>
#include <string_view>

namespace ernc::presentation {
namespace {

/// 设计 v1.2 第 17.3 节的起步宽度，单位是逻辑像素。不是 Siemens 官方尺寸。
constexpr int kProjectTreeWidth = 260;
constexpr int kTaskCardsWidth = 300;

/// 检查器的起步高度，单位是逻辑像素。落在设计给出的 220–300 之内。
constexpr int kInspectorHeight = 240;

/// @brief 这次启动是不是只显示后就退出。
/// @return 命令行带 `--exit-after-show` 时为 true。这种启动不读写布局设置。
bool shellSettingsPaused() {
    return QApplication::arguments().contains(QStringLiteral("--exit-after-show"));
}

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

    projectTree_ = addPlaceholderDock(
        QStringLiteral("erncDockProjectTree"), QStringLiteral("项目树"), Qt::LeftDockWidgetArea);
    inspectorDock_ = addPlaceholderDock(
        QStringLiteral("erncDockInspector"), QStringLiteral("检查器"), Qt::BottomDockWidgetArea);
    taskCards_ = addPlaceholderDock(
        QStringLiteral("erncDockTaskCards"), QStringLiteral("任务卡"), Qt::RightDockWidgetArea);
    operationCenter_ = addPlaceholderDock(
        QStringLiteral("erncDockOperationCenter"), QStringLiteral("操作中心"), Qt::RightDockWidgetArea);
    resizeDocks({projectTree_, taskCards_}, {kProjectTreeWidth, kTaskCardsWidth}, Qt::Horizontal);
    resizeDocks({inspectorDock_}, {kInspectorHeight}, Qt::Vertical);
    tabifyDockWidget(taskCards_, operationCenter_);
    taskCards_->raise();

    const ernc::domain::ParameterCatalog* loadedCatalog = nullptr;
    QTreeView* navigatorTree = nullptr;
    if (!catalog.ok()) {
        showNavigatorError(projectTree_, QStringLiteral("参数目录无法加载：") + catalogPath);
    } else {
        auto* model = new NavigatorModel(std::move(catalog));
        if (!model->ok()) {
            const QString error = model->error();
            delete model;
            showNavigatorError(projectTree_, error);
        } else {
            loadedCatalog = model->catalog();
            auto* tree = new QTreeView(projectTree_);
            tree->setObjectName(QStringLiteral("erncProjectNavigator"));
            tree->setHeaderHidden(true);
            tree->setSelectionMode(QAbstractItemView::SingleSelection);
            model->setParent(tree);
            tree->setModel(model);
            QWidget* previous = projectTree_->widget();
            projectTree_->setWidget(tree);
            delete previous;
            navigatorTree = tree;
        }
    }

    auto* workArea = new QWidget(this);
    workArea->setObjectName(QStringLiteral("erncWorkArea"));
    auto* workLayout = new QVBoxLayout(workArea);
    workLayout->setContentsMargins(0, 0, 0, 0);
#ifdef ERNC_LAYOUT_MANIFEST_PATH
    const ernc::domain::LayoutLoad layoutLoad = ernc::domain::FirmwareLayoutLoader::loadFile(ERNC_LAYOUT_MANIFEST_PATH);
    if (layoutLoad.ok()) {
        firmwareLayout_ = layoutLoad.layout();
        const ernc::domain::Result<ernc::domain::RegionCatalog> regionLoad =
            ernc::domain::RegionCatalog::load(*firmwareLayout_);
        if (regionLoad.ok() && regionLoad.tryValue() != nullptr) {
            regionCatalog_ = regionLoad.value();
        }
    }
#endif
    const ernc::domain::FirmwareLayout* layout = firmwareLayout_ ? &*firmwareLayout_ : nullptr;
    const ernc::domain::RegionCatalog* regionCatalog = regionCatalog_ ? &*regionCatalog_ : nullptr;
    auto* editorTabs = new EditorTabs(loadedCatalog, &parameterStore_, layout, regionCatalog, workArea);
#ifdef ERNC_CAN_NAMES_PATH
    editorTabs->setCanNamesPath(QString::fromUtf8(ERNC_CAN_NAMES_PATH));
#endif
    editorTabs_ = editorTabs;
    workLayout->addWidget(editorTabs);
    setCentralWidget(workArea);
    if (navigatorTree != nullptr) {
        QObject::connect(navigatorTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [editorTabs](const QModelIndex& current, const QModelIndex&) {
                applyTreeSelection(editorTabs, current);
            });
    }

    inspectorPanel_ = new InspectorPanel(loadedCatalog, inspectorDock_);
    QWidget* previousInspector = inspectorDock_->widget();
    inspectorDock_->setWidget(inspectorPanel_);
    delete previousInspector;
    editorTabs_->setParameterObserver([panel = inspectorPanel_](std::string_view parameterId) {
        panel->showParameter(parameterId);
    });
    inspectorPanel_->setOperationCenterOpener([dock = operationCenter_] {
        dock->show();
        dock->raise();
    });

    auto* taskCardPanel = new TaskCardPanel(taskCards_);
    QWidget* previousTasks = taskCards_->widget();
    taskCards_->setWidget(taskCardPanel);
    delete previousTasks;

    auto* operationPanel = new OperationCenterPanel(operationCenter_);
    QWidget* previousOperation = operationCenter_->widget();
    operationCenter_->setWidget(operationPanel);
    delete previousOperation;

    QStatusBar* bar = statusBar();
    bar->setObjectName(QStringLiteral("erncStatusBar"));
    bar->showMessage(QStringLiteral("未连接"));

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    viewMenu->addAction(projectTree_->toggleViewAction());
    viewMenu->addAction(inspectorDock_->toggleViewAction());
    viewMenu->addAction(taskCards_->toggleViewAction());
    viewMenu->addAction(operationCenter_->toggleViewAction());
    viewMenu->addSeparator();
    QAction* parameterDebug = viewMenu->addAction(QStringLiteral("参数调试"));
    QAction* curveAnalysis = viewMenu->addAction(QStringLiteral("曲线分析"));
    QAction* faultDiagnosis = viewMenu->addAction(QStringLiteral("故障诊断"));
    QAction* restoreDefault = viewMenu->addAction(QStringLiteral("恢复默认布局"));
    connect(parameterDebug, &QAction::triggered, this, [this] {
        applyWorkspace(presetLayout(WorkspacePreset::ParameterDebug));
    });
    connect(curveAnalysis, &QAction::triggered, this, [this] {
        applyWorkspace(presetLayout(WorkspacePreset::CurveAnalysis));
    });
    connect(faultDiagnosis, &QAction::triggered, this, [this] {
        applyWorkspace(presetLayout(WorkspacePreset::FaultDiagnosis));
    });
    connect(restoreDefault, &QAction::triggered, this, [this] { restoreDefaultShellLayout(); });

    bindShellKeymap();
    if (!restoreSavedShellLayout()) {
        applyDefaultShellLayout();
    }
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

void MainWindow::applyWorkspace(const WorkspaceLayout& layout) {
    projectTree_->setVisible(layout.docks.projectTree);
    taskCards_->setVisible(layout.docks.taskCards);
    operationCenter_->setVisible(layout.docks.operationCenter);
    inspectorDock_->setVisible(layout.docks.inspector);
    if (layout.docks.operationCenter && layout.raiseOperationCenter) {
        operationCenter_->raise();
    } else if (layout.docks.taskCards) {
        taskCards_->raise();
    }
    if (layout.editorPage == ShellEditorPage::FunctionView) {
        editorTabs_->showFunctionView();
    } else if (layout.editorPage == ShellEditorPage::CurveAnalysis) {
        editorTabs_->showCurveAnalysis();
    } else if (layout.editorPage == ShellEditorPage::ParameterView) {
        editorTabs_->showParameterView();
    }
    if (layout.inspectorPage == ShellInspectorPage::Properties) {
        inspectorPanel_->showPropertiesPage();
    } else if (layout.inspectorPage == ShellInspectorPage::Diagnostics) {
        inspectorPanel_->showDiagnosticsPage();
    }
}

void MainWindow::applyDefaultShellLayout() {
    QScreen* screen = QGuiApplication::primaryScreen();
    const QRect available = screen == nullptr ? QRect() : screen->availableGeometry();
    const WindowSize size = defaultWindowSize(available.width(), available.height());
    resize(size.width, size.height);
    applyWorkspace(layoutForWidth(size.width));
}

bool MainWindow::restoreSavedShellLayout() {
    if (shellSettingsPaused()) {
        return false;
    }
    QSettings settings;
    const QByteArray geometry = settings.value(QStringLiteral("shell/geometry")).toByteArray();
    const QByteArray state = settings.value(QStringLiteral("shell/state")).toByteArray();
    if (geometry.isEmpty() && state.isEmpty()) {
        return false;
    }
    if (!geometry.isEmpty()) {
        restoreGeometry(geometry);
    } else {
        applyDefaultShellLayout();
    }
    if (!state.isEmpty() && restoreState(state, kShellLayoutVersion)) {
        return true;
    }
    if (geometry.isEmpty()) {
        return true;
    }
    applyWorkspace(layoutForWidth(width()));
    return true;
}

void MainWindow::restoreDefaultShellLayout() {
    if (!shellSettingsPaused()) {
        QSettings settings;
        settings.remove(QStringLiteral("shell/geometry"));
        settings.remove(QStringLiteral("shell/state"));
    }
    applyWorkspace(layoutForWidth(width()));
}

void MainWindow::bindShellKeymap() {
    const ShellKeymap map = loadShellKeymapFile(std::filesystem::path{ERNC_SHELL_KEYMAP_PATH});
    if (!map.error.empty()) {
        return;
    }
    for (const KeyBinding& binding : map.bindings) {
        if (binding.reserved) {
            continue;
        }
        const QKeySequence sequence(QString::fromStdString(binding.key));
        if (sequence.isEmpty()) {
            continue;
        }
        QAction* toggle = nullptr;
        if (binding.action == "toggle-project-tree") {
            toggle = projectTree_->toggleViewAction();
        } else if (binding.action == "toggle-task-cards") {
            toggle = taskCards_->toggleViewAction();
        } else if (binding.action == "toggle-inspector") {
            toggle = inspectorDock_->toggleViewAction();
        }
        if (toggle != nullptr) {
            toggle->setShortcut(sequence);
            continue;
        }
        auto* shortcut = new QShortcut(sequence, this);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        const std::string action = binding.action;
        connect(shortcut, &QShortcut::activated, this, [this, action] { runBoundAction(action); });
    }
}

void MainWindow::runBoundAction(std::string_view action) {
    if (action == "show-diagnostics") {
        inspectorDock_->show();
        inspectorDock_->raise();
        inspectorPanel_->showDiagnosticsPage();
        return;
    }
    if (action == "focus-parameter-table") {
        editorTabs_->focusParameterTable();
        return;
    }
    if (action == "refresh-view") {
        editorTabs_->focusCurrentPage();
        return;
    }
    if (action == "close-overlay") {
        return;
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!shellSettingsPaused()) {
        QSettings settings;
        settings.setValue(QStringLiteral("shell/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("shell/state"), saveState(kShellLayoutVersion));
    }
    QMainWindow::closeEvent(event);
}

} // namespace ernc::presentation
