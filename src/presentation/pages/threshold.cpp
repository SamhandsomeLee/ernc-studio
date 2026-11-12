#include "pages/threshold.hpp"

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
#include <map>

namespace ernc::presentation {
namespace {

constexpr QStringView kBlank = u"—";
constexpr QStringView kArrayMark = u"数组";
constexpr const char* kGroupOrder[] = {"ENC Echo", "ENC Divergence", "Horn", "RNC Divergence", "IO Threshold", "其他阈值"};

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

QString groupFor(const QString& id) {
    if (id.startsWith(QStringLiteral("ENCEcho"))) {
        return QStringLiteral("ENC 回声");
    }
    if (id.startsWith(QStringLiteral("ENC_Divergence"))) {
        return QStringLiteral("ENC 发散");
    }
    if (id.startsWith(QStringLiteral("horn_")) || id.startsWith(QStringLiteral("Horn"))) {
        return QStringLiteral("喇叭");
    }
    if (id.startsWith(QStringLiteral("rnc_dicergence."))) {
        return QStringLiteral("RNC 发散");
    }
    if (id == QStringLiteral("Input_threshold") || id == QStringLiteral("Output_Threshold")) {
        return QStringLiteral("IO 阈值");
    }
    return QStringLiteral("其他阈值");
}

}  // namespace

ThresholdPage::ThresholdPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                             const ernc::domain::FirmwareLayout* layout, QWidget* parent)
    : QWidget(parent), catalog_{catalog}, store_{store}, layout_{layout} {
    setObjectName(QStringLiteral("erncThresholdPage"));
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
    if (catalog_ == nullptr) {
        return;
    }

    std::map<QString, std::vector<const ernc::domain::ParameterDefinition*>> grouped;
    for (const ernc::domain::ParameterDefinition& definition : catalog_->entries()) {
        if (!definition.category || *definition.category != "Threshold") {
            continue;
        }
        grouped[groupFor(QString::fromStdString(definition.id.text()))].push_back(&definition);
    }
    for (const char* groupName : kGroupOrder) {
        const auto found = grouped.find(QString::fromUtf8(groupName));
        if (found == grouped.end()) {
            continue;
        }
        auto* heading = new QLabel(QString::fromUtf8(groupName), content);
        root->addWidget(heading);
        for (const ernc::domain::ParameterDefinition* definition : found->second) {
            const QString parameterId = QString::fromStdString(definition->id.text());
            const bool oneSlot = definition->firmwarePath && isOneScalarSlot(layout_, *definition->firmwarePath);
            auto* row = new QWidget(content);
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
            control.group = found->first;
            control.value = value;
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
                auto* shortcut = new QPushButton(QStringLiteral("定位"), row);
                function_view_layout::styleActionButton(shortcut);
                line->addWidget(shortcut);
                connect(shortcut, &QPushButton::clicked, this, [this, parameterId] { activate(parameterId); });
            }
            function_view_layout::sealFormRow(line);
            controls_.push_back(control);
            root->addWidget(row);
        }
    }
    root->addStretch(1);
    reload();
}

void ThresholdPage::reload() {
    reloading_ = true;
    for (Control& control : controls_) {
        if (control.editor == nullptr || catalog_ == nullptr) {
            continue;
        }
        const ernc::domain::ParameterDefinition* definition = catalog_->find(control.id.toStdString());
        const ernc::domain::ParameterRuntimeState* state =
            definition == nullptr || store_ == nullptr ? nullptr : store_->find(definition->id);
        const QString text = formatScalar(shownValue(state));
        control.value->setText(text);
        control.editor->setText(text == kBlank.toString() ? QString() : text);
    }
    reloading_ = false;
}

void ThresholdPage::setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    editObserver_ = std::move(observer);
}

void ThresholdPage::setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    selectionObserver_ = std::move(observer);
}

bool ThresholdPage::containsParameter(const QString& parameterId) const {
    return findControl(parameterId) != nullptr;
}

QString ThresholdPage::groupOf(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control == nullptr ? QString() : control->group;
}

QString ThresholdPage::valueText(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control == nullptr || control->value == nullptr ? QString() : control->value->text();
}

bool ThresholdPage::hasEditor(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control != nullptr && control->editor != nullptr;
}

bool ThresholdPage::hasFormulaButton() const {
    const auto buttons = findChildren<QPushButton*>();
    for (const QPushButton* button : buttons) {
        if (button->text().contains(QStringLiteral("计算")) || button->text().contains(QStringLiteral("公式"))) {
            return true;
        }
    }
    return false;
}

void ThresholdPage::commitValue(const QString& parameterId, const QString& text) {
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

void ThresholdPage::activate(const QString& parameterId) {
    if (catalog_ == nullptr || findControl(parameterId) == nullptr) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr) {
        return;
    }
    selectedId_ = QString::fromStdString(definition->id.text());
    if (selectionObserver_) {
        selectionObserver_(definition->id);
    }
}

QString ThresholdPage::selectedParameterId() const {
    return selectedId_;
}

ThresholdPage::Control* ThresholdPage::findControl(const QString& parameterId) {
    for (Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

const ThresholdPage::Control* ThresholdPage::findControl(const QString& parameterId) const {
    for (const Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

void ThresholdPage::notifyEdited(const ernc::domain::ParameterId& id) {
    if (editObserver_) {
        editObserver_(id);
    }
    reload();
}

}  // namespace ernc::presentation
