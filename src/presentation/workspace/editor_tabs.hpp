#pragma once

#include "workspace/editor_selection.hpp"

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
    /// @brief 按当前选择重写两个页面上的同一段文字。
    void refresh();

    /// 两页共用的页面 id 和参数 id。页签切换不会改它。
    EditorSelection selection_;

    /// Function View 上的文字。由本控件通过 Qt 父子关系拥有。
    QLabel* functionLabel_ = nullptr;

    /// Parameter View 上的文字。内容与 `functionLabel_` 相同。
    QLabel* parameterLabel_ = nullptr;
};

} // namespace ernc::presentation
