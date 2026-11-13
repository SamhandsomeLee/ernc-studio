#include "pages/rnc_param.hpp"

#include "pages/function_view_layout.hpp"
#include "parameters/scalar_display.hpp"

#include "theme/theme_tokens.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStyle>
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

/// @brief 为 Read 列保留 112 个逻辑像素，容纳十进制原值并保持与表头对齐。
/// @param label 借用的页内读数标签或表头，不转移所有权。
void styleReadColumn(QLabel* label) {
    label->setFixedWidth(112);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
    root->setContentsMargins(kSpace4, 0, kSpace8, kSpace4);
    root->setSpacing(kSpace4);
    scroll->setWidget(content);
    pageLayout->addWidget(scroll, 1);
    scroll_ = scroll;
    if (catalog_ == nullptr) {
        return;
    }

    auto* columns = new QHBoxLayout();
    columns->setSpacing(kSpace8);
    auto* parameter = new QLabel(QStringLiteral("参数"), content);
    parameter->setMinimumWidth(function_view_layout::kNameColumnWidth);
    parameter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto* read = new QLabel(QStringLiteral("读 Read"), content);
    styleReadColumn(read);
    auto* set = new QLabel(QStringLiteral("设 Set"), content);
    set->setFixedWidth(function_view_layout::kEditorColumnWidth);
    columns->addWidget(parameter);
    columns->addWidget(read);
    function_view_layout::gapBetweenSecondAndThirdColumns(columns);
    columns->addWidget(set);
    function_view_layout::sealFormRow(columns);
    root->addLayout(columns);

    addSection(content, root, QStringLiteral("步长"), kStepIds, static_cast<int>(std::size(kStepIds)));
    addSection(content, root, QStringLiteral("Alpha 系数"), kAlphaIds, static_cast<int>(std::size(kAlphaIds)));
    addSection(content, root, QStringLiteral("频段"), kFrequencyIds, static_cast<int>(std::size(kFrequencyIds)));
    addSection(content, root, QStringLiteral("泄漏数组"), kLeakyIds, static_cast<int>(std::size(kLeakyIds)));
    {
        auto* heading = new QLabel(QStringLiteral("谱半径"), content);
        heading->setObjectName(QStringLiteral("erncFunctionSection"));
        root->addWidget(heading);
        addParameterViewOnlyRow(content, root, QStringLiteral("ERNC_TF.SpecRadius"));
    }
    addSection(content, root, QStringLiteral("时间原值"), kTimeIds, static_cast<int>(std::size(kTimeIds)));
    root->addStretch(1);
    reload();
}

void RncParamPage::addSection(QWidget* parent, QVBoxLayout* layout, const QString& title, const char* const* ids, int count) {
    if (layout == nullptr || ids == nullptr || count <= 0) {
        return;
    }
    auto* heading = new QLabel(title, parent);
    heading->setObjectName(QStringLiteral("erncFunctionSection"));
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
    styleReadColumn(value);
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
    value->setObjectName(QStringLiteral("erncRncRead"));
    styleReadColumn(value);
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
        editor->setObjectName(QStringLiteral("erncRncSet.%1").arg(parameterId));
        editor->setAccessibleName(QStringLiteral("设定 %1").arg(parameterId));
        editor->setPlaceholderText(kBlank.toString());
        editor->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        line->addWidget(editor);
        control.editor = editor;
        connect(editor, &QLineEdit::editingFinished, this, [this, parameterId] {
            if (reloading_) {
                return;
            }
            const Control* rowControl = findControl(parameterId);
            if (rowControl == nullptr || rowControl->editor == nullptr || !rowControl->editor->isModified()) {
                return;
            }
            rowControl->editor->setModified(false);
            commitValue(parameterId, rowControl->editor->text());
            reload();
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
        const auto* read = state != nullptr ? state->displayedRead() : nullptr;
        const bool historical = read != nullptr && (state->projectReadValue || state->stale);
        control.value->setText(QString::fromStdString(displayScalar(read)));
        control.value->setToolTip(read == nullptr ? QStringLiteral("未读取") : state->projectReadValue
            ? QStringLiteral("历史读数") : state->stale ? QStringLiteral("数据已过期") : QStringLiteral("设备读数"));
        if (control.value->property("historical").toBool() != historical) {
            control.value->setProperty("historical", historical);
            control.value->style()->unpolish(control.value);
            control.value->style()->polish(control.value);
        }
        if (control.editor != nullptr && (!control.editor->hasFocus() || !control.editor->isModified())) {
            control.editor->setText(state != nullptr && state->stagedValue
                ? QString::fromStdString(displayScalar(&*state->stagedValue)) : QString());
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
    activate(QString::fromStdString(id.text()));
    reload();
}

}  // namespace ernc::presentation
