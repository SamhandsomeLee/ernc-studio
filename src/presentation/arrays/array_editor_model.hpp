#pragma once

#include "ernc/domain/array_state.hpp"

#include <QAbstractTableModel>

namespace ernc::presentation {

/// 数组表格的列。行号就是元素下标。
enum class ArrayEditorColumn : int {
    /// 设备读数。不可编辑。
    Read = 0,

    /// 用户目标。编辑后只调用 setValue。
    Set = 1
};

/// 数组元素的 Read/Set 表格。模型不保存第二份数值，也不连接设备。
/// 必须在 UI 主线程创建和调用。
class ArrayEditorModel final : public QAbstractTableModel {
public:
    /// @brief 绑定一份数组状态。
    /// @param state 数组状态，只借用。空指针时没有行。调用方保证它比本模型活得久。
    /// @param parent Qt 父对象。非空时由父子关系拥有本模型。必须在 UI 主线程创建。
    explicit ArrayEditorModel(ernc::domain::ArrayState* state, QObject* parent = nullptr);

    /// @brief 元素行数。
    /// @param parent 只接受无效索引。有效父索引表示树子节点，本表没有，因此返回 0。
    /// @return 数组元素个数。没有状态时为 0。
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    /// @brief 列数。
    /// @param parent 只接受无效索引。有效父索引返回 0。
    /// @return 固定为 2：Read 和 Set。
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;

    /// @brief 读取一个格子的显示文字。
    /// @param index 行是元素下标，列是 ArrayEditorColumn。
    /// @param role 只回答 DisplayRole。其它角色返回空。
    /// @return 十进制原值。没有值时为「—」。
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /// @brief 列标题。
    /// @param section 列号。
    /// @param orientation 只回答水平标题。
    /// @param role 只回答 DisplayRole。
    /// @return Read 或 Set。
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /// @brief 格子的交互标志。
    /// @param index 行是元素下标，列是 ArrayEditorColumn。
    /// @return Set 列可编辑。Read 列只可选择。无效索引没有任何标志。
    Qt::ItemFlags flags(const QModelIndex& index) const override;

    /// @brief 把 Set 列的文字写入该下标的用户目标。
    /// @param index 必须是 Set 列。Read 列被拒绝。
    /// @param value 十进制文本。种类按数组构造时的种类解析。
    /// @param role 只接受 EditRole。
    /// @return 已调用 setValue 时为 true。解析失败、越界或改 Read 列时为 false，状态不变。
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;

    /// @brief 一次把全部 Set 收回 Read，并重画表格。
    /// @note 调用 ArrayState::reset。不访问设备。只在 UI 主线程调用。
    void resetSets();

private:
    /// 数组状态。本模型不拥有。
    ernc::domain::ArrayState* state_ = nullptr;
};

}  // namespace ernc::presentation
