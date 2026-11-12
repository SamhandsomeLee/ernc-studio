#pragma once

#include <QWidget>

namespace ernc::presentation {

/// 右侧任务卡：常用批量读写入口（不含区域 BIN/传函传输）。
class TaskCardPanel final : public QWidget {
    Q_OBJECT
public:
    /// @brief 创建常用操作卡片并收起其它分组。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本控件。必须在 UI 主线程创建。
    explicit TaskCardPanel(QWidget* parent = nullptr);

signals:
    void readCurrentPageRequested();
    void writeModifiedPageRequested();
    void readAllScalarsRequested();
    void writeAllDirtyRequested();
};

}  // namespace ernc::presentation
