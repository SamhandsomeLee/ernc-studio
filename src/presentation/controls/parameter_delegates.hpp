#pragma once

#include "ernc/application/stage_value.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/control_semantics.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QStyledItemDelegate>

#include <optional>

class QWidget;

namespace ernc::presentation {

class ParameterTableModel;

/// Set 列的编辑委托。按目录 editor 决定显示和控件。
/// paint 使用基类绘制，不调用 stage，也不入队设备命令。
/// 必须在 UI 主线程使用。
class ParameterDelegate final : public QStyledItemDelegate {
public:
    /// @brief 绑定目录、布局、运行状态。布局齐全时才能暂存。
    /// @param catalog 参数目录，只借用。源模型第 i 行必须是 entries() 的第 i 条。
    /// @param layout 固件布局，只借用。空指针时编辑确认不会暂存。
    /// @param store 运行状态，只借用。空指针时编辑确认不会暂存。
    /// @param parent Qt 父对象。非空时由父子关系拥有本委托。必须在 UI 主线程创建。
    explicit ParameterDelegate(const ernc::domain::ParameterCatalog* catalog, const ernc::domain::FirmwareLayout* layout,
                               ernc::domain::ParameterRuntimeStore* store, QObject* parent = nullptr);

    /// @brief Set 列只展示实际存在的用户目标值。
    /// @param definition 目录中的一条参数。只借用。
    /// @param state 运行状态。空表示还没有记录。
    /// @return toggle 为「开」「关」或「未知」。其它类型是固件原值，没有 Set 时为「—」。
    /// running_time 和 max_time 不换算成秒。
    QString setCaption(const ernc::domain::ParameterDefinition& definition,
                       const ernc::domain::ParameterRuntimeState* state) const;

    /// @brief Read 列展示文字。Toggle 仅依据 displayedRead，不含 Set 覆盖。
    /// @param definition 当前目录项，只在本次调用借用。
    /// @param state 运行状态，只在本次调用借用。空时显示没有读数。
    QString readCaption(const ernc::domain::ParameterDefinition& definition,
                        const ernc::domain::ParameterRuntimeState* state) const;

    /// @brief 为可编辑的 Set 格创建控件。
    /// @param parent 编辑器的父控件。调用方拥有，Qt 在编辑结束时销毁编辑器。
    /// @param option 视图给出的样式。本函数不读取它来决定控件种类。
    /// @param index 单元格。代理索引会先映射回源模型。
    /// @return Toggle 在表格内直接绘制滑动开关，不创建编辑器。numeric 返回单行输入。
    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    /// @brief 把当前 Set 文字放进编辑器。
    /// @param editor createEditor 返回的控件。只借用。
    /// @param index 单元格。
    void setEditorData(QWidget* editor, const QModelIndex& index) const override;

    /// @brief 用户确认后把选择暂存进 Store。
    /// @param editor 编辑器。只借用。
    /// @param model 视图模型。代理会映射到参数表模型后刷新该行。
    /// @param index 单元格。
    /// @note 只调用 stage。不入队 FC16。Unknown 和非法数字不改 Store。
    void setModelData(QWidget* editor, QAbstractItemModel* model, const QModelIndex& index) const override;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

    bool editorEvent(QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;

protected:
    /// @brief 把 Set 列的显示文字换成 setCaption 的结果，然后交给基类。
    /// @param option 视图样式。本函数改写它的 text。
    /// @param index 单元格。
    /// @note 不写 Store，也不访问串口。Toggle 格不写 text，只画滑动开关。
    void initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const override;

private:
    bool isToggleSetCell(const QModelIndex& index) const;

    bool isToggleReadCell(const QModelIndex& index) const;

    bool stageToggleChoice(const ernc::domain::ParameterDefinition& definition, const QString& word,
                           QAbstractItemModel* model, const QModelIndex& index) const;

    /// @brief 从单元格找到目录定义。
    /// @param index 源模型或代理模型的索引。
    /// @return 指向目录内部定义。没有时为空。指针不转移所有权。
    const ernc::domain::ParameterDefinition* definitionFor(const QModelIndex& index) const;

    /// @brief 找到这一行的参数表模型。
    /// @param index 单元格。
    /// @return 源模型是参数表时返回它。否则为空。
    ParameterTableModel* tableFor(const QModelIndex& index) const;

    /// 参数目录。本委托不拥有。
    const ernc::domain::ParameterCatalog* catalog_;

    /// 固件布局。本委托不拥有。
    const ernc::domain::FirmwareLayout* layout_;

    /// 运行状态。本委托不拥有。
    ernc::domain::ParameterRuntimeStore* store_;

    /// 暂存入口。目录、布局和 Store 都存在时才构造。
    /// setModelData 是 const 的 Qt 接口，所以这里允许在确认编辑时修改暂存。
    mutable std::optional<ernc::application::ParameterStager> stager_;
};

} // namespace ernc::presentation
