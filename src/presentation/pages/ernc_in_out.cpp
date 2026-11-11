#include "pages/ernc_in_out.hpp"

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

/// 本页使用的目录分类。其它分类不生成行。
constexpr const char* kCategory = "ERNC_IN_OUT";

/// 没有数值时的占位。不是 0，也不是通道名。
constexpr QStringView kBlank = u"—";

/// 多槽参数的入口文字。不是 Mic 或 Spk 名称。
constexpr QStringView kArrayMark = u"数组";

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
/// @return locate 得到 1 个标量 slot、4 字节时为 true。数组和找不到的路径为 false。
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

/// @brief 按说明和路径给一行区域名。
/// @param definition 目录定义，只借用。
/// @return 通道数量为「数量」，InputPoi 与 OutputPoi 为「映射」，其余为「增益」。
QString areaName(const ernc::domain::ParameterDefinition& definition) {
    const QString id = QString::fromStdString(definition.id.text());
    if (id == QStringLiteral("InputPoi") || id == QStringLiteral("OutputPoi")) {
        return QStringLiteral("映射");
    }
    if (definition.descriptionZh) {
        const QString description = QString::fromStdString(*definition.descriptionZh);
        if (description.contains(QStringLiteral("通道数")) || description.contains(QStringLiteral("个数"))) {
            return QStringLiteral("数量");
        }
    }
    return QStringLiteral("增益");
}

/// @brief 拼出说明和区域名。
/// @param definition 目录定义，只借用。
/// @return 没有说明时只有区域名。不加入参数 ID，也不生成通道名。
QString detailFor(const ernc::domain::ParameterDefinition& definition) {
    const QString area = areaName(definition);
    if (!definition.descriptionZh || definition.descriptionZh->empty()) {
        return area;
    }
    return QString::fromStdString(*definition.descriptionZh) + QStringLiteral(" ") + area;
}

/// @brief 把 1 slot 标量格式化成十进制。
/// @param value 当前标量。空或种类读不出时为「—」。
/// @return float 的一般格式，或整数的十进制。不附加单位，也不换成通道名。
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

ErncInOutPage::ErncInOutPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                             const ernc::domain::FirmwareLayout* layout, QWidget* parent)
    : QWidget(parent), catalog_{catalog}, store_{store}, layout_{layout} {
    setObjectName(QStringLiteral("erncInOutPage"));
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

    for (const ernc::domain::ParameterDefinition& definition : catalog_->entries()) {
        if (!definition.category || *definition.category != kCategory) {
            continue;
        }
        const bool oneSlot = definition.firmwarePath && isOneScalarSlot(layout_, *definition.firmwarePath);
        auto* row = new QWidget(this);
        auto* line = new QHBoxLayout(row);
        line->setContentsMargins(0, 0, 0, 0);
        line->setSpacing(kSpace8);
        auto* name = new QPushButton(QString::fromStdString(definition.id.text()), row);
        name->setFlat(true);
        name->setStyleSheet(QStringLiteral("text-align: left; padding: 0 4px;"));
        name->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        auto* detail = new QLabel(detailFor(definition), row);
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
        control.id = QString::fromStdString(definition.id.text());
        control.detail = detail;
        control.value = value;
        const QString parameterId = control.id;
        connect(name, &QPushButton::clicked, this, [this, parameterId] { activate(parameterId); });
        if (oneSlot) {
            auto* editor = new QLineEdit(row);
            editor->setMinimumWidth(96);
            editor->setMaximumWidth(220);
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
        root->addWidget(row);
    }
    root->addStretch(1);
    reload();
}

void ErncInOutPage::setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    editObserver_ = std::move(observer);
}

void ErncInOutPage::setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    selectionObserver_ = std::move(observer);
}

void ErncInOutPage::reload() {
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

bool ErncInOutPage::containsParameter(const QString& parameterId) const {
    return findControl(parameterId) != nullptr;
}

QString ErncInOutPage::valueText(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->value == nullptr) {
        return {};
    }
    return control->value->text();
}

QString ErncInOutPage::detailText(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->detail == nullptr) {
        return {};
    }
    return control->detail->text();
}

bool ErncInOutPage::hasEditor(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control != nullptr && control->editor != nullptr;
}

void ErncInOutPage::commitValue(const QString& parameterId, const QString& text) {
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
    if (*definition->dataType == ernc::domain::DataType::Int32) {
        const int number = trimmed.toInt(&ok);
        if (ok) {
            value = ernc::domain::ScalarValue::fromInt32(number);
        }
    } else if (*definition->dataType == ernc::domain::DataType::UInt32) {
        const uint number = trimmed.toUInt(&ok);
        if (ok) {
            value = ernc::domain::ScalarValue::fromUInt32(static_cast<std::uint32_t>(number));
        }
    } else if (*definition->dataType == ernc::domain::DataType::Float32) {
        const double number = trimmed.toDouble(&ok);
        if (ok && std::isfinite(number)) {
            value = ernc::domain::ScalarValue::fromFloat(static_cast<float>(number));
        }
    }
    if (!value) {
        return;
    }
    const ernc::domain::RuntimeTime stagedAt = std::chrono::system_clock::now();
    if (!stager_->stage(definition->id, *value, stagedAt).ok()) {
        return;
    }
    notifyEdited(definition->id);
}

void ErncInOutPage::activate(const QString& parameterId) {
    const Control* control = findControl(parameterId);
    if (control == nullptr || catalog_ == nullptr) {
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

QString ErncInOutPage::selectedParameterId() const {
    return selectedId_;
}

ErncInOutPage::Control* ErncInOutPage::findControl(const QString& parameterId) {
    for (Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

const ErncInOutPage::Control* ErncInOutPage::findControl(const QString& parameterId) const {
    for (const Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

void ErncInOutPage::notifyEdited(const ernc::domain::ParameterId& id) {
    if (editObserver_) {
        editObserver_(id);
    }
    reload();
}

}  // namespace ernc::presentation
