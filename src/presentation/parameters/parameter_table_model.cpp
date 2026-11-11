#include "parameters/parameter_table_model.hpp"

#include "parameters/fake_parameter_rows.hpp"

namespace ernc::presentation {

ParameterTableModel::ParameterTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int ParameterTableModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(std::size(kFakeParameterRows));
}

int ParameterTableModel::columnCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return static_cast<int>(FakeParameterColumn::Count);
}

QVariant ParameterTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) {
        return {};
    }
    const std::string_view cell = fakeParameterCell(index.row(), index.column());
    if (cell.empty()) {
        return {};
    }
    return QString::fromUtf8(cell.data(), static_cast<qsizetype>(cell.size()));
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
    if (fakeParameterColumnIsEditable(index.column())) {
        itemFlags |= Qt::ItemIsEditable;
    }
    return itemFlags;
}

} // namespace ernc::presentation
