#include "arrays/array_editor_model.hpp"

#include <QString>

#include <cmath>

namespace ernc::presentation {
namespace {

constexpr QStringView kBlank = u"—";

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

QString textOf(const std::optional<ernc::domain::ScalarValue>& value) {
    return value ? formatScalar(*value) : kBlank.toString();
}

std::optional<ernc::domain::ScalarValue> parseText(ernc::domain::ScalarValue::Kind kind, const QString& text) {
    bool ok = false;
    const QString trimmed = text.trimmed();
    if (kind == ernc::domain::ScalarValue::Kind::Float32) {
        const double number = trimmed.toDouble(&ok);
        if (ok && std::isfinite(number)) {
            return ernc::domain::ScalarValue::fromFloat(static_cast<float>(number));
        }
    } else if (kind == ernc::domain::ScalarValue::Kind::Int32) {
        const int number = trimmed.toInt(&ok);
        if (ok) {
            return ernc::domain::ScalarValue::fromInt32(number);
        }
    } else if (kind == ernc::domain::ScalarValue::Kind::UInt32) {
        const uint number = trimmed.toUInt(&ok);
        if (ok) {
            return ernc::domain::ScalarValue::fromUInt32(static_cast<std::uint32_t>(number));
        }
    }
    return std::nullopt;
}

}  // namespace

ArrayEditorModel::ArrayEditorModel(ernc::domain::ArrayState* state, QObject* parent)
    : QAbstractTableModel(parent), state_{state} {}

int ArrayEditorModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid() || state_ == nullptr) {
        return 0;
    }
    return static_cast<int>(state_->size());
}

int ArrayEditorModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : 2;
}

QVariant ArrayEditorModel::data(const QModelIndex& index, int role) const {
    if (state_ == nullptr || role != Qt::DisplayRole || !index.isValid()) {
        return {};
    }
    const ernc::domain::ArrayElement* element = state_->element(static_cast<std::size_t>(index.row()));
    if (element == nullptr) {
        return {};
    }
    if (index.column() == static_cast<int>(ArrayEditorColumn::Read)) {
        return textOf(element->read);
    }
    if (index.column() == static_cast<int>(ArrayEditorColumn::Set)) {
        return textOf(element->set);
    }
    return {};
}

QVariant ArrayEditorModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return {};
    }
    if (section == static_cast<int>(ArrayEditorColumn::Read)) {
        return QStringLiteral("读");
    }
    if (section == static_cast<int>(ArrayEditorColumn::Set)) {
        return QStringLiteral("写");
    }
    return {};
}

Qt::ItemFlags ArrayEditorModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == static_cast<int>(ArrayEditorColumn::Set)) {
        itemFlags |= Qt::ItemIsEditable;
    }
    return itemFlags;
}

bool ArrayEditorModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (state_ == nullptr || role != Qt::EditRole || !index.isValid()
        || index.column() != static_cast<int>(ArrayEditorColumn::Set)) {
        return false;
    }
    const std::optional<ernc::domain::ScalarValue> parsed = parseText(state_->kind(), value.toString());
    if (!parsed) {
        return false;
    }
    if (!state_->setValue(static_cast<std::size_t>(index.row()), *parsed).ok()) {
        return false;
    }
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

void ArrayEditorModel::resetSets() {
    if (state_ == nullptr || state_->size() == 0) {
        if (state_ != nullptr) {
            state_->reset();
        }
        return;
    }
    state_->reset();
    emit dataChanged(this->index(0, 0), this->index(rowCount() - 1, columnCount() - 1), {Qt::DisplayRole, Qt::EditRole});
}

}  // namespace ernc::presentation
