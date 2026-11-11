#include "pages/enc_param.hpp"

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

/// 没有数值时的占位。不是 0，也不是开。
constexpr QStringView kBlank = u"—";

/// 多槽参数的入口文字。
constexpr QStringView kArrayMark = u"数组";

/// 一张阶次卡片上的字段。顺序在三张卡片里相同。
struct OrderCard {
    /// 卡片标题。不是参数 ID。
    const char* title;

    /// 开闭标志的目录 ID。
    const char* flagId;

    /// Amp、Step、Leaky、误差权重和安全值。数组与 1 slot 都列在这里。
    const char* fields[8];
};

/// 阶次 1、2、3。没有阶次 4、5。
constexpr OrderCard kCards[] = {
    {"阶次1", "enc_order.OrderOne_Flag",
        {"ENC_param.TachofAmp1", "ENC_param.Amp1", "ENC_param.TachofStep1", "ENC_param.Step1", "ENC_param.TachofLeaky1",
            "ENC_param.Leaky1", "ENC_param.ErrWei1", "ENC_param.Delta1"}},
    {"阶次2", "enc_order.OrderTwo_Flag",
        {"ENC_param.TachofAmp2", "ENC_param.Amp2", "ENC_param.TachofStep2", "ENC_param.Step2", "ENC_param.TachofLeaky2",
            "ENC_param.Leaky2", "ENC_param.ErrWei2", "ENC_param.Delta2"}},
    {"阶次3", "enc_order.OrderThree_Flag",
        {"ENC_param.TachofAmp3", "ENC_param.Amp3", "ENC_param.TachofStep3", "ENC_param.Step3", "ENC_param.TachofLeaky3",
            "ENC_param.Leaky3", "ENC_param.ErrWei3", "ENC_param.Delta3"}},
};

/// 三张卡片以外、仍然属于 ENC_Param 的 1 slot 参数。
constexpr const char* kTailIds[] = {
    "ENC_param.OrderOne", "ENC_param.OrderTwo", "ENC_param.OrderThree", "ENC_param.MaxEngineSpeed", "ENC_param.MinEngineSpeed"};

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

/// @brief 判断这条路径是不是可以暂存的 1 slot 标量。
/// @param layout 固件布局。空指针时不能暂存。
/// @param firmwarePath 目录中的固件路径，只在本次调用期间借用。
/// @return locate 得到 1 个标量 slot、4 字节时为 true。
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

/// @brief 把 1 slot 标量格式化成十进制。
/// @param value 当前标量。空或种类读不出时为「—」。
/// @return float 的一般格式。不显示成开或关。
QString formatScalar(const ernc::domain::ScalarValue* value) {
    if (value == nullptr) {
        return kBlank.toString();
    }
    if (const ernc::domain::Result<float> number = value->asFloat(); number.ok()) {
        return QString::number(static_cast<double>(number.value()), 'g', 9);
    }
    return kBlank.toString();
}

}  // namespace

EncParamPage::EncParamPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                           const ernc::domain::FirmwareLayout* layout, QWidget* parent)
    : QWidget(parent), catalog_{catalog}, store_{store}, layout_{layout} {
    setObjectName(QStringLiteral("erncEncParamPage"));
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
    root->setSpacing(kSpace8);
    scroll->setWidget(content);
    pageLayout->addWidget(scroll);
    if (catalog_ == nullptr) {
        return;
    }

    for (const OrderCard& card : kCards) {
        if (catalog_->find(card.flagId) == nullptr) {
            continue;
        }
        auto* title = new QLabel(QString::fromUtf8(card.title), content);
        root->addWidget(title);
        addRow(content, root, QString::fromUtf8(card.flagId));
        for (const char* field : card.fields) {
            addRow(content, root, QString::fromUtf8(field));
        }
        ++cardCount_;
    }
    for (const char* idText : kTailIds) {
        addRow(content, root, QString::fromUtf8(idText));
    }
    root->addStretch(1);
    reload();
}

void EncParamPage::addRow(QWidget* parent, QVBoxLayout* layout, const QString& parameterId) {
    if (catalog_ == nullptr || layout == nullptr) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr) {
        return;
    }
    const bool oneSlot = definition->firmwarePath && isOneScalarSlot(layout_, *definition->firmwarePath);
    auto* row = new QWidget(parent);
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(kSpace8);
    auto* name = new QPushButton(parameterId, row);
    name->setFlat(true);
    name->setStyleSheet(QStringLiteral("text-align: left; padding: 0 4px;"));
    name->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    const QString description = definition->descriptionZh ? QString::fromStdString(*definition->descriptionZh) : QString();
    auto* detail = new QLabel(description, row);
    detail->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    detail->setMinimumWidth(0);
    detail->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* value = new QLabel(oneSlot ? kBlank.toString() : kArrayMark.toString(), row);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setMinimumWidth(48);
    line->addWidget(name);
    line->addWidget(detail, 1);
    line->addWidget(value);
    Control control;
    control.id = parameterId;
    control.detail = detail;
    control.value = value;
    connect(name, &QPushButton::clicked, this, [this, parameterId] { activate(parameterId); });
    if (oneSlot) {
        auto* editor = new QLineEdit(row);
        editor->setMinimumWidth(72);
        editor->setMaximumWidth(160);
        editor->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        line->addWidget(editor);
        control.editor = editor;
        connect(editor, &QLineEdit::editingFinished, this, [this, parameterId] {
            if (reloading_) {
                return;
            }
            const Control* rowControl = findControl(parameterId);
            if (rowControl == nullptr || rowControl->editor == nullptr) {
                return;
            }
            commitValue(parameterId, rowControl->editor->text());
        });
    } else {
        auto* shortcut = new QPushButton(QStringLiteral("定位"), row);
        shortcut->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        line->addWidget(shortcut);
        connect(shortcut, &QPushButton::clicked, this, [this, parameterId] { activate(parameterId); });
    }
    controls_.push_back(control);
    layout->addWidget(row);
}

void EncParamPage::setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    editObserver_ = std::move(observer);
}

void EncParamPage::setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    selectionObserver_ = std::move(observer);
}

void EncParamPage::reload() {
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

int EncParamPage::orderCardCount() const {
    return cardCount_;
}

bool EncParamPage::containsParameter(const QString& parameterId) const {
    return findControl(parameterId) != nullptr;
}

QString EncParamPage::valueText(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->value == nullptr) {
        return {};
    }
    return control->value->text();
}

QString EncParamPage::detailText(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->detail == nullptr) {
        return {};
    }
    return control->detail->text();
}

bool EncParamPage::hasEditor(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control != nullptr && control->editor != nullptr;
}

void EncParamPage::commitValue(const QString& parameterId, const QString& text) {
    if (!stager_ || catalog_ == nullptr) {
        return;
    }
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->editor == nullptr) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr || !definition->dataType || *definition->dataType != ernc::domain::DataType::Float32
        || !definition->firmwarePath || !isOneScalarSlot(layout_, *definition->firmwarePath)) {
        return;
    }
    bool ok = false;
    const double number = text.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(number)) {
        return;
    }
    const ernc::domain::RuntimeTime stagedAt = std::chrono::system_clock::now();
    if (!stager_->stage(definition->id, ernc::domain::ScalarValue::fromFloat(static_cast<float>(number)), stagedAt).ok()) {
        return;
    }
    notifyEdited(definition->id);
}

void EncParamPage::activate(const QString& parameterId) {
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

QString EncParamPage::selectedParameterId() const {
    return selectedId_;
}

EncParamPage::Control* EncParamPage::findControl(const QString& parameterId) {
    for (Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

const EncParamPage::Control* EncParamPage::findControl(const QString& parameterId) const {
    for (const Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

void EncParamPage::notifyEdited(const ernc::domain::ParameterId& id) {
    if (editObserver_) {
        editObserver_(id);
    }
    reload();
}

}  // namespace ernc::presentation
