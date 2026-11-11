#pragma once

#include "workspace/editor_selection.hpp"

#include <QModelIndex>
#include <QTabWidget>

class QLabel;

namespace ernc::presentation {

/// Function View 和 Parameter View。两个页面读取同一份参数 id。
/// 切换页签不修改选择，也不连接设备。
class EditorTabs final : public QTabWidget {
public:
    /// @brief 创建两个工作页，初始没有选中的页面或参数。
    /// @param parent 父控件。非空时由 Qt 父子关系拥有本控件。必须在 UI 主线程创建。
    /// @note 启动时停在 Function View。这里不记住上次打开的页签。
    explicit EditorTabs(QWidget* parent = nullptr);

    /// @brief 把参数 id 写进两页共用的选择。
    /// @param parameterId 目录主键。空文本不改变已有选择。调用方拥有原件，本函数只复制它。
    void selectParameter(const QString& parameterId);

    /// @brief 更新当前页面，保留已经选中的参数 id。
    /// @param pageId 项目树页面 id。空文本不改变已有选择。调用方拥有原件，本函数只复制它。
    void selectPage(const QString& pageId);

private:
    /// @brief 按当前选择重写 Function View 上的文字。参数表不在这里改写。
    void refresh();

    /// @brief 参数表换行时，把该行参数 id 写入共用选择。
    /// @param current 新的当前行。无效时不改选择。本函数不清除页面 id。
    /// @param previous 换行前的当前行。本函数不读取它。
    /// @note 只在 UI 主线程由表格选择模型调用。不写设备，也不暂存 Set。
    void onParameterRowChanged(const QModelIndex& current, const QModelIndex& previous);

    /// 两页共用的页面 id 和参数 id。页签切换不会改它。
    EditorSelection selection_;

    /// Function View 上的文字。由本控件通过 Qt 父子关系拥有。
    QLabel* functionLabel_ = nullptr;
};

} // namespace ernc::presentation
