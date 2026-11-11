#pragma once

#include <QAbstractTableModel>

namespace ernc::presentation {

/// 固定假数据的参数表模型。它只显示三行文字，不读取设备，也不暂存用户输入。
class ParameterTableModel final : public QAbstractTableModel {
public:
    /// @brief 创建空的模型外壳。数据来自静态假数据表。
    /// @param parent Qt 父对象。非空时由父子关系拥有本模型。必须在 UI 主线程创建。
    explicit ParameterTableModel(QObject* parent = nullptr);

    /// @brief 假数据行数。
    /// @param parent 只接受无效索引。有效父索引表示树子节点，本表没有，因此返回 0。
    /// @return 固定为 3。
    int rowCount(const QModelIndex& parent = {}) const override;

    /// @brief 列数。
    /// @param parent 只接受无效索引。
    /// @return 状态、参数、路径、Read、Set、更新时间共 6 列。
    int columnCount(const QModelIndex& parent = {}) const override;

    /// @brief 读取一格显示文本或列标题以外的数据。
    /// @param index 单元格。无效索引返回空。
    /// @param role 只处理 `Qt::DisplayRole`。
    /// @return 假数据原文。其他角色返回空。
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /// @brief 读取列标题。
    /// @param section 列号。
    /// @param orientation 只处理水平标题。
    /// @param role 只处理 `Qt::DisplayRole`。
    /// @return 列名。垂直标题和其他角色返回空。
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /// @brief 单元格是否可编辑。
    /// @param index 单元格。
    /// @return 有效单元格可选、可用，但任何列都不可编辑。
    Qt::ItemFlags flags(const QModelIndex& index) const override;
};

} // namespace ernc::presentation
