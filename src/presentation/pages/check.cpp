#include "pages/check.hpp"

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
constexpr const char* kModeIds[] = {"check_mod", "TranFuncFlag"};
constexpr const char* kRegionIds[] = {"check_data", "ERNC_TF"};

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

QString formatFloat(const ernc::domain::ScalarValue* value) {
    if (value == nullptr) {
        return kBlank.toString();
    }
    const ernc::domain::Result<float> number = value->asFloat();
    if (!number.ok()) {
        return kBlank.toString();
    }
    return QString::number(static_cast<double>(number.value()), 'g', 9);
}

}  // namespace

CheckPage::CheckPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                     const ernc::domain::FirmwareLayout* layout, QWidget* parent)
    : QWidget(parent), catalog_{catalog}, store_{store}, layout_{layout} {
    setObjectName(QStringLiteral("erncCheckPage"));
    if (catalog_ != nullptr && layout_ != nullptr && store_ != nullptr) {
        stager_.emplace(*catalog_, *layout_, *store_);
    }
    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget(scroll);
    auto* root = new QVBoxLayout(content);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(kSpace4);
    scroll->setWidget(content);
    pageLayout->addWidget(scroll);
    scroll_ = scroll;

    auto* modes = new QLabel(QStringLiteral("模式"), content);
    root->addWidget(modes);
    for (const char* id : kModeIds) {
        addRow(content, root, QString::fromUtf8(id));
    }
    auto* regions = new QLabel(QStringLiteral("区域"), content);
    root->addWidget(regions);
    for (const char* id : kRegionIds) {
        addRow(content, root, QString::fromUtf8(id));
    }
    auto* canButton = new QPushButton(QStringLiteral("CAN 名称"), content);
    canButton->setObjectName(QStringLiteral("erncOpenCanNames"));
    canButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(canButton, &QPushButton::clicked, this, [this] { openCanCatalog(); });
    root->addWidget(canButton, 0, Qt::AlignLeft);
    root->addStretch(1);
    reload();
}

void CheckPage::addRow(QWidget* parent, QVBoxLayout* layout, const QString& parameterId) {
    if (catalog_ == nullptr || layout == nullptr || catalog_->find(parameterId.toStdString()) == nullptr) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    const bool oneSlot = definition->firmwarePath && isOneScalarSlot(layout_, *definition->firmwarePath);
    auto* row = new QWidget(parent);
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(kSpace8);
    auto* name = new QPushButton(parameterId, row);
    name->setFlat(true);
    name->setStyleSheet(QStringLiteral("text-align: left; padding: 0 4px;"));
    function_view_layout::styleParameterNameButton(name);
    auto* value = new QLabel(oneSlot ? kBlank.toString() : kArrayMark.toString(), row);
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
        auto* confirmButton = new QPushButton(QStringLiteral("确认"), row);
        function_view_layout::styleActionButton(confirmButton);
        line->addWidget(confirmButton);
        control.editor = editor;
        connect(confirmButton, &QPushButton::clicked, this, [this, parameterId] { confirm(parameterId); });
    } else {
        auto* hint = new QLabel(function_view_layout::parameterViewHintText(), row);
        hint->setWordWrap(true);
        hint->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        line->addWidget(hint);
    }
    function_view_layout::sealFormRow(line);
    controls_.push_back(control);
    layout->addWidget(row);
}

void CheckPage::reload() {
    reloading_ = true;
    for (Control& control : controls_) {
        if (control.editor == nullptr || catalog_ == nullptr) {
            continue;
        }
        const ernc::domain::ParameterDefinition* definition = catalog_->find(control.id.toStdString());
        const ernc::domain::ParameterRuntimeState* state =
            definition == nullptr || store_ == nullptr ? nullptr : store_->find(definition->id);
        const QString text = formatFloat(shownValue(state));
        control.value->setText(text);
        control.editor->setText(text == kBlank.toString() ? QString() : text);
    }
    reloading_ = false;
}

void CheckPage::setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    editObserver_ = std::move(observer);
}

void CheckPage::setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    selectionObserver_ = std::move(observer);
}

void CheckPage::setCanOpener(std::function<void()> opener) {
    canOpener_ = std::move(opener);
}

bool CheckPage::containsParameter(const QString& parameterId) const {
    return findControl(parameterId) != nullptr;
}

QString CheckPage::valueText(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control == nullptr || control->value == nullptr ? QString() : control->value->text();
}

bool CheckPage::hasEditor(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control != nullptr && control->editor != nullptr;
}

void CheckPage::setDraft(const QString& parameterId, const QString& text) {
    Control* control = findControl(parameterId);
    if (control == nullptr || control->editor == nullptr) {
        return;
    }
    control->editor->setText(text);
}

void CheckPage::confirm(const QString& parameterId) {
    if (!stager_ || catalog_ == nullptr) {
        return;
    }
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->editor == nullptr) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr || !definition->dataType || *definition->dataType != ernc::domain::DataType::Float32) {
        return;
    }
    bool ok = false;
    const double number = control->editor->text().trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
        return;
    }
    const ernc::domain::RuntimeTime stagedAt = std::chrono::system_clock::now();
    if (!stager_->stage(definition->id, ernc::domain::ScalarValue::fromFloat(static_cast<float>(number)), stagedAt).ok()) {
        return;
    }
    notifyEdited(definition->id);
}

void CheckPage::activate(const QString& parameterId) {
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

void CheckPage::emphasizeParameter(const QString& parameterId) {
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

void CheckPage::openCanCatalog() {
    if (canOpener_) {
        canOpener_();
    }
}

QString CheckPage::selectedParameterId() const {
    return selectedId_;
}

CheckPage::Control* CheckPage::findControl(const QString& parameterId) {
    for (Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

const CheckPage::Control* CheckPage::findControl(const QString& parameterId) const {
    for (const Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

void CheckPage::notifyEdited(const ernc::domain::ParameterId& id) {
    if (editObserver_) {
        editObserver_(id);
    }
    reload();
}

}  // namespace ernc::presentation
