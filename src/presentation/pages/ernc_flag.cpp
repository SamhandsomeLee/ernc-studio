#include "pages/ernc_flag.hpp"

#include "controls/parameter_toggle_switch.hpp"
#include "pages/function_view_layout.hpp"

#include "ernc/application/stage_value.hpp"
#include "ernc/domain/control_semantics.hpp"
#include "theme/theme_tokens.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <chrono>
#include <cstdint>

namespace ernc::presentation {
namespace {

/// 本页固定名单。顺序与目录里 ERNC_Flag 的出现顺序一致。
constexpr const char* kFlagIds[] = {
    "ANC_OFF", "ENC_OFF", "RNC_OFF", "RNC_refresh", "SpeakerCheckValue", "Bypass_mod"};

/// 没有数值时的占位。不是 0，也不是关。
constexpr QStringView kBlank = u"—";

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

/// @brief 把反向开关显示成开、关或 Unknown。
/// @param definition 目录定义，只借用。必须是 toggle。
/// @param value 当前标量。空表示还没有值。
/// @return 线值 0 为开，线值 1 为关。其它值和空值都是 Unknown，不是关。
QString toggleText(const ernc::domain::ParameterDefinition& definition, const ernc::domain::ScalarValue* value) {
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
    return QStringLiteral("未知");
}

/// @brief 把 uint32 显示成十进制。
/// @param value 当前标量。空或种类不是 uint32 时不用别的种类顶替。
/// @return 十进制原值。没有值时为「—」。不显示开或关。
QString rawUintText(const ernc::domain::ScalarValue* value) {
    if (value == nullptr) {
        return kBlank.toString();
    }
    const ernc::domain::Result<std::uint32_t> number = value->asUInt32();
    if (!number.ok()) {
        return kBlank.toString();
    }
    return QString::number(number.value());
}

/// @brief 把 int32 显示成十进制。
/// @param value 当前标量。空或种类不是 int32 时不用别的种类顶替。
/// @return RNC_refresh 的固件原值。没有值时为「—」。
QString rawIntText(const ernc::domain::ScalarValue* value) {
    if (value == nullptr) {
        return kBlank.toString();
    }
    const ernc::domain::Result<std::int32_t> number = value->asInt32();
    if (!number.ok()) {
        return kBlank.toString();
    }
    return QString::number(number.value());
}

/// @brief 把「开」或「关」换成开关位置。
/// @param word 用户选择的文字。调用方拥有原件，本函数只比较它。
/// @return 开为 On，关为 Off。其它文字为 Unknown，不能编码。
ernc::domain::TogglePosition positionOf(const QString& word) {
    if (word == QStringLiteral("开")) {
        return ernc::domain::TogglePosition::On;
    }
    if (word == QStringLiteral("关")) {
        return ernc::domain::TogglePosition::Off;
    }
    return ernc::domain::TogglePosition::Unknown;
}

}  // namespace

ErncFlagPage::ErncFlagPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                           const ernc::domain::FirmwareLayout* layout, QWidget* parent)
    : QWidget(parent), catalog_{catalog}, store_{store} {
    setObjectName(QStringLiteral("erncFlagPage"));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    if (catalog_ != nullptr && layout != nullptr && store_ != nullptr) {
        stager_.emplace(*catalog_, *layout, *store_);
    }
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(kSpace8);

    for (const char* idText : kFlagIds) {
        const ernc::domain::ParameterDefinition* definition =
            catalog_ == nullptr ? nullptr : catalog_->find(idText);
        auto* row = new QWidget(this);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(kSpace8);
        auto* name = new QPushButton(QString::fromUtf8(idText), row);
        name->setFlat(true);
        name->setStyleSheet(QStringLiteral("text-align: left; padding: 0 4px;"));
        function_view_layout::styleParameterNameButton(name);
        auto* value = new QLabel(row);
        function_view_layout::styleValueLabel(value);
        line->addWidget(name);
        line->addWidget(value);
        function_view_layout::gapBetweenSecondAndThirdColumns(line);
        Control control;
        control.id = QString::fromUtf8(idText);
        control.value = value;
        control.row = row;
        const QString parameterId = control.id;
        connect(name, &QPushButton::clicked, this, [this, parameterId] { activate(parameterId); });

        const bool toggle = definition != nullptr && definition->editor
            && *definition->editor == ernc::domain::EditorKind::Toggle;
        if (toggle) {
            auto* choice = new ParameterToggleSwitch(row);
            line->addWidget(choice);
            control.toggle = choice;
            connect(choice, &ParameterToggleSwitch::userChose, this, [this, parameterId](const QString& word) {
                if (reloading_) {
                    return;
                }
                chooseToggle(parameterId, word);
            });
        } else if (definition != nullptr && definition->dataType
                   && (*definition->dataType == ernc::domain::DataType::UInt32
                       || *definition->dataType == ernc::domain::DataType::Int32)) {
            auto* editor = new QLineEdit(row);
            function_view_layout::styleScalarEditor(editor);
            line->addWidget(editor);
            control.raw = editor;
            connect(editor, &QLineEdit::editingFinished, this, [this, parameterId] {
                if (reloading_) {
                    return;
                }
                const Control* rowControl = findControl(parameterId);
                if (rowControl == nullptr || rowControl->raw == nullptr) {
                    return;
                }
                commitRaw(parameterId, rowControl->raw->text());
            });
        }
        function_view_layout::sealFormRow(line);
        controls_.push_back(control);
        root->addWidget(row);
    }
    root->addStretch(1);
    reload();
}

void ErncFlagPage::setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    editObserver_ = std::move(observer);
}

void ErncFlagPage::setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    selectionObserver_ = std::move(observer);
}

void ErncFlagPage::activate(const QString& parameterId) {
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

QString ErncFlagPage::selectedParameterId() const {
    return selectedId_;
}

void ErncFlagPage::reload() {
    reloading_ = true;
    for (Control& control : controls_) {
        const ernc::domain::ParameterDefinition* definition =
            catalog_ == nullptr ? nullptr : catalog_->find(control.id.toStdString());
        const ernc::domain::ParameterRuntimeState* state = nullptr;
        if (store_ != nullptr && definition != nullptr) {
            state = store_->find(definition->id);
        }
        const ernc::domain::ScalarValue* value = shownValue(state);
        if (control.toggle != nullptr && definition != nullptr) {
            const QString text = toggleText(*definition, value);
            control.value->setText(text);
            control.toggle->setSuppressSignals(true);
            control.toggle->applyCaption(text);
            control.toggle->setSuppressSignals(false);
        } else if (control.raw != nullptr) {
            const ernc::domain::DataType dataType =
                definition != nullptr && definition->dataType ? *definition->dataType : ernc::domain::DataType::UInt32;
            const QString text = dataType == ernc::domain::DataType::Int32 ? rawIntText(value) : rawUintText(value);
            control.value->setText(text);
            control.raw->setText(text == kBlank.toString() ? QString() : text);
        } else if (control.value != nullptr) {
            control.value->setText(kBlank.toString());
        }
    }
    reloading_ = false;
}

QString ErncFlagPage::valueText(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->value == nullptr) {
        return {};
    }
    return control->value->text();
}

bool ErncFlagPage::hasToggle(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control != nullptr && control->toggle != nullptr;
}

int ErncFlagPage::toggleCount(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->toggle == nullptr) {
        return 0;
    }
    return control->toggle->semanticOptionCount();
}

QString ErncFlagPage::toggleItem(const QString& parameterId, int index) const {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->toggle == nullptr) {
        return {};
    }
    return control->toggle->semanticOptionLabel(index);
}

void ErncFlagPage::chooseToggle(const QString& parameterId, const QString& word) {
    if (!stager_ || catalog_ == nullptr) {
        return;
    }
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->toggle == nullptr) {
        return;
    }
    const ernc::domain::TogglePosition position = positionOf(word);
    if (position == ernc::domain::TogglePosition::Unknown) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr) {
        return;
    }
    const ernc::domain::Result<ernc::domain::ScalarValue> encoded = ernc::domain::encodeToggle(*definition, position);
    if (!encoded.ok() || encoded.tryValue() == nullptr) {
        return;
    }
    const ernc::domain::RuntimeTime stagedAt = std::chrono::system_clock::now();
    if (!stager_->stage(definition->id, *encoded.tryValue(), stagedAt).ok()) {
        return;
    }
    notifyEdited(definition->id);
}

void ErncFlagPage::commitRaw(const QString& parameterId, const QString& text) {
    if (!stager_ || catalog_ == nullptr) {
        return;
    }
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->raw == nullptr) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr || !definition->dataType) {
        return;
    }
    const ernc::domain::RuntimeTime stagedAt = std::chrono::system_clock::now();
    if (*definition->dataType == ernc::domain::DataType::UInt32) {
        bool ok = false;
        const uint number = text.trimmed().toUInt(&ok);
        if (!ok) {
            return;
        }
        if (!stager_->stage(definition->id, ernc::domain::ScalarValue::fromUInt32(static_cast<std::uint32_t>(number)),
                            stagedAt)
                 .ok()) {
            return;
        }
    } else if (*definition->dataType == ernc::domain::DataType::Int32) {
        bool ok = false;
        const int number = text.trimmed().toInt(&ok);
        if (!ok) {
            return;
        }
        if (!stager_->stage(definition->id, ernc::domain::ScalarValue::fromInt32(number), stagedAt).ok()) {
            return;
        }
    } else {
        return;
    }
    notifyEdited(definition->id);
}

bool ErncFlagPage::refreshEnabled() const {
    for (const Control& control : controls_) {
        if (control.refresh != nullptr) {
            return control.refresh->isEnabled();
        }
    }
    return false;
}

bool ErncFlagPage::containsParameter(const QString& parameterId) const {
    return findControl(parameterId) != nullptr;
}

void ErncFlagPage::emphasizeParameter(const QString& parameterId) {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->row == nullptr) {
        return;
    }
    if (emphasizedRow_ != nullptr && emphasizedRow_ != control->row) {
        function_view_layout::clearRowEmphasis(emphasizedRow_);
    }
    emphasizedRow_ = control->row;
    function_view_layout::emphasizeParameterRow(control->row, nullptr);
}

void ErncFlagPage::pressRefresh() {}

ErncFlagPage::Control* ErncFlagPage::findControl(const QString& parameterId) {
    for (Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

const ErncFlagPage::Control* ErncFlagPage::findControl(const QString& parameterId) const {
    for (const Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

void ErncFlagPage::notifyEdited(const ernc::domain::ParameterId& id) {
    if (editObserver_) {
        editObserver_(id);
    }
    reload();
}

}  // namespace ernc::presentation
