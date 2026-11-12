#include "parameters/parameter_table_model.hpp"

#include "parameters/parameter_set_policy.hpp"

#include "parameters/fake_parameter_rows.hpp"

#include "ernc/domain/control_semantics.hpp"

#include <QDateTime>
#include <QTimeZone>

#include <chrono>

namespace ernc::presentation {
namespace {

/// 没有读数或暂存值时的占位。不是数值 0。
constexpr QStringView kBlank = u"—";

/// @brief 把标量格式化成最短的十进制文字。
/// @param value 标量，只在本次调用期间借用。没有物理单位。
/// @return float 使用一般格式，整数使用十进制。种类无法读取时为「—」。
QString formatScalar(const ernc::domain::ScalarValue& value) {
    if (const ernc::domain::Result<float> number = value.asFloat(); number.ok()) {
        return QString::number(static_cast<double>(number.value()), 'g', 9);
    }
    if (const ernc::domain::Result<std::int32_t> number = value.asInt32(); number.ok()) {
        return QString::number(number.value());
    }
    if (const ernc::domain::Result<std::uint32_t> number = value.asUInt32(); number.ok()) {
        return QString::number(number.value());
    }
    return kBlank.toString();
}

/// @brief 按目录控件把标量格式成界面文字。Toggle 为开、关或未知。
QString formatDisplayValue(const ernc::domain::ParameterDefinition& definition, const ernc::domain::ScalarValue& value) {
    if (definition.editor && *definition.editor == ernc::domain::EditorKind::Toggle) {
        switch (ernc::domain::decodeToggle(definition, value)) {
        case ernc::domain::TogglePosition::On:
            return QStringLiteral("开");
        case ernc::domain::TogglePosition::Off:
            return QStringLiteral("关");
        case ernc::domain::TogglePosition::Unknown:
            return QStringLiteral("未知");
        }
    }
    return formatScalar(value);
}

/// @brief 把调用方给出的时刻写成 UTC 的 ISO 文字。
/// @param time 系统钟时间点。本函数不读取当前时间。
/// @return 例如 1970-01-01T00:00:10Z。秒以下被丢掉。
QString formatTime(ernc::domain::RuntimeTime time) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch());
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(seconds.count()), QTimeZone::UTC).toString(Qt::ISODate);
}

/// @brief 由一条运行状态决定状态列文字。
/// @param state Store 里的记录。空表示还没有记录。
/// @return 工程历史值标记「工程历史读数」，设备缓存过期时为「数据已过期」。有未提交目标时为「待写入」。
/// 有未过期读数时为「已读取」。否则为「未读取」。
QString statusText(const ernc::domain::ParameterRuntimeState* state) {
    if (state == nullptr) {
        return QStringLiteral("未读取");
    }
    if (state->projectReadValue) {
        return QStringLiteral("工程历史读数");
    }
    if (state->stale && state->deviceValue.has_value()) {
        return QStringLiteral("数据已过期");
    }
    if (state->dirty) {
        return QStringLiteral("待写入");
    }
    if (state->deviceValue.has_value() && !state->stale) {
        return QStringLiteral("已读取");
    }
    return QStringLiteral("未读取");
}

} // namespace

ParameterTableModel::ParameterTableModel(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                                         QObject* parent)
    : QAbstractTableModel(parent), catalog_{catalog}, store_{store} {
    if (catalog_ == nullptr) {
        return;
    }
    rows_.resize(static_cast<int>(catalog_->size()));
    for (int row = 0; row < rows_.size(); ++row) {
        refreshRow(row);
    }
}

void ParameterTableModel::bindSession(ernc::domain::SessionId session) noexcept {
    session_ = session;
}

void ParameterTableModel::noteUpdated(ernc::domain::SessionId session, const ernc::domain::ParameterId& id) {
    if (session != session_) {
        return;
    }
    const int row = rowOf(id);
    if (row < 0) {
        return;
    }
    refreshRow(row);
    const QModelIndex left = index(row, 0);
    const QModelIndex right = index(row, columnCount() - 1);
    emit dataChanged(left, right, {Qt::DisplayRole});
}

void ParameterTableModel::noteLocalEdit(const ernc::domain::ParameterId& id) {
    const int row = rowOf(id);
    if (row < 0) {
        return;
    }
    refreshRow(row);
    emit dataChanged(index(row, 0), index(row, columnCount() - 1), {Qt::DisplayRole});
}

void ParameterTableModel::noteDisconnected() {
    if (rowCount() <= 0) {
        return;
    }
    for (int row = 0; row < rows_.size(); ++row) {
        refreshRow(row);
    }
    emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {Qt::DisplayRole});
}

int ParameterTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || catalog_ == nullptr) {
        return 0;
    }
    return static_cast<int>(catalog_->size());
}

int ParameterTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(FakeParameterColumn::Count);
}

QVariant ParameterTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole || catalog_ == nullptr) {
        return {};
    }
    if (index.row() < 0 || static_cast<std::size_t>(index.row()) >= catalog_->size()) {
        return {};
    }
    if (index.row() >= rows_.size()) {
        return {};
    }
    const RowText& row = rows_.at(index.row());
    switch (static_cast<FakeParameterColumn>(index.column())) {
    case FakeParameterColumn::Status:
        return row.status;
    case FakeParameterColumn::Parameter:
        return row.parameter;
    case FakeParameterColumn::Path:
        return row.path;
    case FakeParameterColumn::Read:
        return row.read;
    case FakeParameterColumn::Set:
        return row.set;
    case FakeParameterColumn::UpdatedAt:
        return row.updatedAt;
    case FakeParameterColumn::Count:
        break;
    }
    return {};
}

QVariant ParameterTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return {};
    }
    const std::string_view name = fakeParameterColumnName(section);
    if (name.empty()) {
        return {};
    }
    return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
}

Qt::ItemFlags ParameterTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (catalog_ != nullptr && index.column() == static_cast<int>(FakeParameterColumn::Set)
        && index.row() >= 0 && static_cast<std::size_t>(index.row()) < catalog_->size()) {
        const ernc::domain::ParameterDefinition& definition = catalog_->entries()[static_cast<std::size_t>(index.row())];
        if (parameterSetColumnEditable(definition)) {
            itemFlags |= Qt::ItemIsEditable;
        }
    }
    return itemFlags;
}

// Read 列可以展示工程历史原值。历史值没有本次设备读回时刻。
void ParameterTableModel::refreshRow(int row) {
    if (catalog_ == nullptr || row < 0 || static_cast<std::size_t>(row) >= catalog_->size() || row >= rows_.size()) {
        return;
    }
    const ernc::domain::ParameterDefinition& definition = catalog_->entries()[static_cast<std::size_t>(row)];
    const ernc::domain::ParameterRuntimeState* state = store_ == nullptr ? nullptr : store_->find(definition.id);
    RowText& text = rows_[row];
    text.status = statusText(state);
    text.parameter = QString::fromStdString(definition.id.text());
    text.path = definition.firmwarePath ? QString::fromStdString(*definition.firmwarePath) : kBlank.toString();
    const auto* read = state == nullptr ? nullptr : state->displayedRead();
    if (definition.editor && *definition.editor == ernc::domain::EditorKind::Toggle) {
        text.read.clear();
        text.set.clear();
    } else {
        text.read = read != nullptr ? formatDisplayValue(definition, *read) : kBlank.toString();
        text.set = state != nullptr && state->stagedValue ? formatScalar(*state->stagedValue) : kBlank.toString();
    }
    if (state != nullptr && !state->projectReadValue && state->readAt) {
        text.updatedAt = formatTime(*state->readAt);
    } else if (state != nullptr && state->stagedAt) {
        text.updatedAt = formatTime(*state->stagedAt);
    } else {
        text.updatedAt = kBlank.toString();
    }
}

int ParameterTableModel::rowOf(const ernc::domain::ParameterId& id) const {
    if (catalog_ == nullptr) {
        return -1;
    }
    const std::vector<ernc::domain::ParameterDefinition>& entries = catalog_->entries();
    for (std::size_t row = 0; row < entries.size(); ++row) {
        if (entries[row].id == id) {
            return static_cast<int>(row);
        }
    }
    return -1;
}

} // namespace ernc::presentation
