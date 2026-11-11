#include "shell/main_window.hpp"

#include "navigator/navigator_model.hpp"
#include "theme/theme_tokens.hpp"

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

/// @brief 把选中节点写成工作区里的一行文字。
/// @param label 工作区文字，调用方拥有。空指针时不改。
/// @param current 当前选中索引。无效时恢复成“工作区”。
void showSelection(QLabel* label, const QModelIndex& current) {
    if (label == nullptr) {
        return;
    }
    if (!current.isValid()) {
        label->setText(QStringLiteral("工作区"));
        return;
    }
    const QString kind = current.data(NavigatorModel::KindRole).toString();
    const QString id = current.data(Qt::DisplayRole).toString();
    label->setText(kind + QStringLiteral(" ") + id);
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
    workLayout->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    auto* workLabel = new QLabel(QStringLiteral("工作区"), workArea);
    workLabel->setObjectName(QStringLiteral("erncWorkSelection"));
    workLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    workLayout->addWidget(workLabel, 0, Qt::AlignLeft | Qt::AlignTop);
    workLayout->addStretch(1);
    setCentralWidget(workArea);

    QDockWidget* projectTree = addPlaceholderDock(
        QStringLiteral("erncDockProjectTree"), QStringLiteral("项目树"), Qt::LeftDockWidgetArea);
    QDockWidget* inspector = addPlaceholderDock(
        QStringLiteral("erncDockInspector"), QStringLiteral("检查器"), Qt::BottomDockWidgetArea);
    QDockWidget* taskCards = addPlaceholderDock(
        QStringLiteral("erncDockTaskCards"), QStringLiteral("任务卡"), Qt::RightDockWidgetArea);
    resizeDocks({projectTree, taskCards}, {kProjectTreeWidth, kTaskCardsWidth}, Qt::Horizontal);
    resizeDocks({inspector}, {kInspectorHeight}, Qt::Vertical);

    if (!catalog.ok()) {
        showNavigatorError(projectTree, QStringLiteral("参数目录无法加载：") + catalogPath);
    } else {
        auto* model = new NavigatorModel(std::move(catalog));
        if (!model->ok()) {
            const QString error = model->error();
            delete model;
            showNavigatorError(projectTree, error);
        } else {
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
                [workLabel](const QModelIndex& current, const QModelIndex&) {
                    showSelection(workLabel, current);
                });
        }
    }

    QStatusBar* bar = statusBar();
    bar->setObjectName(QStringLiteral("erncStatusBar"));
    bar->showMessage(QStringLiteral("未连接"));

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    viewMenu->addAction(projectTree->toggleViewAction());
    viewMenu->addAction(inspector->toggleViewAction());
    viewMenu->addAction(taskCards->toggleViewAction());
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
