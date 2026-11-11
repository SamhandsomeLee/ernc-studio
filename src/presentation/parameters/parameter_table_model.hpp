#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QAbstractTableModel>
#include <QString>
#include <QVector>

namespace ernc::presentation {

/// 参数表模型。行来自目录，格子上的文字在读取时向 RuntimeStore 现取。
/// 模型不保存 deviceValue 或 stagedValue 的第二份副本，也不连接串口。
/// 必须在 UI 主线程创建和调用。
class ParameterTableModel final : public QAbstractTableModel {
public:
    /// @brief 绑定目录和运行状态。
    /// @param catalog 参数目录，只借用。空指针表示没有行。调用方保证它比本模型活得久。
    /// @param store 运行状态，只借用。空指针时各行都显示未读取。调用方保证它比本模型活得久。
    /// @param parent Qt 父对象。非空时由父子关系拥有本模型。必须在 UI 主线程创建。
    explicit ParameterTableModel(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                                 QObject* parent = nullptr);

    /// @brief 指定此后接受哪一次会话的更新通知。
    /// @param session 当前会话代号。none() 表示不接受任何通知。
    /// @note 不修改 Store，也不刷新格子。只在 UI 主线程调用。
    void bindSession(ernc::domain::SessionId session) noexcept;

    /// @brief 通知某一参数的显示可能变了。
    /// @param session 发出通知的会话代号。与当前绑定不一致时丢掉，不读 Store。
    /// @param id 参数标识，只在本次调用期间借用。目录里没有时忽略。
    /// @note 只发出这一行的 dataChanged。不写 Store，不入队设备命令。
    void noteUpdated(ernc::domain::SessionId session, const ernc::domain::ParameterId& id);

    /// @brief 断连后重读全部可见行。
    /// @note 不调用 markOffline。调用方先把 Store 标成过期，再调用本函数。只在 UI 主线程调用。
    void noteDisconnected();

    /// @brief 目录行数。
    /// @param parent 只接受无效索引。有效父索引表示树子节点，本表没有，因此返回 0。
    /// @return 目录参数个数。没有目录时为 0。
    int rowCount(const QModelIndex& parent = {}) const override;

    /// @brief 列数。
    /// @param parent 只接受无效索引。
    /// @return 状态、参数、路径、Read、Set、更新时间共 6 列。
    int columnCount(const QModelIndex& parent = {}) const override;

    /// @brief 读取一格的显示文字。
    /// @param index 单元格。无效索引返回空。
    /// @param role 只处理 `Qt::DisplayRole`。
    /// @return 从目录和 Store 现取的文字。其他角色返回空。
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /// @brief 读取列标题。
    /// @param section 列号。
    /// @param orientation 只处理水平标题。
    /// @param role 只处理 `Qt::DisplayRole`。
    /// @return 列名。垂直标题和其他角色返回空。
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    /// @brief 单元格是否可编辑。
    /// @param index 单元格。
    /// @return 有效单元格可选、可用。Read 和 Set 都不可编辑。
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    /// 一行已经接受的显示文字。它不是 deviceValue 或 stagedValue 的副本。
    struct RowText {
        /// 状态列。
        QString status;

        /// 参数 id。
        QString parameter;

        /// 固件路径。
        QString path;

        /// Read 列。
        QString read;

        /// Set 列。
        QString set;

        /// 更新时间列。
        QString updatedAt;
    };

    /// @brief 按参数标识找行号。
    /// @param id 参数标识，只在本次调用期间借用。
    /// @return 目录顺序中的行号。没有时为 -1。
    int rowOf(const ernc::domain::ParameterId& id) const;

    /// @brief 按当前 Store 重写一行显示文字。
    /// @param row 目录顺序中的行号。越界时不改。
    void refreshRow(int row);

    /// 各行显示文字。只在会话匹配的通知或断连刷新时重写。
    QVector<RowText> rows_;

    /// 参数目录。本模型不拥有。空表示没有行。
    const ernc::domain::ParameterCatalog* catalog_;

    /// 运行状态。本模型不拥有。空时不读取数值。
    ernc::domain::ParameterRuntimeStore* store_;

    /// 当前接受的会话代号。新建时为 none()。
    ernc::domain::SessionId session_{ernc::domain::SessionId::none()};
};

} // namespace ernc::presentation
