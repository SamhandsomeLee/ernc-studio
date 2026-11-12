#include "pages/can_info_panel.hpp"

#include "device/device_controller.hpp"

#include "theme/theme_tokens.hpp"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QShowEvent>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#ifdef signals
#undef signals
#endif
#include "can_lamp.hpp"
#include "can_mapping.hpp"

namespace ernc::presentation {
namespace {

constexpr QStringView kBlank = u"—";

QString freshnessText(ernc::application::CanFreshness freshness) {
    switch (freshness) {
    case ernc::application::CanFreshness::Fresh:
        return QStringLiteral("新");
    case ernc::application::CanFreshness::Stale:
        return QStringLiteral("旧");
    case ernc::application::CanFreshness::Unknown:
    default:
        return kBlank.toString();
    }
}

QString lampStyle(ernc::application::CanLampColor color) {
    const char* hex = "#687781";
    switch (color) {
    case ernc::application::CanLampColor::Red:
        hex = "#B42318";
        break;
    case ernc::application::CanLampColor::Green:
        hex = "#167A49";
        break;
    case ernc::application::CanLampColor::Gray:
    default:
        hex = "#687781";
        break;
    }
    return QStringLiteral(
        "background-color: %1; border-radius: 7px; min-width: 14px; max-width: 14px; min-height: 14px; max-height: "
        "14px;")
        .arg(QString::fromLatin1(hex));
}

}  // namespace

struct CanInfoPanel::MappingState {
    ernc::application::CanMappingTable table;
    bool ready{false};
};

CanInfoPanel::CanInfoPanel(const ernc::domain::FirmwareLayout* layout, QWidget* parent)
    : QWidget(parent), layout_{layout}, mapping_{std::make_unique<MappingState>()} {
    setObjectName(QStringLiteral("erncCanInfoPanel"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(kSpace16, kSpace16, kSpace16, kSpace16);
    root->setSpacing(kSpace8);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    root->addWidget(statusLabel_);

    tableWidget_ = new QTableWidget(this);
    tableWidget_->setObjectName(QStringLiteral("erncCanInfoTable"));
    tableWidget_->setColumnCount(5);
    tableWidget_->setHorizontalHeaderLabels(
        {QStringLiteral("灯"), QStringLiteral("序号"), QStringLiteral("名称"), QStringLiteral("数值"),
         QStringLiteral("新鲜度")});
    tableWidget_->horizontalHeader()->setStretchLastSection(true);
    tableWidget_->verticalHeader()->setVisible(false);
    tableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tableWidget_->setSelectionMode(QAbstractItemView::NoSelection);
    tableWidget_->setFocusPolicy(Qt::NoFocus);
    root->addWidget(tableWidget_, 1);

    pollTimer_ = new QTimer(this);
    pollTimer_->setInterval(1500);
    connect(pollTimer_, &QTimer::timeout, this, &CanInfoPanel::onPollTick);

    statusLabel_->setText(QStringLiteral("未加载 CAN 映射。"));
}

CanInfoPanel::~CanInfoPanel() = default;

void CanInfoPanel::setNamesPath(const QString& path) {
    namesPath_ = path;
    reloadMapping();
}

void CanInfoPanel::setSlotMapPath(const QString& path) {
    slotMapPath_ = path;
    reloadMapping();
}

void CanInfoPanel::setDeviceController(DeviceController* controller) {
    if (deviceController_ == controller) {
        return;
    }
    if (deviceController_ != nullptr) {
        disconnect(deviceController_, nullptr, this, nullptr);
    }
    deviceController_ = controller;
    if (deviceController_ != nullptr) {
        connect(deviceController_, &DeviceController::canMsgUpdated, this, &CanInfoPanel::onCanMsgUpdated);
        connect(deviceController_, &DeviceController::connectionChanged, this, &CanInfoPanel::onConnectionChanged);
    }
    refreshRows();
    updatePollTimer();
}

void CanInfoPanel::reloadMapping() {
    mapping_->ready = false;
    mapping_->table.signals.clear();
    lampDots_.clear();
    statusLabel_->setText(QStringLiteral("未加载 CAN 映射。"));
    if (namesPath_.isEmpty()) {
        tableWidget_->setRowCount(0);
        return;
    }
    const ernc::domain::Result<ernc::application::CanMappingTable> loaded =
        ernc::application::loadCanMappingTableFromFile(namesPath_.toUtf8().constData());
    if (!loaded.ok() || loaded.tryValue() == nullptr) {
        statusLabel_->setText(QStringLiteral("CAN 名称文件加载失败。"));
        tableWidget_->setRowCount(0);
        return;
    }
    mapping_->table = loaded.value();
    if (!slotMapPath_.isEmpty()) {
        const ernc::domain::Result<void> merged =
            ernc::application::loadCanMsgSlotMapIntoTable(mapping_->table, slotMapPath_.toUtf8().constData());
        if (!merged.ok()) {
            statusLabel_->setText(QStringLiteral("CAN 槽位映射加载失败。"));
            tableWidget_->setRowCount(0);
            return;
        }
    }
    if (layout_ != nullptr) {
        const ernc::domain::Result<void> applied =
            ernc::application::applyConfirmedSlotMap(mapping_->table, *layout_);
        if (!applied.ok()) {
            statusLabel_->setText(QStringLiteral("固件布局无法解析 Can_Msg。"));
            tableWidget_->setRowCount(0);
            return;
        }
    }
    mapping_->ready = mapping_->table.mappingTableConfirmed;
    const ernc::application::CanMappingSummary summary = ernc::application::summarizeCanMapping(mapping_->table);
    statusLabel_->setText(
        QStringLiteral("已映射 %1 / %2 路。").arg(summary.mappedCount).arg(summary.totalSignals));
    const int rows = static_cast<int>(mapping_->table.signals.size());
    tableWidget_->setRowCount(rows);
    lampDots_.resize(static_cast<std::size_t>(rows));
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < tableWidget_->columnCount(); ++col) {
            tableWidget_->setItem(row, col, new QTableWidgetItem());
        }
        auto* wrap = new QWidget(tableWidget_);
        auto* lay = new QHBoxLayout(wrap);
        lay->setContentsMargins(0, 0, 0, 0);
        auto* dot = new QLabel(wrap);
        dot->setFixedSize(14, 14);
        lay->addStretch();
        lay->addWidget(dot);
        lay->addStretch();
        tableWidget_->setCellWidget(row, 0, wrap);
        lampDots_[static_cast<std::size_t>(row)] = dot;
    }
    refreshRows();
}

void CanInfoPanel::refreshRows() {
    if (mapping_ == nullptr || !mapping_->ready || tableWidget_ == nullptr) {
        return;
    }
    const bool haveSnapshot =
        deviceController_ != nullptr && deviceController_->canMsgSnapshotValid();
    const std::vector<float>& snapshot = haveSnapshot ? deviceController_->canMsgSnapshot() : std::vector<float>{};

    for (int row = 0; row < static_cast<int>(mapping_->table.signals.size()); ++row) {
        const ernc::application::CanSignalMapping& signal = mapping_->table.signals[static_cast<std::size_t>(row)];
        bool hasValue = false;
        float raw = 0.0F;
        ernc::application::CanFreshness freshness = ernc::application::CanFreshness::Unknown;
        if (haveSnapshot && signal.arrayIndex.has_value() && *signal.arrayIndex < snapshot.size()) {
            hasValue = true;
            raw = snapshot[*signal.arrayIndex];
            freshness = ernc::application::CanFreshness::Fresh;
        }
        const ernc::application::CanLampColor lamp = ernc::application::evaluateCanLamp(raw, hasValue);

        if (row >= 0 && static_cast<std::size_t>(row) < lampDots_.size() && lampDots_[static_cast<std::size_t>(row)] != nullptr) {
            lampDots_[static_cast<std::size_t>(row)]->setStyleSheet(lampStyle(lamp));
        }
        if (QTableWidgetItem* indexItem = tableWidget_->item(row, 1)) {
            indexItem->setText(QString::number(signal.excelIndex));
            indexItem->setTextAlignment(Qt::AlignCenter);
        }
        if (QTableWidgetItem* nameItem = tableWidget_->item(row, 2)) {
            nameItem->setText(QString::fromStdString(signal.nameZh));
        }
        if (QTableWidgetItem* valueItem = tableWidget_->item(row, 3)) {
            valueItem->setText(hasValue ? QString::number(static_cast<double>(raw), 'g', 6) : kBlank.toString());
            valueItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        }
        if (QTableWidgetItem* freshItem = tableWidget_->item(row, 4)) {
            freshItem->setText(freshnessText(freshness));
            freshItem->setTextAlignment(Qt::AlignCenter);
        }
    }
}

void CanInfoPanel::onPollTick() {
    if (deviceController_ == nullptr || !deviceController_->connected() || deviceController_->canMsgReadBusy()) {
        return;
    }
    deviceController_->readCanMsg();
}

void CanInfoPanel::onCanMsgUpdated(bool success) {
    (void)success;
    refreshRows();
    updatePollTimer();
}

void CanInfoPanel::onConnectionChanged(bool connected) {
    if (!connected) {
        refreshRows();
    }
    updatePollTimer();
}

void CanInfoPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    updatePollTimer();
    onPollTick();
}

void CanInfoPanel::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
    updatePollTimer();
}

void CanInfoPanel::updatePollTimer() {
    if (pollTimer_ == nullptr) {
        return;
    }
    const bool active = isVisible() && deviceController_ != nullptr && deviceController_->connected();
    if (active) {
        if (!pollTimer_->isActive()) {
            pollTimer_->start();
        }
    } else if (pollTimer_->isActive()) {
        pollTimer_->stop();
    }
}

}  // namespace ernc::presentation
