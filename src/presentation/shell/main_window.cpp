#include "shell/layout_settings.hpp"
#include "shell/window_title.hpp"
#include "shell/main_window.hpp"
#include "shell/product_ui.hpp"

#include "arrays/array_detail_panel.hpp"

#include <QTabWidget>

#include "inspector/inspector_panel.hpp"
#include "navigator/navigator_model.hpp"
#include "operation_center/operation_center_panel.hpp"
#include "task_cards/task_card_panel.hpp"
#include "theme/theme_tokens.hpp"
#include "workspace/editor_tabs.hpp"
#include "device/device_controller.hpp"
#include "device/serial_connect_dialog.hpp"

#include "diagnostics/session_log_paths.hpp"
#include "ernc/domain/region_catalog.hpp"
#include "ernc/infrastructure/logging/app_session_log.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QGuiApplication>
#include <QItemSelectionModel>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

namespace ernc::presentation {
namespace {

/// 设计 v1.2 第 17.3 节的起步宽度，单位是逻辑像素。不是 Siemens 官方尺寸。
constexpr int kProjectTreeWidth = 260;
constexpr int kTaskCardsWidth = 300;

/// 检查器的起步高度，单位是逻辑像素。落在设计给出的 220–300 之内。
constexpr int kInspectorHeight = 240;

/// @brief 从设备报告提取便于扫描的读取结果。原报告留在操作日志。
/// @param success 本次请求是否完成且没有失败项。
/// @param detail 本次调用借用的结构化报告或简短错误文字。
QString readSummary(bool success, const QString& detail) {
    const auto object = QJsonDocument::fromJson(detail.toUtf8()).object();
    if (!object.contains(QStringLiteral("completed"))) {
        return detail.isEmpty() ? QStringLiteral("读取失败") : detail;
    }
    const QString scope = object.value(QStringLiteral("scope")).toString() == QStringLiteral("all_scalars")
        ? QStringLiteral("全部可读参数") : QStringLiteral("本页");
    return QStringLiteral("%1%2 · %3 项完成，%4 项失败，%5 项跳过")
        .arg(scope, success ? QStringLiteral("读取完成") : QStringLiteral("读取有失败"))
        .arg(object.value(QStringLiteral("completed")).toInt())
        .arg(object.value(QStringLiteral("failed")).toInt())
        .arg(object.value(QStringLiteral("skipped")).toInt());
}

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
        const QModelIndex parent = current.parent();
        if (parent.isValid()) {
            tabs->selectPage(parent.data(Qt::DisplayRole).toString());
        }
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
    setWindowTitle(applicationBaseTitle());

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
            catalog_ = loadedCatalog;
            auto* tree = new QTreeView(projectTree_);
            tree->setObjectName(QStringLiteral("erncProjectNavigator"));
            tree->setHeaderHidden(true);
            tree->setSelectionMode(QAbstractItemView::SingleSelection);
            tree->setUniformRowHeights(true);
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
    workLayout->setSpacing(0);
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
#ifdef ERNC_CAN_SLOT_MAP_PATH
    editorTabs->setCanSlotMapPath(QString::fromUtf8(ERNC_CAN_SLOT_MAP_PATH));
#endif
    editorTabs_ = editorTabs;
    workspaceContext_ = new QLabel(workArea);
    workspaceContext_->setObjectName(QStringLiteral("erncEditorContext"));
    workspaceContext_->setTextFormat(Qt::PlainText);
    workspaceContext_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    workLayout->addWidget(workspaceContext_);
    workLayout->addWidget(editorTabs);
    setCentralWidget(workArea);
    if (navigatorTree != nullptr) {
        QObject::connect(navigatorTree->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this, editorTabs](const QModelIndex& current, const QModelIndex&) {
                applyTreeSelection(editorTabs, current);
                updateWorkspaceContext();
            });
    }

    inspectorPanel_ = new InspectorPanel(loadedCatalog, &parameterStore_, inspectorDock_);
    arrayDetailPanel_ = new ArrayDetailPanel(loadedCatalog, firmwareLayout_ ? &*firmwareLayout_ : nullptr,
                                             &arrayParameterStore_, inspectorDock_);
    bottomInspectorTabs_ = new QTabWidget(inspectorDock_);
    bottomInspectorTabs_->setObjectName(QStringLiteral("erncBottomInspectorTabs"));
    bottomInspectorTabs_->addTab(inspectorPanel_, QStringLiteral("检查器"));
    bottomInspectorTabs_->addTab(arrayDetailPanel_, QStringLiteral("数组详情"));
    QWidget* previousInspector = inspectorDock_->widget();
    inspectorDock_->setWidget(bottomInspectorTabs_);
    delete previousInspector;
    editorTabs_->setArrayRuntimeStore(&arrayParameterStore_);
    editorTabs_->setArrayDetailPanel(arrayDetailPanel_);
    editorTabs_->setArrayDetailPresenter([this] { presentArrayDetailPanel(); });
    editorTabs_->setParameterObserver([panel = inspectorPanel_](std::string_view parameterId) {
        panel->showParameter(parameterId);
    });
    inspectorPanel_->setOperationCenterOpener([dock = operationCenter_] {
        dock->show();
        dock->raise();
    });

    TaskCardPanel* taskCardPanel = new TaskCardPanel(taskCards_);
    QWidget* previousTasks = taskCards_->widget();
    taskCards_->setWidget(taskCardPanel);
    delete previousTasks;

    operationPanel_ = new OperationCenterPanel(operationCenter_);
    QWidget* previousOperation = operationCenter_->widget();
    operationCenter_->setWidget(operationPanel_);
    delete previousOperation;
    operationPanel_->setExportLogsHandler([this] { exportSessionLogs(); });

    const std::filesystem::path sessionDirectory = createSessionLogDirectory();
    if (!sessionDirectory.empty()) {
        sessionLog_ = std::make_unique<ernc::infrastructure::AppSessionLog>(sessionDirectory, 1U);
        std::ostringstream meta;
        meta << "{\"phase\":\"desktop\"}";
#ifdef ERNC_PARAMETER_CATALOG_PATH
        meta << ",\"catalog\":\"" << ERNC_PARAMETER_CATALOG_PATH << '"';
#endif
#ifdef ERNC_LAYOUT_MANIFEST_PATH
        meta << ",\"layout_manifest\":\"" << ERNC_LAYOUT_MANIFEST_PATH << '"';
#endif
        meta << '}';
        recordUiAction("app.session.start", meta.str(), true);
        operationPanel_->bindSessionLog(sessionLog_.get());
        editorTabs_->setUiActivityRecorder([this](std::string_view action, std::string_view detail, bool success,
                                                  ernc::domain::ErrorCode error) {
            recordUiAction(action, detail, success, error);
        });
    }

    QStatusBar* bar = statusBar();
    bar->setObjectName(QStringLiteral("erncStatusBar"));
    deviceStatus_ = new QLabel(bar);
    deviceStatus_->setObjectName(QStringLiteral("erncDeviceStatus"));
    deviceStatus_->setTextFormat(Qt::PlainText);
    bar->addPermanentWidget(deviceStatus_);
    taskStatus_ = new QLabel(QStringLiteral("就绪"), bar);
    taskStatus_->setObjectName(QStringLiteral("erncTaskStatus"));
    taskStatus_->setTextFormat(Qt::PlainText);
    taskStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    bar->addWidget(taskStatus_, 1);
    if (editorTabs_ != nullptr) {
        editorTabs_->setRegionStatusSink([this](const QString& text) {
            if (taskStatus_ != nullptr) {
                taskStatus_->setText(text);
            }
        });
    }
    devicePumpTimer_ = new QTimer(this);
    devicePumpTimer_->setInterval(10);
    QObject::connect(devicePumpTimer_, &QTimer::timeout, this, [this] {
        if (deviceController_ != nullptr) {
            deviceController_->pumpTick();
        }
    });

    if (loadedCatalog != nullptr && firmwareLayout_.has_value()) {
        deviceController_ = new DeviceController(*loadedCatalog, *firmwareLayout_, parameterStore_, arrayParameterStore_, this);
        arrayDetailPanel_->setIoHandlers(
            [controller = deviceController_](const QString& id) { controller->readArrayParameter(id); },
            [controller = deviceController_](const QString& id) { controller->writeArrayParameter(id); });
        DeviceController::UiRecorder recorder;
        recorder.record = [this](std::string_view action, std::string_view detail, bool success, ernc::domain::ErrorCode error) {
            recordUiAction(action, detail, success, error);
        };
        deviceController_->setUiRecorder(std::move(recorder));
        if (sessionLog_ != nullptr) {
            deviceController_->attachSessionDiagnosticLog(&sessionLog_->diagnosticLog());
        }
        editorTabs_->setDeviceController(deviceController_);
        inspectorPanel_->setScalarReadHandler(
            [controller = deviceController_](const std::string_view parameterId) {
                if (controller != nullptr && !parameterId.empty()) {
                    controller->readScalarParameter(QString::fromStdString(std::string(parameterId)));
                }
            });
        QObject::connect(deviceController_, &DeviceController::connectionChanged, this, &MainWindow::onDeviceConnectionChanged);
        QObject::connect(deviceController_, &DeviceController::parametersUpdated, this, &MainWindow::onParametersUpdated);
        QObject::connect(deviceController_, &DeviceController::operationLogRefreshRequested, this, [this] {
            if (operationPanel_ != nullptr) {
                operationPanel_->refreshFromSession();
            }
        });
        QObject::connect(deviceController_, &DeviceController::readPageFinished, this, [this](bool success, QString detail) {
            taskStatus_->setText(readSummary(success, detail));
            if (success && editorTabs_ != nullptr) {
                QTimer::singleShot(0, editorTabs_, [tabs = editorTabs_]() {
                    tabs->refreshParameterTableAfterBatchRead();
                });
            }
        });
        QObject::connect(deviceController_, &DeviceController::scalarReadFinished, this,
                         [this](bool success, const QString& parameterId, const QString& detail) {
                             const QString prefix = success ? QStringLiteral("读取完成") : QStringLiteral("读取失败");
                             taskStatus_->setText(QStringLiteral("%1 · %2").arg(prefix, parameterId));
                             if (!success && !detail.isEmpty()) {
                                 taskStatus_->setToolTip(detail);
                             } else {
                                 taskStatus_->setToolTip(QString());
                             }
                         });
        QObject::connect(deviceController_, &DeviceController::writeBatchFinished, this, [this](int acked, int failed) {
            taskStatus_->setText(QStringLiteral("写入结束 · %1 项设备已确认，%2 项失败").arg(acked).arg(failed));
        });
        QObject::connect(taskCardPanel, &TaskCardPanel::readAllScalarsRequested, deviceController_, &DeviceController::readAllScalars);
    }
    QObject::connect(taskCardPanel, &TaskCardPanel::readCurrentPageRequested, editorTabs, &EditorTabs::readCurrentPage);
    QObject::connect(taskCardPanel, &TaskCardPanel::writeCurrentPageRequested, editorTabs, &EditorTabs::openWritePlan);
    QObject::connect(taskCardPanel, &TaskCardPanel::writeAllDirtyRequested, editorTabs, &EditorTabs::openAllWritePlan);

    updateDeviceStatusBar(false, ernc::domain::SessionId::none());

    addFileMenu();
    QMenu* deviceMenu = menuBar()->addMenu(QStringLiteral("设备"));
    QAction* connectSimulator = deviceMenu->addAction(QStringLiteral("连接虚拟从站"));
    QAction* connectSerial = deviceMenu->addAction(QStringLiteral("连接串口设备…"));
    QAction* disconnectDevice = deviceMenu->addAction(QStringLiteral("断开设备"));
    QAction* readCurrentPage = deviceMenu->addAction(QStringLiteral("读取本页参数"));
    connectSimulator->setEnabled(deviceController_ != nullptr);
    connectSerial->setEnabled(deviceController_ != nullptr);
    disconnectDevice->setEnabled(false);
    readCurrentPage->setEnabled(false);
    if (deviceController_ != nullptr) {
        connect(deviceController_, &DeviceController::connectionChanged, this,
            [disconnectDevice, readCurrentPage](bool connected, ernc::domain::SessionId) {
                disconnectDevice->setEnabled(connected);
                readCurrentPage->setEnabled(connected);
            });
    }
    QObject::connect(connectSimulator, &QAction::triggered, this, [this] {
        if (deviceController_ != nullptr) {
            deviceController_->connectSimulator();
        }
    });
    QObject::connect(connectSerial, &QAction::triggered, this, [this] {
        if (deviceController_ == nullptr) {
            return;
        }
        SerialConnectDialog dialog(this);
        if (dialog.exec() != QDialog::Accepted) {
            return;
        }
        deviceController_->connectSerial(dialog.portName(), dialog.baudRate());
    });
    QObject::connect(disconnectDevice, &QAction::triggered, this, [this] {
        if (deviceController_ != nullptr) {
            deviceController_->disconnectDevice();
        }
    });
    QObject::connect(readCurrentPage, &QAction::triggered, this, [this] {
        if (editorTabs_ != nullptr) {
            editorTabs_->readCurrentPage();
        }
    });

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    viewMenu->addAction(projectTree_->toggleViewAction());
    viewMenu->addAction(inspectorDock_->toggleViewAction());
    viewMenu->addAction(taskCards_->toggleViewAction());
    viewMenu->addAction(operationCenter_->toggleViewAction());
    viewMenu->addSeparator();
    QAction* parameterDebug = viewMenu->addAction(QStringLiteral("参数调试"));
    QAction* curveAnalysis = viewMenu->addAction(QStringLiteral("曲线分析"));
    curveAnalysis->setEnabled(regionCatalog != nullptr);
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

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
    QAction* exportLogs = helpMenu->addAction(QStringLiteral("导出日志压缩包…"), this, &MainWindow::exportSessionLogs);
    exportLogs->setEnabled(sessionLog_ != nullptr);

    auto* tools = addToolBar(kProjectBundleUiEnabled ? QStringLiteral("工程工具栏") : QStringLiteral("工具栏"));
    tools->setObjectName(QStringLiteral("erncWorkbenchTools"));
    tools->setMovable(false);
    tools->setFloatable(false);
    tools->setToolButtonStyle(Qt::ToolButtonTextOnly);
    if (kProjectBundleUiEnabled) {
        openProjectAction_->setIconText(QStringLiteral("打开工程"));
        tools->addAction(openProjectAction_);
        tools->addAction(saveProjectAction_);
        tools->addSeparator();
    }
    auto* connection = new QToolButton(tools);
    connection->setObjectName(QStringLiteral("erncDeviceMenuButton"));
    connection->setText(QStringLiteral("连接设备"));
    connection->setPopupMode(QToolButton::InstantPopup);
    auto* connectionMenu = new QMenu(connection);
    connectionMenu->addActions({connectSimulator, connectSerial, disconnectDevice});
    connection->setMenu(connectionMenu);
    connection->setEnabled(deviceController_ != nullptr);
    tools->addWidget(connection);
    tools->addSeparator();
    tools->addAction(projectTree_->toggleViewAction());
    tools->addAction(inspectorDock_->toggleViewAction());
    tools->addAction(taskCards_->toggleViewAction());

    if (navigatorTree != nullptr) {
        // 目录第一项为 ALL。启动即展示可搜索的参数表，保留函数视图作为相邻入口。
        navigatorTree->setCurrentIndex(navigatorTree->model()->index(0, 0));
        editorTabs_->showParameterView();
    }

    bindShellKeymap();
    if (!restoreSavedShellLayout()) {
        applyDefaultShellLayout();
    }
}

void MainWindow::updateWorkspaceContext() {
    const auto* tree = qobject_cast<QTreeView*>(projectTree_->widget());
    QModelIndex page = tree == nullptr ? QModelIndex() : tree->currentIndex();
    if (page.parent().isValid()) {
        page = page.parent();
    }
    QString scope = page.isValid() ? page.data().toString() : QStringLiteral("参数工作区");
    if (scope == QStringLiteral("ALL")) {
        scope = QStringLiteral("全部参数");
    } else if (scope == QStringLiteral("RNC_Param")) {
        scope = QStringLiteral("RNC 参数");
    }
    if (kProjectBundleUiEnabled) {
        const QString name = project_ ? QString::fromStdString(project_->name) : QStringLiteral("未打开工程");
        workspaceContext_->setText(QStringLiteral("%1  /  %2").arg(name, scope));
    } else {
        workspaceContext_->setText(scope);
    }
    workspaceContext_->setToolTip(workspaceContext_->text());
}

MainWindow::~MainWindow() = default;

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

void MainWindow::recordUiAction(std::string_view action, std::string_view detail, bool success,
                                ernc::domain::ErrorCode error) {
    if (sessionLog_ == nullptr) {
        return;
    }
    sessionLog_->recordUiAction(action, detail, success, error);
    sessionLog_->appendReadableLine(std::string(action) + (success ? " ok " : " fail ") + std::string(detail));
    if (operationPanel_ != nullptr) {
        operationPanel_->refreshFromSession();
    }
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
    if (sessionLog_ != nullptr) {
        std::ostringstream detail;
        detail << "{\"editor_page\":" << static_cast<int>(layout.editorPage)
               << ",\"project_tree\":" << (layout.docks.projectTree ? "true" : "false") << '}';
        recordUiAction("workspace.apply", detail.str(), true);
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
        QAction* actionWithShortcut = nullptr;
        if (binding.action == "toggle-project-tree") {
            actionWithShortcut = projectTree_->toggleViewAction();
        } else if (binding.action == "toggle-task-cards") {
            actionWithShortcut = taskCards_->toggleViewAction();
        } else if (binding.action == "toggle-inspector") {
            actionWithShortcut = inspectorDock_->toggleViewAction();
        } else if (kProjectBundleUiEnabled && binding.action == "open-project") {
            actionWithShortcut = openProjectAction_;
        } else if (kProjectBundleUiEnabled && binding.action == "save-project") {
            actionWithShortcut = saveProjectAction_;
        }
        if (actionWithShortcut != nullptr) {
            actionWithShortcut->setShortcut(sequence);
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
        if (deviceController_ != nullptr && deviceController_->connected()) {
            editorTabs_->readCurrentPage();
        } else {
            editorTabs_->focusCurrentPage();
        }
        return;
    }
    if (action == "close-overlay") {
        return;
    }
}

void MainWindow::updateDeviceStatusBar(const bool connected, const ernc::domain::SessionId sessionId) {
    QString deviceText;
    if (connected && deviceController_ != nullptr && deviceController_->linkKind() == DeviceController::LinkKind::Serial) {
        deviceText = QStringLiteral("串口 #%1 · 已连接").arg(sessionId.raw());
    } else if (connected) {
        deviceText = QStringLiteral("虚拟从站 #%1 · 已连接").arg(sessionId.raw());
    } else {
        deviceText = QStringLiteral("设备：未连接");
    }
    deviceStatus_->setText(deviceText);
    deviceStatus_->setProperty("connected", connected);
    deviceStatus_->style()->unpolish(deviceStatus_);
    deviceStatus_->style()->polish(deviceStatus_);
    deviceStatus_->setToolTip(sessionLog_ != nullptr
        ? QStringLiteral("会话日志：%1").arg(QString::fromStdWString(sessionLog_->sessionDirectory().wstring()))
        : QStringLiteral("会话日志目录不可用"));
}

void MainWindow::onDeviceConnectionChanged(const bool connected, const ernc::domain::SessionId sessionId) {
    if (connected) {
        if (devicePumpTimer_ != nullptr) {
            devicePumpTimer_->start();
        }
        if (editorTabs_ != nullptr) {
            editorTabs_->bindParameterSession(sessionId);
        }
    } else {
        if (devicePumpTimer_ != nullptr) {
            devicePumpTimer_->stop();
        }
        parameterStore_.markOffline();
        if (editorTabs_ != nullptr) {
            editorTabs_->noteDeviceDisconnected();
            editorTabs_->bindParameterSession(ernc::domain::SessionId::none());
        }
    }
    updateDeviceStatusBar(connected, sessionId);
}

void MainWindow::presentArrayDetailPanel() {
    if (inspectorDock_ == nullptr || arrayDetailPanel_ == nullptr || bottomInspectorTabs_ == nullptr) {
        return;
    }
    inspectorDock_->show();
    inspectorDock_->raise();
    bottomInspectorTabs_->setCurrentWidget(arrayDetailPanel_);
}

void MainWindow::onParametersUpdated(const ernc::domain::SessionId sessionId, const QStringList& parameterIds) {
    if (arrayDetailPanel_ != nullptr) {
        arrayDetailPanel_->onArrayStoreChanged();
    }
    if (editorTabs_ == nullptr) {
        return;
    }
    EditorTabs* tabs = editorTabs_;
    QTimer::singleShot(0, tabs, [tabs, sessionId, parameterIds]() {
        tabs->notifyParametersUpdated(sessionId, parameterIds);
    });
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (kProjectBundleUiEnabled && !confirmLeavingProject()) {
        event->ignore();
        return;
    }
    if (deviceController_ != nullptr && deviceController_->connected()) {
        deviceController_->disconnectDevice();
    }
    if (sessionLog_ != nullptr) {
        recordUiAction("app.session.end", "{}", true);
        (void)sessionLog_->exportSnapshot();
    }
    if (!shellSettingsPaused()) {
        QSettings settings;
        settings.setValue(QStringLiteral("shell/geometry"), saveGeometry());
        settings.setValue(QStringLiteral("shell/state"), saveState(kShellLayoutVersion));
    }
    QMainWindow::closeEvent(event);
}

} // namespace ernc::presentation
