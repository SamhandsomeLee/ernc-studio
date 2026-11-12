#include "workspace/editor_tabs.hpp"

#include "arrays/array_detail_panel.hpp"
#include "arrays/array_summary.hpp"

#include "analysis/analysis_panel.hpp"
#include "device/device_controller.hpp"
#include "parameters/write_plan_dialog.hpp"

#include "ernc/application/batch_write.hpp"
#include "scheduler.hpp"

#include "controls/parameter_delegates.hpp"
#include "pages/can_names.hpp"
#include "pages/can_info_panel.hpp"
#include "pages/check.hpp"
#include "pages/switch_state.hpp"
#include "pages/other_page.hpp"
#include "pages/threshold.hpp"
#include "pages/enc_param.hpp"
#include "pages/rnc_param.hpp"
#include "pages/ernc_flag.hpp"
#include "pages/ernc_in_out.hpp"
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
#include <QMenu>
#include <QPushButton>
#include <QTableView>
#include <QToolButton>
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
    layout->setContentsMargins(kSpace8, kSpace8, kSpace8, kSpace8);
    layout->setSpacing(kSpace8);
    auto* toolbar = new QHBoxLayout();
    label = new QLabel(view);
    label->setObjectName(QStringLiteral("erncFunctionSummary"));
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setWordWrap(true);
    toolbar->addWidget(label);
    toolbar->addStretch(1);
    auto* read = new QPushButton(QStringLiteral("读取本页"), view);
    read->setObjectName(QStringLiteral("erncFunctionRead"));
    auto* plan = new QPushButton(QStringLiteral("查看写入计划"), view);
    plan->setObjectName(QStringLiteral("erncFunctionWritePlan"));
    toolbar->addWidget(read);
    toolbar->addWidget(plan);
    layout->addLayout(toolbar);
    layout->addStretch(1);
    return view;
}

/// @brief 做一个显示目录和运行状态的 Parameter View。
/// @param parent 页签控件。它通过 Qt 父子关系拥有返回的页面。
/// @param catalog 参数目录，只借用。空指针时表格没有行。
/// @param store 运行状态，只借用。空指针时不显示数值。
/// @param layout 固件布局，只借用。空指针时 Set 列确认不会暂存。
/// @param search 成功时写入搜索框。调用方不另接所有权。
/// @param proxy 成功时写入筛选代理。调用方不另接所有权。
/// @param modelOut 成功时写入参数表模型。调用方不另接所有权。
/// @return 带说明、搜索和表格的页面。表格不连接设备，选中行由调用方接到共用选择。
QWidget* makeParameterView(QWidget* parent, const ernc::domain::ParameterCatalog* catalog,
                           ernc::domain::ParameterRuntimeStore* store, const ernc::domain::FirmwareLayout* layout,
                           QLineEdit*& search, ParameterFilterProxy*& proxy, ParameterTableModel*& modelOut) {
    auto* view = new QWidget(parent);
    view->setObjectName(QStringLiteral("erncParameterView"));
    auto* viewLayout = new QVBoxLayout(view);
    viewLayout->setContentsMargins(kSpace8, kSpace8, kSpace8, kSpace8);
    viewLayout->setSpacing(kSpace8);

    auto* filters = new QHBoxLayout();
    search = new QLineEdit(view);
    search->setObjectName(QStringLiteral("erncParameterSearch"));
    search->setPlaceholderText(QStringLiteral("搜索参数、路径或说明"));
    search->setClearButtonEnabled(true);
    auto* dirtyOnly = new QCheckBox(QStringLiteral("只看待写入"), view);
    dirtyOnly->setObjectName(QStringLiteral("erncParameterDirtyOnly"));
    filters->addWidget(search, 1);
    filters->addWidget(dirtyOnly);
    auto* columns = new QToolButton(view);
    columns->setText(QStringLiteral("显示"));
    columns->setObjectName(QStringLiteral("erncParameterDisplayOptions"));
    columns->setPopupMode(QToolButton::InstantPopup);
    auto* displayMenu = new QMenu(columns);
    columns->setMenu(displayMenu);
    filters->addWidget(columns);
    auto* readPage = new QPushButton(QStringLiteral("读取本页"), view);
    readPage->setObjectName(QStringLiteral("erncReadCurrentPage"));
    filters->addWidget(readPage);
    auto* writePlan = new QPushButton(QStringLiteral("查看写入计划"), view);
    writePlan->setObjectName(QStringLiteral("erncOpenWritePlan"));
    filters->addWidget(writePlan);
    viewLayout->addLayout(filters);

    auto* table = new QTableView(view);
    table->setObjectName(QStringLiteral("erncParameterTable"));
    auto* model = new ParameterTableModel(catalog, store, table);
    modelOut = model;
    proxy = new ParameterFilterProxy(catalog, store, table);
    proxy->setSourceModel(model);
    table->setModel(proxy);
    table->setSortingEnabled(true);
    proxy->sort(-1);
    table->horizontalHeader()->setSortIndicatorShown(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setItemDelegate(new ParameterDelegate(catalog, layout, store, table));
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    auto* header = table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setSectionResizeMode(static_cast<int>(FakeParameterColumn::Parameter), QHeaderView::Stretch);
    table->setColumnWidth(static_cast<int>(FakeParameterColumn::Status), 122);
    table->setColumnWidth(static_cast<int>(FakeParameterColumn::Read), 118);
    table->setColumnWidth(static_cast<int>(FakeParameterColumn::Set), 118);
    table->setColumnWidth(static_cast<int>(FakeParameterColumn::Path), 210);
    table->setColumnWidth(static_cast<int>(FakeParameterColumn::UpdatedAt), 174);
    auto* rows = table->verticalHeader();
    rows->setVisible(false);
    rows->setSectionResizeMode(QHeaderView::Fixed);
    rows->setDefaultSectionSize(30);
    const auto addColumnToggle = [displayMenu, table](const QString& label, FakeParameterColumn column) {
        const int index = static_cast<int>(column);
        table->setColumnHidden(index, true);
        QAction* action = displayMenu->addAction(label);
        action->setCheckable(true);
        QObject::connect(action, &QAction::toggled, table, [table, index](bool visible) {
            table->setColumnHidden(index, !visible);
        });
    };
    addColumnToggle(QStringLiteral("固件路径"), FakeParameterColumn::Path);
    addColumnToggle(QStringLiteral("更新时间"), FakeParameterColumn::UpdatedAt);
    displayMenu->addSeparator();
    QAction* standardDensity = displayMenu->addAction(QStringLiteral("标准行距"));
    standardDensity->setCheckable(true);
    QObject::connect(standardDensity, &QAction::toggled, rows, [rows](bool standard) {
        rows->setDefaultSectionSize(standard ? 34 : 30);
    });
    viewLayout->addWidget(table, 1);
    QObject::connect(search, &QLineEdit::textChanged, proxy, &ParameterFilterProxy::setQuery);
    QObject::connect(dirtyOnly, &QCheckBox::toggled, proxy, &ParameterFilterProxy::setDirtyOnly);
    return view;
}

} // namespace

EditorTabs::EditorTabs(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                       const ernc::domain::FirmwareLayout* layout, const ernc::domain::RegionCatalog* regionCatalog,
                       QWidget* parent)
    : QTabWidget(parent), catalog_{catalog}, store_{store}, layout_{layout} {
    setObjectName(QStringLiteral("erncEditorTabs"));
    setTabsClosable(false);
    setMovable(false);

    auto* functionView = makeFunctionView(this, functionLabel_);
    flagPage_ = new ErncFlagPage(catalog, store, layout, functionView);
    if (auto* functionLayout = qobject_cast<QVBoxLayout*>(functionView->layout())) {
        functionLayout->insertWidget(1, flagPage_, 1);
    }
    flagPage_->setEditObserver([this](const ernc::domain::ParameterId& id) {
        if (parameterModel_ != nullptr) {
            parameterModel_->noteLocalEdit(id);
        }
    });
    flagPage_->setSelectionObserver([this](const ernc::domain::ParameterId& id) {
        selectParameter(QString::fromStdString(id.text()));
    });
    flagPage_->hide();
    inOutPage_ = new ErncInOutPage(catalog, store, layout, functionView);
    if (auto* functionLayout = qobject_cast<QVBoxLayout*>(functionView->layout())) {
        functionLayout->insertWidget(2, inOutPage_, 1);
    }
    inOutPage_->setEditObserver([this](const ernc::domain::ParameterId& id) {
        if (parameterModel_ != nullptr) {
            parameterModel_->noteLocalEdit(id);
        }
    });
    inOutPage_->setSelectionObserver([this](const ernc::domain::ParameterId& id) {
        selectParameter(QString::fromStdString(id.text()));
    });
    inOutPage_->hide();
    encPage_ = new EncParamPage(catalog, store, layout, functionView);
    if (auto* functionLayout = qobject_cast<QVBoxLayout*>(functionView->layout())) {
        functionLayout->insertWidget(3, encPage_, 1);
    }
    encPage_->setEditObserver([this](const ernc::domain::ParameterId& id) {
        if (parameterModel_ != nullptr) {
            parameterModel_->noteLocalEdit(id);
        }
    });
    encPage_->setSelectionObserver([this](const ernc::domain::ParameterId& id) {
        selectParameter(QString::fromStdString(id.text()));
    });
    encPage_->hide();
    rncPage_ = new RncParamPage(catalog, store, layout, functionView);
    if (auto* functionLayout = qobject_cast<QVBoxLayout*>(functionView->layout())) {
        functionLayout->insertWidget(4, rncPage_, 1);
    }
    rncPage_->setEditObserver([this](const ernc::domain::ParameterId& id) {
        if (parameterModel_ != nullptr) {
            parameterModel_->noteLocalEdit(id);
        }
    });
    rncPage_->setSelectionObserver([this](const ernc::domain::ParameterId& id) {
        selectParameter(QString::fromStdString(id.text()));
    });
    rncPage_->hide();
    checkPage_ = new CheckPage(catalog, store, layout, functionView);
    if (auto* functionLayout = qobject_cast<QVBoxLayout*>(functionView->layout())) {
        functionLayout->insertWidget(5, checkPage_, 1);
    }
    checkPage_->setEditObserver([this](const ernc::domain::ParameterId& id) {
        if (parameterModel_ != nullptr) {
            parameterModel_->noteLocalEdit(id);
        }
    });
    checkPage_->setSelectionObserver([this](const ernc::domain::ParameterId& id) {
        selectParameter(QString::fromStdString(id.text()));
    });
    checkPage_->setCanOpener([this] { showCanInfoTab(); });
    checkPage_->hide();
    switchPage_ = new SwitchStatePage(functionView);
    if (auto* functionLayout = qobject_cast<QVBoxLayout*>(functionView->layout())) {
        functionLayout->insertWidget(6, switchPage_, 1);
    }
    switchPage_->hide();
    canPage_ = new CanNamePage(functionView);
    if (auto* functionLayout = qobject_cast<QVBoxLayout*>(functionView->layout())) {
        functionLayout->insertWidget(7, canPage_, 1);
    }
    canPage_->hide();
    thresholdPage_ = new ThresholdPage(catalog, store, layout, functionView);
    if (auto* functionLayout = qobject_cast<QVBoxLayout*>(functionView->layout())) {
        functionLayout->insertWidget(8, thresholdPage_, 1);
    }
    thresholdPage_->setEditObserver([this](const ernc::domain::ParameterId& id) {
        if (parameterModel_ != nullptr) {
            parameterModel_->noteLocalEdit(id);
        }
    });
    thresholdPage_->setSelectionObserver([this](const ernc::domain::ParameterId& id) {
        selectParameter(QString::fromStdString(id.text()));
    });
    thresholdPage_->hide();
    otherPage_ = new OtherPage(catalog, store, layout, functionView);
    if (auto* functionLayout = qobject_cast<QVBoxLayout*>(functionView->layout())) {
        functionLayout->insertWidget(9, otherPage_, 1);
    }
    otherPage_->setEditObserver([this](const ernc::domain::ParameterId& id) {
        if (parameterModel_ != nullptr) {
            parameterModel_->noteLocalEdit(id);
        }
    });
    otherPage_->setSelectionObserver([this](const ernc::domain::ParameterId& id) {
        selectParameter(QString::fromStdString(id.text()));
    });
    otherPage_->hide();
    addTab(functionView, QStringLiteral("函数视图"));
    auto* parameterView = makeParameterView(this, catalog, store, layout, parameterSearch_, parameterProxy_, parameterModel_);
    addTab(parameterView, QStringLiteral("参数视图"));
    if (regionCatalog != nullptr) {
        // 本次只开放离线文件导入与画图。Region 设备读写端口保持为空，不新增真实设备操作路径。
        curveAnalysis_ = new AnalysisPanel(*regionCatalog, nullptr, nullptr, this);
        addTab(curveAnalysis_, QStringLiteral("曲线分析"));
    }
    canInfoPanel_ = new CanInfoPanel(layout, this);
    addTab(canInfoPanel_, QStringLiteral("CAN信息"));
    parameterTable_ = parameterView->findChild<QTableView*>(QStringLiteral("erncParameterTable"));
    connect(functionView->findChild<QPushButton*>(QStringLiteral("erncFunctionRead")), &QPushButton::clicked,
        this, &EditorTabs::readCurrentPage);
    connect(functionView->findChild<QPushButton*>(QStringLiteral("erncFunctionWritePlan")), &QPushButton::clicked,
        this, &EditorTabs::openWritePlan);
    if (auto* readCurrent = parameterView->findChild<QPushButton*>(QStringLiteral("erncReadCurrentPage"))) {
        connect(readCurrent, &QPushButton::clicked, this, &EditorTabs::readCurrentPage);
    }
    if (auto* openWritePlan = parameterView->findChild<QPushButton*>(QStringLiteral("erncOpenWritePlan"))) {
        connect(openWritePlan, &QPushButton::clicked, this, &EditorTabs::openWritePlan);
    }
    connect(parameterTable_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
        &EditorTabs::onParameterRowChanged);
    connect(this, &QTabWidget::currentChanged, this, [this] {
        if (showingFunctionView()) {
            emphasizeFunctionViewParameter(QString::fromStdString(selection_.parameterId()));
        } else {
            syncParameterSelection();
        }
    });
    connect(parameterModel_, &QAbstractItemModel::dataChanged, this, [this](const QModelIndex& first, const QModelIndex& last) {
        if (!parameterObserver_ || selection_.parameterId().empty()) {
            return;
        }
        const QString selected = QString::fromStdString(selection_.parameterId());
        for (int row = first.row(); row <= last.row(); ++row) {
            if (parameterModel_->index(row, static_cast<int>(FakeParameterColumn::Parameter)).data().toString() == selected) {
                parameterObserver_(selection_.parameterId());
                return;
            }
        }
    });
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
    const QString parameterId = idIndex.data(Qt::DisplayRole).toString();
    if (parameterId.toStdString() != selection_.parameterId()) {
        selectParameter(parameterId);
    }
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
    if (showingFunctionView()) {
        emphasizeFunctionViewParameter(parameterId);
    } else {
        syncParameterSelection();
    }
    if (arrayDetail_ != nullptr && catalog_ != nullptr && layout_ != nullptr) {
        const ernc::domain::ParameterDefinition* definition = catalog_->find(incoming);
        if (usesCompactArrayUi(definition, layout_)) {
            arrayDetail_->bindParameter(parameterId);
        }
    }
    if (parameterObserver_) {
        parameterObserver_(selection_.parameterId());
    }
}

void EditorTabs::syncParameterSelection() {
    if (parameterTable_ == nullptr || parameterProxy_ == nullptr || currentWidget() != parameterTable_->parentWidget()) {
        return;
    }
    const QString selected = QString::fromStdString(selection_.parameterId());
    for (int row = 0; row < parameterProxy_->rowCount(); ++row) {
        const auto index = parameterProxy_->index(row, static_cast<int>(FakeParameterColumn::Parameter));
        if (index.data().toString() == selected) {
            if (parameterTable_->currentIndex().row() != row) {
                // 保留表格自身的选择通知和重绘；当前行回调见到相同 ID 时直接返回。
                parameterTable_->setCurrentIndex(index);
                parameterTable_->scrollTo(index);
            }
            return;
        }
    }
}

void EditorTabs::setArrayRuntimeStore(ernc::domain::ArrayRuntimeStore* store) noexcept {
    arrayStore_ = store;
    if (thresholdPage_ != nullptr) {
        thresholdPage_->setArrayRuntimeStore(store);
    }
    if (otherPage_ != nullptr) {
        otherPage_->setArrayRuntimeStore(store);
    }
}

void EditorTabs::setArrayDetailPanel(ArrayDetailPanel* panel) noexcept {
    arrayDetail_ = panel;
}

bool EditorTabs::showingFunctionView() const {
    const QWidget* page = currentWidget();
    return page != nullptr && page->objectName() == QStringLiteral("erncFunctionView");
}

void EditorTabs::emphasizeFunctionViewParameter(const QString& parameterId) {
    const auto tryEmphasize = [&](auto* page) -> bool {
        if (page == nullptr || !page->isVisible()) {
            return false;
        }
        if (!page->containsParameter(parameterId)) {
            return false;
        }
        page->emphasizeParameter(parameterId);
        return true;
    };
    if (tryEmphasize(flagPage_) || tryEmphasize(inOutPage_) || tryEmphasize(encPage_) || tryEmphasize(rncPage_)
        || tryEmphasize(checkPage_) || tryEmphasize(thresholdPage_) || tryEmphasize(otherPage_)) {
        return;
    }
}

void EditorTabs::showCanCatalog() {
    canShown_ = true;
    refresh();
}

void EditorTabs::hideCanCatalog() {
    canShown_ = false;
    refresh();
}

void EditorTabs::setCanNamesPath(const QString& path) {
    if (canPage_ != nullptr) {
        canPage_->loadFile(path);
    }
    if (canInfoPanel_ != nullptr) {
        canInfoPanel_->setNamesPath(path);
    }
}

void EditorTabs::setCanSlotMapPath(const QString& path) {
    if (canInfoPanel_ != nullptr) {
        canInfoPanel_->setSlotMapPath(path);
    }
}

void EditorTabs::showCanInfoTab() {
    if (canInfoPanel_ == nullptr) {
        return;
    }
    for (int index = 0; index < count(); ++index) {
        if (widget(index) == canInfoPanel_) {
            setCurrentIndex(index);
            return;
        }
    }
}

void EditorTabs::selectPage(const QString& pageId) {
    canShown_ = false;
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

void EditorTabs::setDeviceController(DeviceController* controller) {
    deviceController_ = controller;
    if (canInfoPanel_ != nullptr) {
        canInfoPanel_->setDeviceController(controller);
    }
}

void EditorTabs::readCurrentPage() {
    if (deviceController_ == nullptr) {
        return;
    }
    deviceController_->readCategory(selection_.pageId());
}

void EditorTabs::bindParameterSession(const ernc::domain::SessionId session) {
    if (parameterModel_ != nullptr) {
        parameterModel_->bindSession(session);
    }
}

void EditorTabs::notifyParametersUpdated(const ernc::domain::SessionId session, const QStringList& parameterIds) {
    if (parameterModel_ == nullptr) {
        return;
    }
    for (const QString& text : parameterIds) {
        const ernc::domain::Result<ernc::domain::ParameterId> parsed = ernc::domain::ParameterId::parse(text.toStdString());
        if (parsed.ok() && parsed.tryValue() != nullptr) {
            parameterModel_->noteUpdated(session, *parsed.tryValue());
        }
    }
    refresh();
    if (arrayDetail_ != nullptr) {
        arrayDetail_->onArrayStoreChanged();
    }
}

void EditorTabs::noteDeviceDisconnected() {
    if (parameterModel_ != nullptr) {
        parameterModel_->noteDisconnected();
    }
    refresh();
}

void EditorTabs::refreshLocalParameters() {
    if (parameterModel_ != nullptr && catalog_ != nullptr) {
        for (const auto& definition : catalog_->entries()) {
            parameterModel_->noteLocalEdit(definition.id);
        }
    }
    refresh();
}

void EditorTabs::openWritePlan() {
    showWritePlan(QString::fromStdString(selection_.pageId()));
}

void EditorTabs::openAllWritePlan() {
    showWritePlan(QStringLiteral("ALL"));
}

void EditorTabs::showWritePlan(const QString& pageId) {
    const auto dialogs = findChildren<QDialog*>(QStringLiteral("erncWritePlanDialog"), Qt::FindDirectChildrenOnly);
    if (!dialogs.isEmpty()) {
        dialogs.constFirst()->raise();
        dialogs.constFirst()->activateWindow();
        return;
    }
    WritePlanDialog::Sources sources;
    sources.catalog = catalog_;
    sources.store = store_;
    sources.layout = layout_;
    sources.pageId = pageId;
    auto* dialog = new WritePlanDialog(sources, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if (deviceController_ != nullptr) {
        dialog->setDeviceController(deviceController_);
        ernc::application::CommandScheduler* scheduler = deviceController_->scheduler();
        ernc::application::ParameterBatchWriter* writer = deviceController_->batchWriter();
        if (scheduler != nullptr && writer != nullptr) {
            dialog->setWriter(writer, scheduler);
        }
        deviceController_->setWritePlanDialog(dialog);
        QObject::connect(dialog, &QObject::destroyed, deviceController_, [controller = deviceController_](QObject* dead) {
            (void)dead;
            controller->setWritePlanDialog(nullptr);
        });
    }
    dialog->show();
}

void EditorTabs::showFunctionView() {
    showNamedPage(QStringLiteral("erncFunctionView"));
}

void EditorTabs::showParameterView() {
    showNamedPage(QStringLiteral("erncParameterView"));
}

void EditorTabs::showCurveAnalysis() {
    if (curveAnalysis_ != nullptr) {
        showNamedPage(QStringLiteral("erncCurveAnalysis"));
    }
}

void EditorTabs::setUiActivityRecorder(
    std::function<void(std::string_view, std::string_view, bool, ernc::domain::ErrorCode)> recorder) {
    if (curveAnalysis_ != nullptr) {
        curveAnalysis_->setUiActivityRecorder(std::move(recorder));
    }
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
    const QString readLabel = selection_.pageId() == "ALL" ? QStringLiteral("读取全部标量") : QStringLiteral("读取本页");
    findChild<QPushButton*>(QStringLiteral("erncFunctionRead"))->setText(readLabel);
    findChild<QPushButton*>(QStringLiteral("erncReadCurrentPage"))->setText(readLabel);
    const bool showFlag = selection_.pageId() == "ERNC_Flag";
    const bool showInOut = selection_.pageId() == "ERNC_IN_OUT";
    const bool showEnc = selection_.pageId() == "ENC_Param";
    const bool showRnc = selection_.pageId() == "RNC_Param";
    const bool showCheck = selection_.pageId() == "Check" && !canShown_;
    const bool showSwitch = selection_.pageId() == "ERNC_Switch_State";
    const bool showCan = selection_.pageId() == "Check" && canShown_;
    const bool showThreshold = selection_.pageId() == "Threshold";
    const bool showOther = selection_.pageId() == ernc::domain::kOtherPageCategory;
    functionLabel_->setVisible(!showFlag && !showInOut && !showEnc && !showRnc && !showCheck && !showSwitch && !showCan
                                && !showThreshold && !showOther);
    if (flagPage_ != nullptr) {
        flagPage_->setVisible(showFlag);
        if (showFlag) {
            flagPage_->reload();
        }
    }
    if (inOutPage_ != nullptr) {
        inOutPage_->setVisible(showInOut);
        if (showInOut) {
            inOutPage_->reload();
        }
    }
    if (encPage_ != nullptr) {
        encPage_->setVisible(showEnc);
        if (showEnc) {
            encPage_->reload();
        }
    }
    if (rncPage_ != nullptr) {
        rncPage_->setVisible(showRnc);
        if (showRnc) {
            rncPage_->reload();
        }
    }
    if (checkPage_ != nullptr) {
        checkPage_->setVisible(showCheck);
        if (showCheck) {
            checkPage_->reload();
        }
    }
    if (switchPage_ != nullptr) {
        switchPage_->setVisible(showSwitch);
    }
    if (canPage_ != nullptr) {
        canPage_->setVisible(showCan);
    }
    if (thresholdPage_ != nullptr) {
        thresholdPage_->setVisible(showThreshold);
        if (showThreshold) {
            thresholdPage_->reload();
        }
    }
    if (otherPage_ != nullptr) {
        otherPage_->setVisible(showOther);
        if (showOther) {
            otherPage_->reload();
        }
    }
    if (QWidget* functionView = functionLabel_->parentWidget()) {
        if (auto* functionLayout = qobject_cast<QVBoxLayout*>(functionView->layout())) {
            const bool showPage = showFlag || showInOut || showEnc || showRnc || showCheck || showSwitch || showCan
                || showThreshold || showOther;
            if (functionLayout->count() > 10) {
                functionLayout->setStretch(1, showFlag ? 1 : 0);
                functionLayout->setStretch(2, showInOut ? 1 : 0);
                functionLayout->setStretch(3, showEnc ? 1 : 0);
                functionLayout->setStretch(4, showRnc ? 1 : 0);
                functionLayout->setStretch(5, showCheck ? 1 : 0);
                functionLayout->setStretch(6, showSwitch ? 1 : 0);
                functionLayout->setStretch(7, showCan ? 1 : 0);
                functionLayout->setStretch(8, showThreshold ? 1 : 0);
                functionLayout->setStretch(9, showOther ? 1 : 0);
                functionLayout->setStretch(10, showPage ? 0 : 1);
            }
        }
    }
}

} // namespace ernc::presentation
