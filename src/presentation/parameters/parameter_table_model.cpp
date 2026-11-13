#include "parameters/parameter_table_model.hpp"

#include "parameters/parameter_set_policy.hpp"
#include "parameters/parameter_status_display.hpp"
#include "parameters/scalar_display.hpp"

#include "parameters/fake_parameter_rows.hpp"

#include "arrays/array_summary.hpp"

#include "ernc/domain/array_runtime_store.hpp"

#include <QDateTime>
#include <QTimeZone>

#include <chrono>

namespace ernc::presentation {
namespace {

/// 没有读数或暂存值时的占位。不是数值 0。
constexpr QStringView kBlank = u"—";

/// @brief 把调用方给出的时刻写成 UTC 的 ISO 文字。
/// @param time 系统钟时间点。本函数不读取当前时间。
/// @return 例如 1970-01-01T00:00:10Z。秒以下被丢掉。
QString formatTime(ernc::domain::RuntimeTime time) {
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch());
    return QDateTime::fromSecsSinceEpoch(static_cast<qint64>(seconds.count()), QTimeZone::UTC).toString(Qt::ISODate);
}

} // namespace

ParameterTableModel::ParameterTableModel(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                                         QObject* parent, const ernc::domain::FirmwareLayout* layout,
                                         ernc::domain::ArrayRuntimeStore* arrayStore)
    : QAbstractTableModel(parent), catalog_{catalog}, store_{store}, layout_{layout}, arrayStore_{arrayStore} {
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
    if (session_ != ernc::domain::SessionId::none() && session != session_) {
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

void ParameterTableModel::noteBatchUpdated(ernc::domain::SessionId session, const QStringList& parameterIds) {
    if (session_ != ernc::domain::SessionId::none() && session != session_) {
        return;
    }
    if (parameterIds.isEmpty() || catalog_ == nullptr) {
        return;
    }
    reloadAllRowsFromStore();
}

void ParameterTableModel::reloadAllRowsFromStore() {
    if (catalog_ == nullptr || rowCount() <= 0) {
        return;
    }
    for (int row = 0; row < rows_.size(); ++row) {
        refreshRow(row);
    }
    emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
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
    if (!index.isValid() || catalog_ == nullptr) {
        return {};
    }
    if (index.row() < 0 || static_cast<std::size_t>(index.row()) >= catalog_->size()) {
        return {};
    }
    if (index.row() >= rows_.size()) {
        return {};
    }
    const RowText& row = rows_.at(index.row());
    if (role == Qt::TextAlignmentRole && (index.column() == static_cast<int>(FakeParameterColumn::Read)
            || index.column() == static_cast<int>(FakeParameterColumn::Set))) {
        return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
    }
    if (role != Qt::DisplayRole) {
        return {};
    }
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
    if (section == static_cast<int>(FakeParameterColumn::Read)) {
        return QStringLiteral("读 Read");
    }
    if (section == static_cast<int>(FakeParameterColumn::Set)) {
        return QStringLiteral("设 Set");
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
    text.status = parameterRowStatusText(definition, layout_, store_, arrayStore_);
    text.parameter = QString::fromStdString(definition.id.text());
    text.path = definition.firmwarePath ? QString::fromStdString(*definition.firmwarePath) : kBlank.toString();
    const bool compactArray = usesCompactArrayUi(&definition, layout_);
    const auto* read = state == nullptr ? nullptr : state->displayedRead();
    if (definition.editor && *definition.editor == ernc::domain::EditorKind::Toggle) {
        text.read.clear();
        text.set.clear();
    } else if (compactArray && layout_ != nullptr) {
        text.read = compactArrayCaption(definition, *layout_, arrayStore_);
        text.set = kBlank.toString();
    } else {
        text.read = QString::fromStdString(displayScalar(read));
        text.set = QString::fromStdString(displayScalar(state != nullptr && state->stagedValue ? &*state->stagedValue : nullptr));
    }
    if (compactArray && arrayStore_ != nullptr) {
        const ernc::domain::ArrayRuntimeMeta* meta = arrayStore_->meta(definition.id);
        if (meta != nullptr && meta->readAt) {
            text.updatedAt = formatTime(*meta->readAt);
        } else {
            text.updatedAt = kBlank.toString();
        }
    } else if (state != nullptr && !state->projectReadValue && state->readAt) {
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
