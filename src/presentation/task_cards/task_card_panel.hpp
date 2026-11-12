#pragma once

#include <QWidget>

namespace ernc::presentation {

/// 右侧任务卡：当前页和全工程操作入口。只发出用户意图，写入先打开预览。
class TaskCardPanel final : public QWidget {
    Q_OBJECT
public:
    /// @brief 创建当前页与全工程的读取和写入计划入口。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本控件。必须在 UI 主线程创建。
    explicit TaskCardPanel(QWidget* parent = nullptr);

signals:
    /// @brief 用户请求读取编辑区当前页面的参数。
    void readCurrentPageRequested();
    /// @brief 用户请求查看编辑区当前页面的待写参数，尚未发送设备命令。
    void writeCurrentPageRequested();
    /// @brief 用户请求读取目录内全部可读标量。
    void readAllScalarsRequested();
    /// @brief 用户请求查看全工程待写标量，尚未发送设备命令。
    void writeAllDirtyRequested();
};

}  // namespace ernc::presentation
