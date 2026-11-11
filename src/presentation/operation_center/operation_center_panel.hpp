#pragma once

#include "operation_center/operation_log.hpp"

#include <QWidget>

namespace ernc::presentation {

/// 无设备操作中心。显示固定的诊断、停在零的进度和有上限的示例日志。
/// 本控件不连接串口，也不记录真实命令。
class OperationCenterPanel final : public QWidget {
public:
    /// @brief 创建诊断、进度和日志三块。日志先写入三行示例。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本控件。必须在 UI 主线程创建。
    explicit OperationCenterPanel(QWidget* parent = nullptr);

private:
    /// 示例日志的行队列。构造时写入三行，之后不再追加。文本框只复制一次它的文本。
    OperationLog log_;
};

} // namespace ernc::presentation
