#pragma once

#include <QString>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSizePolicy>

namespace ernc::presentation::function_view_layout {

/// 功能视图表单列宽。左对齐成组，右侧留白由行尾 stretch 吸收。
inline constexpr int kNameColumnWidth = 232;
inline constexpr int kKindColumnWidth = 52;
inline constexpr int kValueColumnWidth = 56;
inline constexpr int kEditorColumnWidth = 128;
inline constexpr int kComboColumnWidth = 96;
inline constexpr int kActionButtonWidth = 72;

/// 第二栏（类型或当前值）与第三栏（值或编辑控件）之间的额外留白。
inline constexpr int kColumnTwoThreeGap = 20;

/// 多槽/区域参数在函数视图中的读写提示。标量在本页暂存；数组在参数视图读写。
inline QString parameterViewHintText() {
    return QStringLiteral("读写在参数视图");
}

inline void styleParameterNameButton(QPushButton* name) {
    name->setMinimumWidth(kNameColumnWidth);
    name->setMaximumWidth(kNameColumnWidth);
    name->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

inline void styleParameterIdLabel(QLabel* name) {
    name->setMinimumWidth(kNameColumnWidth);
    name->setMaximumWidth(kNameColumnWidth);
    name->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

inline void styleValueLabel(QLabel* value) {
    value->setMinimumWidth(kValueColumnWidth);
    value->setMaximumWidth(kValueColumnWidth);
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

inline void styleKindLabel(QLabel* kind) {
    kind->setMinimumWidth(kKindColumnWidth);
    kind->setMaximumWidth(kKindColumnWidth);
    kind->setAlignment(Qt::AlignCenter);
    kind->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

inline void styleScalarEditor(QLineEdit* editor) {
    editor->setMinimumWidth(kEditorColumnWidth);
    editor->setMaximumWidth(kEditorColumnWidth);
    editor->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

inline void styleToggleCombo(QComboBox* combo) {
    combo->setMinimumWidth(kComboColumnWidth);
    combo->setMaximumWidth(kComboColumnWidth);
    combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

inline void styleActionButton(QPushButton* button) {
    button->setMinimumWidth(kActionButtonWidth);
    button->setMaximumWidth(kActionButtonWidth);
    button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

/// @brief 加宽第二、第三栏之间的视觉间距（不改变各栏固定列宽）。
inline void gapBetweenSecondAndThirdColumns(QHBoxLayout* line) {
    line->addSpacing(kColumnTwoThreeGap);
}

/// @brief 在行尾吸收多余宽度，避免控件被拉到窗口最右侧。
inline void sealFormRow(QHBoxLayout* line) {
    line->addStretch(1);
}

}  // namespace ernc::presentation::function_view_layout
