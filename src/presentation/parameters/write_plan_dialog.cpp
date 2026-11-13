#include "parameters/write_plan_dialog.hpp"
#include "parameters/scalar_display.hpp"

#include "batch_operation_tags.hpp"
#include "device/device_controller.hpp"

#include "write_verification.hpp"

#include "ernc/application/batch_write.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/control_semantics.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/write_plan.hpp"

#include "scheduler.hpp"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <optional>
#include <utility>
#include <variant>

namespace ernc::presentation {
namespace {

/// 没有读数或没有目标时的占位。不是数值 0，也不是关。
constexpr QStringView kBlank = u"—";

/// @brief 按项目树页面 id 生成写入预览范围。`ALL` 表示目录内全部 dirty 标量。
/// @param page 本次调用借用的目录分类或全工程标记。
ernc::domain::WriteScope writeScopeForPage(const QString& page) {
    ernc::domain::WriteScope scope;
    if (isNavigatorAllPage(page)) {
        scope.kind = ernc::domain::WriteScopeKind::All;
    } else {
        scope.kind = ernc::domain::WriteScopeKind::Category;
        scope.category = page.toStdString();
    }
    return scope;
}

/// @brief 按目录控件显示一个标量。
/// @param definition 目录定义。空指针时只做数值格式。只在本次调用期间借用。
/// @param value 设备读数或用户目标，只借用。
/// @return 开关为开、关或 Unknown。其它类型为固件原值的十进制，不附加秒。
QString formatValue(const ernc::domain::ParameterDefinition* definition, const ernc::domain::ScalarValue& value) {
    if (definition != nullptr && definition->editor && *definition->editor == ernc::domain::EditorKind::Toggle) {
        switch (ernc::domain::decodeToggle(*definition, value)) {
        case ernc::domain::TogglePosition::On:
            return QStringLiteral("开");
        case ernc::domain::TogglePosition::Off:
            return QStringLiteral("关");
        case ernc::domain::TogglePosition::Unknown:
            return QStringLiteral("未知");
        }
    }
    return QString::fromStdString(displayScalar(&value));
}

/// @brief 取出 Read 和 Set 的显示文字。
/// @param definition 目录定义。空指针时开关仍按数值显示。只在本次调用期间借用。
/// @param state 运行状态。空指针时两列都是「—」。只在本次调用期间借用。
/// @param readText 成功时写入 Read 列。调用方拥有。
/// @param setText 成功时写入 Set 列。调用方拥有。
/// @note Read 末尾区分工程历史值与过期设备读数，两者均不代表当前设备值。
void formatReadSet(const ernc::domain::ParameterDefinition* definition, const ernc::domain::ParameterRuntimeState* state,
                   QString& readText, QString& setText) {
    readText = kBlank.toString();
    setText = kBlank.toString();
    if (state == nullptr) {
        return;
    }
    if (const auto* read = state->displayedRead()) {
        readText = formatValue(definition, *read);
        if (state->projectReadValue) {
            readText += QStringLiteral(" 历史读数");
        } else if (state->stale) {
            readText += QStringLiteral(" 数据已过期");
        }
    }
    if (state->stagedValue) {
        setText = formatValue(definition, *state->stagedValue);
    }
}

/// @brief 判断这条路径是不是可以逐个写入的 1 slot 标量。
/// @param layout 固件布局，只在本次调用期间借用。
/// @param firmwarePath 目录中的固件路径，只在本次调用期间借用。
/// @return 路径不是 Can_Msg，且 locate 得到 1 个标量 slot、4 字节时为 true。
/// @note 规则与 ParameterBatchWriter 跳过多 slot 的条件相同，避免预览把不会发送的项显示成待写入。
bool isOneSlot(const ernc::domain::FirmwareLayout& layout, std::string_view firmwarePath) {
    constexpr std::string_view prefix = "Can_Msg.";
    if (firmwarePath == "Can_Msg" || firmwarePath.starts_with(prefix)) {
        return false;
    }
    const ernc::domain::LayoutResolve located = layout.locate(firmwarePath);
    if (!located.ok()) {
        return false;
    }
    const ernc::domain::FieldLayout& field = located.field();
    return field.scalarType.has_value() && field.slotCount == 1U && field.size == 4U;
}

/// @brief 把排除原因写成预览状态。
/// @param reason WritePlan 给出的原因。
/// @return 界面状态。它不是 ErrorCode。
QString exclusionText(ernc::domain::WriteExclusion reason) {
    switch (reason) {
    case ernc::domain::WriteExclusion::ReadOnly:
        return QStringLiteral("只读");
    case ernc::domain::WriteExclusion::Command:
        return QStringLiteral("命令");
    case ernc::domain::WriteExclusion::Blob:
        return QStringLiteral("区域");
    case ernc::domain::WriteExclusion::Unsupported:
        return QStringLiteral("不支持");
    case ernc::domain::WriteExclusion::Unverified:
        return QStringLiteral("未核验");
    }
    return QStringLiteral("未核验");
}

/// @brief 把失败类别写成诊断标识。
/// @param code 领域错误码。名字就是诊断标识，没有另外的数字编号。
/// @return ErrorCode 枚举名。Unknown 也原样返回。
QString errorName(ernc::domain::ErrorCode code) {
    switch (code) {
    case ernc::domain::ErrorCode::DeviceDisconnected:
        return QStringLiteral("设备未连接");
    case ernc::domain::ErrorCode::SerialOpenFailed:
        return QStringLiteral("串口打开失败");
    case ernc::domain::ErrorCode::SerialIoFailed:
        return QStringLiteral("串口读写失败");
    case ernc::domain::ErrorCode::FrameLengthInvalid:
        return QStringLiteral("帧长度无效");
    case ernc::domain::ErrorCode::CrcMismatch:
        return QStringLiteral("CRC 校验失败");
    case ernc::domain::ErrorCode::ModbusException:
        return QStringLiteral("Modbus 异常");
    case ernc::domain::ErrorCode::ResponseTimeout:
        return QStringLiteral("响应超时");
    case ernc::domain::ErrorCode::ResponseMismatch:
        return QStringLiteral("响应不匹配");
    case ernc::domain::ErrorCode::RetryExhausted:
        return QStringLiteral("重试次数用尽");
    case ernc::domain::ErrorCode::InvalidParameter:
        return QStringLiteral("参数无效");
    case ernc::domain::ErrorCode::ReadOnlyViolation:
        return QStringLiteral("只读参数");
    case ernc::domain::ErrorCode::PayloadSizeMismatch:
        return QStringLiteral("载荷长度不匹配");
    case ernc::domain::ErrorCode::RegionDecodeFailed:
        return QStringLiteral("区域解码失败");
    case ernc::domain::ErrorCode::FileIoFailed:
        return QStringLiteral("文件读写失败");
    case ernc::domain::ErrorCode::DatabaseFailed:
        return QStringLiteral("数据库错误");
    case ernc::domain::ErrorCode::OperationCancelled:
        return QStringLiteral("操作已取消");
    case ernc::domain::ErrorCode::ReadBackMismatch:
        return QStringLiteral("读回不一致");
    case ernc::domain::ErrorCode::Unknown:
        return QStringLiteral("未知错误");
    }
    return QStringLiteral("未知错误");
}

/// @brief 在批量写结果里查找一个参数。
/// @param items 批量写已经记下的项，只在本次调用期间借用。
/// @param id 参数标识，只借用。
/// @return 找到时指向该项。没有时为空指针。
const ernc::application::BatchWriteItem* findItem(const std::vector<ernc::application::BatchWriteItem>& items,
                                                  const ernc::domain::ParameterId& id) {
    for (const ernc::application::BatchWriteItem& item : items) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

/// @brief 这项是不是已经有最终结局。
/// @param item 批量写结果。空指针表示还没有结局。
/// @return Verified、Mismatch 或 Failed 时为 true。排除和未入队不算在途结束。
bool isTerminal(const ernc::application::BatchWriteItem* item) {
    if (item == nullptr) {
        return false;
    }
    return item->kind == ernc::application::BatchWriteKind::Verified
        || item->kind == ernc::application::BatchWriteKind::Mismatch
        || item->kind == ernc::application::BatchWriteKind::Failed;
}

}  // namespace

WritePlanDialog::WritePlanDialog(Sources sources, QWidget* parent)
    : QDialog(parent), sources_{std::move(sources)} {
    setObjectName(QStringLiteral("erncWritePlanDialog"));
    setWindowTitle(QStringLiteral("写入计划"));
    setModal(true);
    resize(760, 420);

    auto* root = new QVBoxLayout(this);
    scopeLabel_ = new QLabel(this);
    scopeLabel_->setObjectName(QStringLiteral("erncWritePlanScope"));
    auto* hint = new QLabel(QStringLiteral("核对目标值和跳过原因。只有点击「确认写入」才会发送。"), this);
    hint->setWordWrap(true);
    diagnosticLabel_ = new QLabel(this);
    diagnosticLabel_->setObjectName(QStringLiteral("erncWritePlanDiagnostic"));
    diagnosticLabel_->setWordWrap(true);
    table_ = new QTableWidget(this);
    table_->setObjectName(QStringLiteral("erncWritePlanTable"));
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("参数"), QStringLiteral("路径"), QStringLiteral("读 Read"), QStringLiteral("设 Set"), QStringLiteral("状态"),
            QStringLiteral("诊断")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(static_cast<int>(WritePlanColumn::Parameter), 158);
    table_->setColumnWidth(static_cast<int>(WritePlanColumn::Path), 158);
    table_->setColumnWidth(static_cast<int>(WritePlanColumn::Status), 135);
    confirmButton_ = new QPushButton(QStringLiteral("确认写入"), this);
    confirmButton_->setObjectName(QStringLiteral("erncWritePlanConfirm"));
    confirmButton_->setAutoDefault(false);
    auto* close = new QPushButton(QStringLiteral("关闭"), this);
    close->setDefault(true);
    auto* buttons = new QHBoxLayout();
    buttons->addStretch(1);
    buttons->addWidget(confirmButton_);
    buttons->addWidget(close);
    root->addWidget(scopeLabel_);
    root->addWidget(hint);
    root->addWidget(diagnosticLabel_);
    root->addWidget(table_, 1);
    root->addLayout(buttons);
    connect(confirmButton_, &QPushButton::clicked, this, [this] { confirmWrite(); });
    connect(close, &QPushButton::clicked, this, &WritePlanDialog::reject);
    rebuild();
}

void WritePlanDialog::setWriter(ernc::application::ParameterBatchWriter* writer, ernc::application::CommandScheduler* scheduler) {
    writer_ = writer;
    scheduler_ = scheduler;
}

void WritePlanDialog::rebuild() {
    table_->setRowCount(0);
    rows_.clear();
    scopeReady_ = false;
    confirmButton_->setEnabled(false);
    const QString page = sources_.pageId;
    scopeLabel_->setText(page.isEmpty() ? QStringLiteral("未选择页面") : page);
    if (page.isEmpty()) {
        diagnosticLabel_->setText(QStringLiteral("未选择页面"));
        return;
    }
    if (sources_.catalog == nullptr || sources_.store == nullptr || sources_.layout == nullptr) {
        diagnosticLabel_->setText(QStringLiteral("没有目录或布局"));
        return;
    }

    diagnosticLabel_->clear();
    const ernc::domain::WriteScope scope = writeScopeForPage(page);
    const ernc::domain::WritePlan plan = ernc::domain::buildWritePlan(*sources_.catalog, *sources_.store, *sources_.layout, scope);
    scopeReady_ = true;

    const auto append = [this](const ernc::domain::ParameterId& id, const QString& path, const QString& status, bool send) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        const ernc::domain::ParameterDefinition* definition = sources_.catalog->find(id.text());
        QString readText;
        QString setText;
        formatReadSet(definition, sources_.store->find(id), readText, setText);
        const QString idText = QString::fromStdString(id.text());
        const QString columns[] = {idText, path, readText, setText, status, QString()};
        for (int column = 0; column < 6; ++column) {
            auto* item = new QTableWidgetItem(columns[column]);
            item->setToolTip(columns[column]);
            if (column == static_cast<int>(WritePlanColumn::Read) || column == static_cast<int>(WritePlanColumn::Set)) {
                item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
            table_->setItem(row, column, item);
        }
        rows_.push_back(PlanRow{id, send});
    };

    int sendCount = 0;
    for (const ernc::domain::PlannedWrite& included : plan.included) {
        const bool send = isOneSlot(*sources_.layout, included.firmwarePath);
        append(included.id, QString::fromStdString(included.firmwarePath),
            send ? QStringLiteral("待写入") : QStringLiteral("非单值或受限制"), send);
        sendCount += send ? 1 : 0;
    }
    for (const ernc::domain::RejectedWrite& rejected : plan.excluded) {
        const ernc::domain::ParameterDefinition* definition = sources_.catalog->find(rejected.id.text());
        const QString path = definition != nullptr && definition->firmwarePath
            ? QString::fromStdString(*definition->firmwarePath)
            : QString::fromStdString(rejected.id.text());
        append(rejected.id, path, exclusionText(rejected.reason), false);
    }
    const QString scopeName = isNavigatorAllPage(page) ? QStringLiteral("全部 · 待写标量") : page;
    scopeLabel_->setText(QStringLiteral("%1 · %2 项待写入 · %3 项跳过")
        .arg(scopeName).arg(sendCount).arg(table_->rowCount() - sendCount));
    confirmButton_->setEnabled(sendCount > 0);
    if (rows_.empty()) {
        diagnosticLabel_->setText(QStringLiteral("当前范围没有待写入的参数"));
    }
}

void WritePlanDialog::setDeviceController(DeviceController* controller) noexcept {
    deviceController_ = controller;
}

bool WritePlanDialog::writeBusy() const {
    if (writer_ != nullptr && writer_->busy()) {
        return true;
    }
    if (deviceController_ != nullptr && deviceController_->writeBusy()) {
        return true;
    }
    return false;
}

void WritePlanDialog::confirmWrite() {
    if (started_ || writeBusy()) {
        return;
    }
    rebuild();
    if (!confirmButton_->isEnabled() || !scopeReady_) {
        return;
    }
    if (deviceController_ != nullptr && (deviceController_->linkKind() == DeviceController::LinkKind::Serial
            || (deviceController_->linkKind() == DeviceController::LinkKind::Simulator && isNavigatorAllPage(sources_.pageId)))) {
        const ernc::domain::ErrorCode code = deviceController_->startWriteCategory(sources_.pageId);
        started_ = code == ernc::domain::ErrorCode::Unknown;
        confirmButton_->setEnabled(false);
        // 串口与模拟器统一由窗口定时器推进，确保本批完成事件也经过控制器。
        diagnosticLabel_->setText(started_ ? QStringLiteral("写入任务已启动") : errorName(code));
        return;
    }
    if (writer_ == nullptr || scheduler_ == nullptr) {
        diagnosticLabel_->setText(QStringLiteral("设备未连接"));
        confirmButton_->setEnabled(false);
        return;
    }
    const ernc::domain::WriteScope scope = writeScopeForPage(sources_.pageId);
    const ernc::domain::Result<void> started = writer_->start(scope);
    started_ = started.ok();
    confirmButton_->setEnabled(false);
    noteProgress();
    if (!started.ok() && started.tryError() != nullptr) {
        diagnosticLabel_->setText(errorName(*started.tryError()));
    }
}

void WritePlanDialog::noteProgress() {
    if (!started_) {
        return;
    }
    if (deviceController_ != nullptr && deviceController_->linkKind() == DeviceController::LinkKind::Serial) {
        deviceController_->refreshWritePlanFromWorker(this);
        return;
    }
    if (writer_ == nullptr || sources_.catalog == nullptr || sources_.store == nullptr || table_ == nullptr) {
        return;
    }
    const std::optional<ernc::application::CommandId> inflight = writer_->inFlight();
    std::optional<ernc::application::WritePhase> phase;
    if (inflight.has_value() && scheduler_ != nullptr) {
        phase = scheduler_->phaseOf(*inflight);
    }
    noteProgressFromWorker(writer_->items(), inflight, phase);
}

void WritePlanDialog::noteProgressFromWorker(const std::vector<ernc::application::BatchWriteItem>& items,
                                             const std::optional<ernc::application::CommandId>& inflight,
                                             const std::optional<ernc::application::WritePhase>& phase) {
    if (!started_ || sources_.catalog == nullptr || sources_.store == nullptr || table_ == nullptr) {
        return;
    }
    int currentRow = -1;
    if (inflight.has_value()) {
        for (int row = 0; row < static_cast<int>(rows_.size()); ++row) {
            if (!rows_[static_cast<std::size_t>(row)].send || isTerminal(findItem(items, rows_[static_cast<std::size_t>(row)].id))) {
                continue;
            }
            currentRow = row;
            break;
        }
    }

    for (int row = 0; row < static_cast<int>(rows_.size()); ++row) {
        const PlanRow& planRow = rows_[static_cast<std::size_t>(row)];
        const ernc::domain::ParameterDefinition* definition = sources_.catalog->find(planRow.id.text());
        QString readText;
        QString setText;
        formatReadSet(definition, sources_.store->find(planRow.id), readText, setText);
        if (QTableWidgetItem* readItem = table_->item(row, static_cast<int>(WritePlanColumn::Read))) {
            readItem->setText(readText);
        }
        if (QTableWidgetItem* setItem = table_->item(row, static_cast<int>(WritePlanColumn::Set))) {
            setItem->setText(setText);
        }
        QString status;
        QString diagnostic;
        const ernc::application::BatchWriteItem* item = findItem(items, planRow.id);
        if (item != nullptr) {
            switch (item->kind) {
            case ernc::application::BatchWriteKind::Excluded:
                status = item->exclusion ? exclusionText(*item->exclusion) : QStringLiteral("不写入");
                break;
            case ernc::application::BatchWriteKind::NotOneSlot:
                status = QStringLiteral("不写入");
                break;
            case ernc::application::BatchWriteKind::Verified:
                // 当前批量标量写收到 FC16 ACK 后结束。历史枚举名不代表独立读回。
                status = QStringLiteral("设备已确认 (ACK)");
                break;
            case ernc::application::BatchWriteKind::Mismatch:
                status = QStringLiteral("读回不一致");
                diagnostic = errorName(item->error.value_or(ernc::domain::ErrorCode::ReadBackMismatch));
                break;
            case ernc::application::BatchWriteKind::Failed:
                status = QStringLiteral("失败");
                diagnostic = errorName(item->error.value_or(ernc::domain::ErrorCode::Unknown));
                break;
            }
        } else if (row == currentRow) {
            // 标量写 readBack 为 false 时 phaseOf 通常为空，只显示「正在写入」直到 ACK 结束。
            if (phase == ernc::application::WritePhase::Acked) {
                status = QStringLiteral("设备已确认");
            } else if (phase == ernc::application::WritePhase::Verifying) {
                status = QStringLiteral("正在读回");
            } else {
                status = QStringLiteral("正在写入");
            }
            if (inflight.has_value()) {
                diagnostic = QString::number(static_cast<qulonglong>(inflight->raw()));
            }
        }
        if (!status.isEmpty()) {
            if (QTableWidgetItem* statusItem = table_->item(row, static_cast<int>(WritePlanColumn::Status))) {
                statusItem->setText(status);
            }
        }
        if (QTableWidgetItem* diagnosticItem = table_->item(row, static_cast<int>(WritePlanColumn::Diagnostic))) {
            if (item != nullptr || row == currentRow) {
                diagnosticItem->setText(diagnostic);
            }
        }
    }
}

int WritePlanDialog::planRowCount() const {
    return table_ == nullptr ? 0 : table_->rowCount();
}

int WritePlanDialog::rowOf(const QString& parameterId) const {
    for (int row = 0; row < planRowCount(); ++row) {
        if (cell(row, static_cast<int>(WritePlanColumn::Parameter)) == parameterId) {
            return row;
        }
    }
    return -1;
}

QString WritePlanDialog::cell(int row, int column) const {
    if (table_ == nullptr) {
        return {};
    }
    const QTableWidgetItem* item = table_->item(row, column);
    return item == nullptr ? QString() : item->text();
}

bool WritePlanDialog::confirmEnabled() const {
    return confirmButton_ != nullptr && confirmButton_->isEnabled();
}

QString WritePlanDialog::diagnosticText() const {
    return diagnosticLabel_ == nullptr ? QString() : diagnosticLabel_->text();
}

void WritePlanDialog::closeEvent(QCloseEvent* event) {
    if (started_ && writeBusy()) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void WritePlanDialog::reject() {
    if (!started_ || !writeBusy()) {
        QDialog::reject();
    }
}

}  // namespace ernc::presentation
