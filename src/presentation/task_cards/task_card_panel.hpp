#pragma once

#include <QWidget>

namespace ernc::presentation {

/// 右侧任务卡。三张卡片一次只展开一张，文字只说明尚未接入。
/// 本控件不读取参数，也不发起读写。
class TaskCardPanel final : public QWidget {
public:
    /// @brief 创建三张可切换的任务卡。初始展开「常用操作」。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本控件。必须在 UI 主线程创建。
    explicit TaskCardPanel(QWidget* parent = nullptr);
};

} // namespace ernc::presentation
