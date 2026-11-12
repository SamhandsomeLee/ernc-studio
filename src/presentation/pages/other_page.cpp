#include "pages/other_page.hpp"

#include "arrays/array_summary.hpp"
#include "ernc/domain/catalog.hpp"
#include "pages/function_view_layout.hpp"

#include "theme/theme_tokens.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <chrono>
#include <cmath>

namespace ernc::presentation {
namespace {

constexpr QStringView kBlank = u"—";
constexpr QStringView kArrayMark = u"数组";

/// @brief 暂存值优先，否则用界面显示的 Read（可能来自工程）。
/// @param state 运行状态，仅在本次调用期间借用，空时没有可显示的值。
/// @return 借用状态内的标量，未有数值时为空。
const ernc::domain::ScalarValue* shownValue(const ernc::domain::ParameterRuntimeState* state) {
    if (state == nullptr) {
        return nullptr;
    }
    if (state->stagedValue) {
        return &*state->stagedValue;
    }
    return state->displayedRead();
}

bool isOneScalarSlot(const ernc::domain::FirmwareLayout* layout, std::string_view firmwarePath) {
    if (layout == nullptr || firmwarePath.empty()) {
        return false;
    }
    const ernc::domain::LayoutResolve located = layout->locate(firmwarePath);
    if (!located.ok()) {
        return false;
    }
    const ernc::domain::FieldLayout& field = located.field();
    return field.scalarType.has_value() && field.slotCount == 1U && field.size == 4U;
}

QString formatScalar(const ernc::domain::ScalarValue* value) {
    if (value == nullptr) {
        return kBlank.toString();
    }
    if (const ernc::domain::Result<float> number = value->asFloat(); number.ok()) {
        return QString::number(static_cast<double>(number.value()), 'g', 9);
    }
    if (const ernc::domain::Result<std::int32_t> number = value->asInt32(); number.ok()) {
        return QString::number(number.value());
    }
    if (const ernc::domain::Result<std::uint32_t> number = value->asUInt32(); number.ok()) {
        return QString::number(number.value());
    }
    return kBlank.toString();
}

}  // namespace

OtherPage::OtherPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                     const ernc::domain::FirmwareLayout* layout, QWidget* parent)
    : QWidget(parent), catalog_{catalog}, store_{store}, layout_{layout} {
    setObjectName(QStringLiteral("erncOtherPage"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    if (catalog_ != nullptr && layout_ != nullptr && store_ != nullptr) {
        stager_.emplace(*catalog_, *layout_, *store_);
    }
    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    auto* content = new QWidget(scroll);
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(kSpace4);
    scroll->setWidget(content);
    pageLayout->addWidget(scroll, 1);
    scroll_ = scroll;
    if (catalog_ == nullptr) {
        return;
    }

    for (const ernc::domain::ParameterDefinition& definition : catalog_->entries()) {
        if (!definition.category
            || *definition.category != std::string(ernc::domain::kOtherPageCategory)) {
            continue;
        }
        if (definition.id.text() == "Can_Msg") {
            continue;
        }
        const bool compactArray = usesCompactArrayUi(&definition, layout_);
        const bool oneSlot =
            definition.firmwarePath && isOneScalarSlot(layout_, *definition.firmwarePath) && !compactArray;
        const QString parameterId = QString::fromStdString(definition.id.text());
        auto* row = new QWidget(content);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(kSpace8);
        auto* name = new QPushButton(parameterId, row);
        name->setFlat(true);
        name->setStyleSheet(QStringLiteral("text-align: left; padding: 0 4px;"));
        function_view_layout::styleParameterNameButton(name);
        auto* value = new QLabel(
            compactArray && layout_ != nullptr
                ? compactArrayCaption(definition, *layout_, arrayStore_)
                : (oneSlot ? kBlank.toString() : kArrayMark.toString()),
            row);
        function_view_layout::styleValueLabel(value);
        line->addWidget(name);
        line->addWidget(value);
        function_view_layout::gapBetweenSecondAndThirdColumns(line);
        Control control;
        control.id = parameterId;
        control.value = value;
        control.row = row;
        connect(name, &QPushButton::clicked, this, [this, parameterId] { activate(parameterId); });
        if (oneSlot) {
            auto* editor = new QLineEdit(row);
            function_view_layout::styleScalarEditor(editor);
            line->addWidget(editor);
            control.editor = editor;
            connect(editor, &QLineEdit::editingFinished, this, [this, parameterId] {
                if (reloading_) {
                    return;
                }
                const Control* rowControl = findControl(parameterId);
                if (rowControl != nullptr && rowControl->editor != nullptr) {
                    commitValue(parameterId, rowControl->editor->text());
                }
            });
        } else {
            auto* hint = new QLabel(function_view_layout::parameterViewHintText(), row);
            hint->setWordWrap(true);
            hint->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            function_view_layout::styleParameterViewHint(hint);
            line->addWidget(hint);
        }
        function_view_layout::sealFormRow(line);
        controls_.push_back(control);
        root->addWidget(row);
    }
    root->addStretch(1);
    reload();
}

void OtherPage::reload() {
    reloading_ = true;
    for (Control& control : controls_) {
        if (catalog_ == nullptr) {
            continue;
        }
        const ernc::domain::ParameterDefinition* definition = catalog_->find(control.id.toStdString());
        if (definition == nullptr) {
            continue;
        }
        if (usesCompactArrayUi(definition, layout_)) {
            if (layout_ != nullptr) {
                control.value->setText(compactArrayCaption(*definition, *layout_, arrayStore_));
            }
            continue;
        }
        if (control.editor == nullptr) {
            continue;
        }
        const ernc::domain::ParameterRuntimeState* state =
            store_ == nullptr ? nullptr : store_->find(definition->id);
        const QString text = formatScalar(shownValue(state));
        control.value->setText(text);
        control.editor->setText(text == kBlank.toString() ? QString() : text);
    }
    reloading_ = false;
}

void OtherPage::setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    editObserver_ = std::move(observer);
}

void OtherPage::setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    selectionObserver_ = std::move(observer);
}

bool OtherPage::containsParameter(const QString& parameterId) const {
    return findControl(parameterId) != nullptr;
}

QString OtherPage::valueText(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control == nullptr || control->value == nullptr ? QString() : control->value->text();
}

bool OtherPage::hasEditor(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control != nullptr && control->editor != nullptr;
}

void OtherPage::commitValue(const QString& parameterId, const QString& text) {
    if (!stager_ || catalog_ == nullptr) {
        return;
    }
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->editor == nullptr) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr || !definition->dataType || !definition->firmwarePath
        || !isOneScalarSlot(layout_, *definition->firmwarePath)) {
        return;
    }
    const QString trimmed = text.trimmed();
    bool ok = false;
    std::optional<ernc::domain::ScalarValue> value;
    if (*definition->dataType == ernc::domain::DataType::Float32) {
        const double number = trimmed.toDouble(&ok);
        if (ok && std::isfinite(number)) {
            value = ernc::domain::ScalarValue::fromFloat(static_cast<float>(number));
        }
    } else if (*definition->dataType == ernc::domain::DataType::UInt32) {
        const uint number = trimmed.toUInt(&ok);
        if (ok) {
            value = ernc::domain::ScalarValue::fromUInt32(static_cast<std::uint32_t>(number));
        }
    } else if (*definition->dataType == ernc::domain::DataType::Int32) {
        const int number = trimmed.toInt(&ok);
        if (ok) {
            value = ernc::domain::ScalarValue::fromInt32(number);
        }
    }
    if (!value) {
        return;
    }
    if (!stager_->stage(definition->id, *value, std::chrono::system_clock::now()).ok()) {
        return;
    }
    notifyEdited(definition->id);
}

void OtherPage::activate(const QString& parameterId) {
    if (catalog_ == nullptr || findControl(parameterId) == nullptr) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr) {
        return;
    }
    selectedId_ = QString::fromStdString(definition->id.text());
    emphasizeParameter(parameterId);
    if (selectionObserver_) {
        selectionObserver_(definition->id);
    }
}

void OtherPage::emphasizeParameter(const QString& parameterId) {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->row == nullptr) {
        return;
    }
    if (emphasizedRow_ != nullptr && emphasizedRow_ != control->row) {
        function_view_layout::clearRowEmphasis(emphasizedRow_);
    }
    emphasizedRow_ = control->row;
    function_view_layout::emphasizeParameterRow(control->row, scroll_);
}

QString OtherPage::selectedParameterId() const {
    return selectedId_;
}

OtherPage::Control* OtherPage::findControl(const QString& parameterId) {
    for (Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

const OtherPage::Control* OtherPage::findControl(const QString& parameterId) const {
    for (const Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

void OtherPage::notifyEdited(const ernc::domain::ParameterId& id) {
    if (editObserver_) {
        editObserver_(id);
    }
    reload();
}

}  // namespace ernc::presentation
