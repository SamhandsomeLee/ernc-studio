#include "controls/parameter_delegates.hpp"

#include "parameters/fake_parameter_rows.hpp"
#include "parameters/parameter_table_model.hpp"

#include <QComboBox>
#include <QLineEdit>
#include <QSortFilterProxyModel>

#include <chrono>
#include <cmath>

namespace ernc::presentation {
namespace {

/// 没有值时的占位。不是数值 0，也不是关。
constexpr QStringView kBlank = u"—";

/// @brief 把标量格式化成最短十进制。不把时间备注换算成秒。
/// @param value 标量，只借用。
/// @return float 的一般格式，或整数的十进制。读不到种类时为「—」。
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

/// @brief 暂存值优先，否则用设备读数。
/// @param state 运行状态。空表示没有记录。
/// @return 指向状态内部的标量。不转移所有权。两个都空时为空指针。
const ernc::domain::ScalarValue* shownValue(const ernc::domain::ParameterRuntimeState* state) {
    if (state == nullptr) {
        return nullptr;
    }
    if (state->stagedValue) {
        return &*state->stagedValue;
    }
    if (state->deviceValue) {
        return &*state->deviceValue;
    }
    return nullptr;
}

} // namespace

ParameterDelegate::ParameterDelegate(const ernc::domain::ParameterCatalog* catalog, const ernc::domain::FirmwareLayout* layout,
                                     ernc::domain::ParameterRuntimeStore* store, QObject* parent)
    : QStyledItemDelegate(parent), catalog_{catalog}, layout_{layout}, store_{store} {
    if (catalog_ != nullptr && layout_ != nullptr && store_ != nullptr) {
        stager_.emplace(*catalog_, *layout_, *store_);
    }
}

QString ParameterDelegate::setCaption(const ernc::domain::ParameterDefinition& definition,
                                      const ernc::domain::ParameterRuntimeState* state) const {
    const ernc::domain::ScalarValue* value = shownValue(state);
    if (definition.editor && *definition.editor == ernc::domain::EditorKind::Toggle) {
        if (value == nullptr) {
            return QStringLiteral("未知");
        }
        switch (ernc::domain::decodeToggle(definition, *value)) {
        case ernc::domain::TogglePosition::On:
            return QStringLiteral("开");
        case ernc::domain::TogglePosition::Off:
            return QStringLiteral("关");
        case ernc::domain::TogglePosition::Unknown:
            return QStringLiteral("未知");
        }
    }
    return value == nullptr ? kBlank.toString() : formatScalar(*value);
}

QWidget* ParameterDelegate::createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    (void)option;
    if (index.column() != static_cast<int>(FakeParameterColumn::Set)) {
        return nullptr;
    }
    const ernc::domain::ParameterDefinition* definition = definitionFor(index);
    if (definition == nullptr || !definition->editor) {
        return nullptr;
    }
    if (*definition->editor == ernc::domain::EditorKind::Toggle) {
        auto* box = new QComboBox(parent);
        box->addItem(QStringLiteral("开"));
        box->addItem(QStringLiteral("关"));
        return box;
    }
    if (*definition->editor == ernc::domain::EditorKind::Numeric) {
        return new QLineEdit(parent);
    }
    return nullptr;
}

void ParameterDelegate::setEditorData(QWidget* editor, const QModelIndex& index) const {
    const ernc::domain::ParameterDefinition* definition = definitionFor(index);
    if (definition == nullptr) {
        return;
    }
    const ernc::domain::ParameterRuntimeState* state = store_ == nullptr ? nullptr : store_->find(definition->id);
    const QString caption = setCaption(*definition, state);
    if (auto* box = qobject_cast<QComboBox*>(editor)) {
        const int found = box->findText(caption);
        box->setCurrentIndex(found);
        return;
    }
    if (auto* line = qobject_cast<QLineEdit*>(editor)) {
        line->setText(caption == QStringLiteral("—") ? QString() : caption);
    }
}

void ParameterDelegate::setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const {
    (void)model;
    const ernc::domain::ParameterDefinition* definition = definitionFor(index);
    if (definition == nullptr || !stager_) {
        return;
    }
    const ernc::domain::RuntimeTime stagedAt = std::chrono::system_clock::now();
    ernc::domain::Result<ernc::domain::ScalarValue> value =
        ernc::domain::Result<ernc::domain::ScalarValue>::failure(ernc::domain::ErrorCode::InvalidParameter);
    if (auto* box = qobject_cast<QComboBox*>(editor)) {
        const QString text = box->currentText();
        ernc::domain::TogglePosition position = ernc::domain::TogglePosition::Unknown;
        if (text == QStringLiteral("开")) {
            position = ernc::domain::TogglePosition::On;
        } else if (text == QStringLiteral("关")) {
            position = ernc::domain::TogglePosition::Off;
        }
        value = ernc::domain::encodeToggle(*definition, position);
    } else if (auto* line = qobject_cast<QLineEdit*>(editor)) {
        if (!definition->dataType) {
            return;
        }
        bool ok = false;
        if (*definition->dataType == ernc::domain::DataType::Float32) {
            const float number = line->text().toFloat(&ok);
            if (ok && std::isfinite(number)) {
                value = ernc::domain::Result<ernc::domain::ScalarValue>::success(ernc::domain::ScalarValue::fromFloat(number));
            }
        } else if (*definition->dataType == ernc::domain::DataType::Int32) {
            const int number = line->text().toInt(&ok);
            if (ok) {
                value = ernc::domain::Result<ernc::domain::ScalarValue>::success(ernc::domain::ScalarValue::fromInt32(number));
            }
        } else if (*definition->dataType == ernc::domain::DataType::UInt32) {
            const uint number = line->text().toUInt(&ok);
            if (ok) {
                value = ernc::domain::Result<ernc::domain::ScalarValue>::success(
                    ernc::domain::ScalarValue::fromUInt32(static_cast<std::uint32_t>(number)));
            }
        }
    }
    if (!value.ok() || value.tryValue() == nullptr) {
        return;
    }
    if (!stager_->stage(definition->id, *value.tryValue(), stagedAt).ok()) {
        return;
    }
    if (ParameterTableModel* table = tableFor(index)) {
        table->noteLocalEdit(definition->id);
    }
}

void ParameterDelegate::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const {
    QStyledItemDelegate::initStyleOption(option, index);
    if (option == nullptr || index.column() != static_cast<int>(FakeParameterColumn::Set)) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = definitionFor(index);
    if (definition == nullptr) {
        return;
    }
    const ernc::domain::ParameterRuntimeState* state = store_ == nullptr ? nullptr : store_->find(definition->id);
    option->text = setCaption(*definition, state);
}

const ernc::domain::ParameterDefinition* ParameterDelegate::definitionFor(const QModelIndex& index) const {
    if (catalog_ == nullptr || !index.isValid()) {
        return nullptr;
    }
    QModelIndex source = index;
    if (const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(index.model())) {
        source = proxy->mapToSource(index);
    }
    if (!source.isValid() || source.row() < 0 || static_cast<std::size_t>(source.row()) >= catalog_->size()) {
        return nullptr;
    }
    return &catalog_->entries()[static_cast<std::size_t>(source.row())];
}

ParameterTableModel* ParameterDelegate::tableFor(const QModelIndex& index) const {
    const QAbstractItemModel* model = index.model();
    if (const auto* proxy = qobject_cast<const QSortFilterProxyModel*>(model)) {
        model = proxy->sourceModel();
    }
    return dynamic_cast<ParameterTableModel*>(const_cast<QAbstractItemModel*>(model));
}

} // namespace ernc::presentation
