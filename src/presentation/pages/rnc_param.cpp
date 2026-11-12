#include "pages/rnc_param.hpp"

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

/// 没有数值时的占位。不是 0，也不是秒。
constexpr QStringView kBlank = u"—";

/// 多槽参数的入口文字。
constexpr QStringView kArrayMark = u"数组";

/// 只读步长。没有写入口。
constexpr const char* kStepIds[] = {"Step"};

/// 五段 alpha 和 10 分钟前的对应值。都是 1 个 float slot。
constexpr const char* kAlphaIds[] = {
    "RNC_param.alpha1", "RNC_param.alpha1_10", "RNC_param.alpha2", "RNC_param.alpha2_10", "RNC_param.alpha3",
    "RNC_param.alpha3_10", "RNC_param.alpha4", "RNC_param.alpha4_10", "RNC_param.alpha5", "RNC_param.alpha5_10"};

/// 频率分段个数。目录名 forth_fre_num 保持原拼写。单位不是 Hz。
constexpr const char* kFrequencyIds[] = {
    "start_fre_num", "first_fre_num", "second_fre_num", "third_fre_num", "forth_fre_num"};

/// Leaky 数组。不展开元素。
constexpr const char* kLeakyIds[] = {"RNC_param.LeakyRNCFre", "RNC_param.LeakyRNC"};

/// 时间。只使用固件原值，不换算秒。
constexpr const char* kTimeIds[] = {"running_time", "max_time"};

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

/// @brief 把标量格式化成十进制原值。
/// @param value 当前标量。空或种类读不出时为「—」。
/// @return float 的一般格式，或整数的十进制。不附加秒或 Hz。
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

/// @brief 判断目录是否声明为只读。
/// @param definition 参数定义，只借用。
/// @return access 为 read_only 时为 true。Step 属于这一类。
bool isReadOnly(const ernc::domain::ParameterDefinition& definition) {
    return definition.access && *definition.access == ernc::domain::AccessMode::ReadOnly;
}

}  // namespace

RncParamPage::RncParamPage(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                           const ernc::domain::FirmwareLayout* layout, QWidget* parent)
    : QWidget(parent), catalog_{catalog}, store_{store}, layout_{layout} {
    setObjectName(QStringLiteral("erncRncParamPage"));
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

    addSection(content, root, QStringLiteral("步长"), kStepIds, static_cast<int>(std::size(kStepIds)));
    addSection(content, root, QStringLiteral("alpha"), kAlphaIds, static_cast<int>(std::size(kAlphaIds)));
    addSection(content, root, QStringLiteral("频率"), kFrequencyIds, static_cast<int>(std::size(kFrequencyIds)));
    addSection(content, root, QStringLiteral("泄漏"), kLeakyIds, static_cast<int>(std::size(kLeakyIds)));
    {
        auto* heading = new QLabel(QStringLiteral("谱半径"), content);
        root->addWidget(heading);
        addParameterViewOnlyRow(content, root, QStringLiteral("ERNC_TF.SpecRadius"));
    }
    addSection(content, root, QStringLiteral("时间"), kTimeIds, static_cast<int>(std::size(kTimeIds)));
    root->addStretch(1);
    reload();
}

void RncParamPage::addSection(QWidget* parent, QVBoxLayout* layout, const QString& title, const char* const* ids, int count) {
    if (layout == nullptr || ids == nullptr || count <= 0) {
        return;
    }
    auto* heading = new QLabel(title, parent);
    layout->addWidget(heading);
    for (int index = 0; index < count; ++index) {
        addRow(parent, layout, QString::fromUtf8(ids[index]));
    }
}

void RncParamPage::addParameterViewOnlyRow(QWidget* parent, QVBoxLayout* layout, const QString& parameterId) {
    if (catalog_ == nullptr || layout_ == nullptr || layout == nullptr
        || catalog_->find(parameterId.toStdString()) == nullptr) {
        return;
    }
    auto* row = new QWidget(parent);
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(kSpace8);
    auto* name = new QPushButton(parameterId, row);
    name->setFlat(true);
    name->setStyleSheet(QStringLiteral("text-align: left; padding: 0 4px;"));
    function_view_layout::styleParameterNameButton(name);
    auto* value = new QLabel(kArrayMark.toString(), row);
    function_view_layout::styleValueLabel(value);
    auto* hint = new QLabel(function_view_layout::parameterViewHintText(), row);
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    function_view_layout::styleParameterViewHint(hint);
    line->addWidget(name);
    line->addWidget(value);
    function_view_layout::gapBetweenSecondAndThirdColumns(line);
    line->addWidget(hint);
    function_view_layout::sealFormRow(line);
    Control control;
    control.id = parameterId;
    control.value = value;
    control.parameterViewHint = hint;
    control.row = row;
    connect(name, &QPushButton::clicked, this, [this, parameterId] { activate(parameterId); });
    controls_.push_back(control);
    layout->addWidget(row);
}

void RncParamPage::addRow(QWidget* parent, QVBoxLayout* layout, const QString& parameterId) {
    if (catalog_ == nullptr || layout == nullptr) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr) {
        return;
    }
    const bool oneSlot = definition->firmwarePath && isOneScalarSlot(layout_, *definition->firmwarePath);
    const bool editable = oneSlot && !isReadOnly(*definition);
    auto* row = new QWidget(parent);
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(kSpace8);
    auto* name = new QPushButton(parameterId, row);
    name->setFlat(true);
    name->setStyleSheet(QStringLiteral("text-align: left; padding: 0 4px;"));
    function_view_layout::styleParameterNameButton(name);
    auto* value = new QLabel(oneSlot || isReadOnly(*definition) ? kBlank.toString() : kArrayMark.toString(), row);
    function_view_layout::styleValueLabel(value);
    line->addWidget(name);
    line->addWidget(value);
    function_view_layout::gapBetweenSecondAndThirdColumns(line);
    Control control;
    control.id = parameterId;
    control.value = value;
    control.row = row;
    connect(name, &QPushButton::clicked, this, [this, parameterId] { activate(parameterId); });
    if (editable) {
        auto* editor = new QLineEdit(row);
        function_view_layout::styleScalarEditor(editor);
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
    } else if (!oneSlot) {
        auto* hint = new QLabel(function_view_layout::parameterViewHintText(), row);
        hint->setWordWrap(true);
        hint->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        function_view_layout::styleParameterViewHint(hint);
        line->addWidget(hint);
        control.parameterViewHint = hint;
    }
    function_view_layout::sealFormRow(line);
    controls_.push_back(control);
    layout->addWidget(row);
}

void RncParamPage::setEditObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    editObserver_ = std::move(observer);
}

void RncParamPage::setSelectionObserver(std::function<void(const ernc::domain::ParameterId&)> observer) {
    selectionObserver_ = std::move(observer);
}

void RncParamPage::reload() {
    reloading_ = true;
    for (Control& control : controls_) {
        if (catalog_ == nullptr) {
            continue;
        }
        const ernc::domain::ParameterDefinition* definition = catalog_->find(control.id.toStdString());
        if (definition == nullptr || (control.editor == nullptr && control.value->text() == kArrayMark.toString())) {
            continue;
        }
        const ernc::domain::ParameterRuntimeState* state = store_ == nullptr ? nullptr : store_->find(definition->id);
        const QString text = formatScalar(shownValue(state));
        control.value->setText(text);
        if (control.editor != nullptr) {
            control.editor->setText(text == kBlank.toString() ? QString() : text);
        }
    }
    reloading_ = false;
}

bool RncParamPage::containsParameter(const QString& parameterId) const {
    return findControl(parameterId) != nullptr;
}

QString RncParamPage::valueText(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->value == nullptr) {
        return {};
    }
    return control->value->text();
}

QString RncParamPage::parameterViewHintText(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->parameterViewHint == nullptr) {
        return {};
    }
    return control->parameterViewHint->text();
}

QString RncParamPage::detailText(const QString& parameterId) const {
    if (catalog_ == nullptr) {
        return {};
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr || !definition->descriptionZh) {
        return {};
    }
    return QString::fromStdString(*definition->descriptionZh);
}

bool RncParamPage::hasEditor(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control != nullptr && control->editor != nullptr;
}

void RncParamPage::commitValue(const QString& parameterId, const QString& text) {
    if (!stager_ || catalog_ == nullptr) {
        return;
    }
    const Control* control = findControl(parameterId);
    if (control == nullptr || control->editor == nullptr) {
        return;
    }
    const ernc::domain::ParameterDefinition* definition = catalog_->find(parameterId.toStdString());
    if (definition == nullptr || isReadOnly(*definition) || !definition->dataType || !definition->firmwarePath
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
    const ernc::domain::RuntimeTime stagedAt = std::chrono::system_clock::now();
    if (!stager_->stage(definition->id, *value, stagedAt).ok()) {
        return;
    }
    notifyEdited(definition->id);
}

void RncParamPage::activate(const QString& parameterId) {
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

void RncParamPage::emphasizeParameter(const QString& parameterId) {
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

bool RncParamPage::isParameterEmphasized(const QString& parameterId) const {
    const Control* control = findControl(parameterId);
    return control != nullptr && control->row != nullptr && control->row == emphasizedRow_
        && !control->row->styleSheet().isEmpty();
}

QString RncParamPage::selectedParameterId() const {
    return selectedId_;
}

RncParamPage::Control* RncParamPage::findControl(const QString& parameterId) {
    for (Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

const RncParamPage::Control* RncParamPage::findControl(const QString& parameterId) const {
    for (const Control& control : controls_) {
        if (control.id == parameterId) {
            return &control;
        }
    }
    return nullptr;
}

void RncParamPage::notifyEdited(const ernc::domain::ParameterId& id) {
    if (editObserver_) {
        editObserver_(id);
    }
    reload();
}

}  // namespace ernc::presentation
