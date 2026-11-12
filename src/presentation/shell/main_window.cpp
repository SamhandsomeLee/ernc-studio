#include "shell/layout_settings.hpp"
#include "shell/main_window.hpp"

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
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QTimer>
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
            catalog_ = loadedCatalog;
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

    operationPanel_ = new OperationCenterPanel(operationCenter_);
    QWidget* previousOperation = operationCenter_->widget();
    operationCenter_->setWidget(operationPanel_);
    delete previousOperation;

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
    devicePumpTimer_ = new QTimer(this);
    devicePumpTimer_->setInterval(10);
    QObject::connect(devicePumpTimer_, &QTimer::timeout, this, [this] {
        if (deviceController_ != nullptr) {
            deviceController_->pumpTick();
        }
    });

    if (loadedCatalog != nullptr && firmwareLayout_.has_value()) {
        deviceController_ = new DeviceController(*loadedCatalog, *firmwareLayout_, parameterStore_, this);
        DeviceController::UiRecorder recorder;
        recorder.record = [this](std::string_view action, std::string_view detail, bool success, ernc::domain::ErrorCode error) {
            recordUiAction(action, detail, success, error);
        };
        deviceController_->setUiRecorder(std::move(recorder));
        if (sessionLog_ != nullptr) {
            deviceController_->attachSessionDiagnosticLog(&sessionLog_->diagnosticLog());
        }
        editorTabs_->setDeviceController(deviceController_);
        QObject::connect(deviceController_, &DeviceController::connectionChanged, this, &MainWindow::onDeviceConnectionChanged);
        QObject::connect(deviceController_, &DeviceController::parametersUpdated, this, &MainWindow::onParametersUpdated);
        QObject::connect(deviceController_, &DeviceController::operationLogRefreshRequested, this, [this] {
            if (operationPanel_ != nullptr) {
                operationPanel_->refreshFromSession();
            }
        });
        QObject::connect(deviceController_, &DeviceController::readPageFinished, this, [this](bool success, QString detail) {
            if (success) {
                statusBar()->showMessage(QStringLiteral("读取本页完成：%1").arg(detail), 5000);
            } else {
                statusBar()->showMessage(QStringLiteral("读取本页失败：%1").arg(detail), 8000);
            }
        });
    }

    if (sessionLog_ != nullptr) {
        updateDeviceStatusBar(false, ernc::domain::SessionId::none());
    } else {
        bar->showMessage(QStringLiteral("未连接设备（会话日志目录创建失败）"));
    }

    QMenu* deviceMenu = menuBar()->addMenu(QStringLiteral("设备"));
    QAction* connectSimulator = deviceMenu->addAction(QStringLiteral("连接虚拟从站"));
    QAction* connectSerial = deviceMenu->addAction(QStringLiteral("连接串口设备…"));
    QAction* disconnectDevice = deviceMenu->addAction(QStringLiteral("断开设备"));
    QAction* readCurrentPage = deviceMenu->addAction(QStringLiteral("读取本页参数"));
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
        deviceText = QStringLiteral("设备：串口 #%1").arg(sessionId.raw());
    } else if (connected) {
        deviceText = QStringLiteral("设备：虚拟从站 #%1").arg(sessionId.raw());
    } else {
        deviceText = QStringLiteral("设备：未连接");
    }
    QString message = deviceText;
    if (sessionLog_ != nullptr) {
        message += QStringLiteral("  |  日志：") + QString::fromStdWString(sessionLog_->sessionDirectory().wstring());
    }
    statusBar()->showMessage(message);
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

void MainWindow::onParametersUpdated(const ernc::domain::SessionId sessionId, const QStringList& parameterIds) {
    if (editorTabs_ != nullptr) {
        editorTabs_->notifyParametersUpdated(sessionId, parameterIds);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
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
