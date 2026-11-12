#include "controls/parameter_delegates.hpp"

#include "controls/parameter_toggle_switch.hpp"
#include "parameters/fake_parameter_rows.hpp"
#include "parameters/parameter_table_model.hpp"
#include "parameters/parameter_set_policy.hpp"

#include <QApplication>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStyle>

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

/// @brief 暂存值优先，否则用界面显示的 Read（可能来自工程）。
/// @param state 运行状态。空表示没有记录。
/// @return 指向状态内部的标量。不转移所有权。两个都空时为空指针。
const ernc::domain::ScalarValue* shownValue(const ernc::domain::ParameterRuntimeState* state) {
    if (state == nullptr) {
        return nullptr;
    }
    if (state->stagedValue) {
        return &*state->stagedValue;
    }
    return state->displayedRead();
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
    if (definition == nullptr) {
        return nullptr;
    }
    if (definition->editor && *definition->editor == ernc::domain::EditorKind::Toggle) {
        return nullptr;
    }
    if (parameterSetUsesNumericEditor(*definition)) {
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
    if (auto* toggle = qobject_cast<ParameterToggleSwitch*>(editor)) {
        toggle->applyCaption(caption);
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
    if (auto* toggle = qobject_cast<ParameterToggleSwitch*>(editor)) {
        const QString text = toggle->currentWord();
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
    if (definition->editor && *definition->editor == ernc::domain::EditorKind::Toggle) {
        option->text.clear();
        option->displayAlignment = Qt::AlignCenter;
        return;
    }
    const ernc::domain::ParameterRuntimeState* state = store_ == nullptr ? nullptr : store_->find(definition->id);
    option->text = setCaption(*definition, state);
}

void ParameterDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
    if (painter == nullptr || !isToggleSetCell(index)) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    QStyleOptionViewItem styled(option);
    initStyleOption(&styled, index);
    styled.text.clear();
    QApplication::style()->drawControl(QStyle::CE_ItemViewItem, &styled, painter);
    const ernc::domain::ParameterDefinition* definition = definitionFor(index);
    if (definition == nullptr) {
        return;
    }
    const ernc::domain::ParameterRuntimeState* state = store_ == nullptr ? nullptr : store_->find(definition->id);
    const ParameterToggleSwitch::VisualState visual =
        visualStateFromCaption(setCaption(*definition, state));
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    paintParameterToggleSwitch(*painter, styled.rect, visual, styled.state.testFlag(QStyle::State_HasFocus));
    painter->restore();
}

bool ParameterDelegate::editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                                    const QModelIndex& index) {
    if (!isToggleSetCell(index) || event == nullptr || model == nullptr) {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
    if (event->type() != QEvent::MouseButtonRelease) {
        return QStyledItemDelegate::editorEvent(event, model, option, index);
    }
    auto* mouse = dynamic_cast<QMouseEvent*>(event);
    if (mouse == nullptr || mouse->button() != Qt::LeftButton) {
        return false;
    }
    const QRect track = toggleTrackRectInCell(option.rect);
    if (!track.contains(mouse->pos())) {
        return false;
    }
    const ernc::domain::ParameterDefinition* definition = definitionFor(index);
    if (definition == nullptr) {
        return false;
    }
    const bool chooseOn = toggleChoosesOnFromCellClick(option.rect, mouse->pos());
    const QString word = chooseOn ? QStringLiteral("开") : QStringLiteral("关");
    if (!stageToggleChoice(*definition, word, model, index)) {
        return false;
    }
    model->dataChanged(index, index, {Qt::DisplayRole});
    return true;
}

bool ParameterDelegate::isToggleSetCell(const QModelIndex& index) const {
    if (index.column() != static_cast<int>(FakeParameterColumn::Set)) {
        return false;
    }
    const ernc::domain::ParameterDefinition* definition = definitionFor(index);
    return definition != nullptr && definition->editor && *definition->editor == ernc::domain::EditorKind::Toggle;
}

bool ParameterDelegate::stageToggleChoice(const ernc::domain::ParameterDefinition& definition, const QString& word,
                                            QAbstractItemModel* model, const QModelIndex& index) const {
    if (!stager_) {
        return false;
    }
    ernc::domain::TogglePosition position = ernc::domain::TogglePosition::Unknown;
    if (word == QStringLiteral("开")) {
        position = ernc::domain::TogglePosition::On;
    } else if (word == QStringLiteral("关")) {
        position = ernc::domain::TogglePosition::Off;
    } else {
        return false;
    }
    const ernc::domain::Result<ernc::domain::ScalarValue> encoded = ernc::domain::encodeToggle(definition, position);
    if (!encoded.ok() || encoded.tryValue() == nullptr) {
        return false;
    }
    const ernc::domain::RuntimeTime stagedAt = std::chrono::system_clock::now();
    if (!stager_->stage(definition.id, *encoded.tryValue(), stagedAt).ok()) {
        return false;
    }
    if (ParameterTableModel* table = tableFor(index)) {
        table->noteLocalEdit(definition.id);
    }
    (void)model;
    return true;
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
