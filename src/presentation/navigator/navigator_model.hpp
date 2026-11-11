#pragma once

#include "ernc/domain/catalog.hpp"
#include "navigator/navigator_pages.hpp"

#include <QAbstractItemModel>

#include <optional>

namespace ernc::presentation {

/// 项目树模型。根下面是固定页面，页面下面是目录参数 id。
/// 它借用自己持有的目录，不保存参数值，也不连接设备。
class NavigatorModel final : public QAbstractItemModel {
public:
    /// 区分页面和参数。两者都用 DisplayRole 显示稳定 id。
    static constexpr int KindRole = Qt::UserRole;

    /// @brief 接管一份目录加载结果并建立投影。
    /// @param catalog 目录加载结果，所有权移入本模型。失败的加载结果不会生成页面。
    /// @param parent Qt 父对象。非空时由父子关系拥有本模型。必须在 UI 主线程创建。
    explicit NavigatorModel(ernc::domain::CatalogLoad catalog, QObject* parent = nullptr);

    /// @brief 投影是否可以显示成九个入口。
    /// @return 目录加载成功且每个参数都能放进固定页面时为 true。
    bool ok() const;

    /// @brief 不能显示树时的原因。
    /// @return 失败文本。成功时为空。文本不是设备状态。
    QString error() const;

    /// @brief 取得页面或参数节点。
    /// @param row 父节点下的行号，从 0 开始。
    /// @param column 只接受 0。
    /// @param parent 父索引。无效索引表示根，根的子节点是页面。
    /// @return 越界或投影失败时返回无效索引。
    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override;

    /// @brief 取得节点的父索引。
    /// @param child 子索引。无效索引没有父节点。
    /// @return 页面的父索引无效。参数的父索引是它所属的页面。
    QModelIndex parent(const QModelIndex& child) const override;

    /// @brief 子节点数量。
    /// @param parent 父索引。无效索引表示根。
    /// @return 根的行数是页面数。页面的行数是它下面的参数数。参数没有子节点。
    int rowCount(const QModelIndex& parent = {}) const override;

    /// @brief 列数。
    /// @param parent 未使用。项目树只有 id 一列。
    /// @return 投影成功时为 1，否则为 0。
    int columnCount(const QModelIndex& parent = {}) const override;

    /// @brief 读取节点显示文本或节点种类。
    /// @param index 有效的页面或参数索引。
    /// @param role `Qt::DisplayRole` 返回稳定 id。`KindRole` 返回“页面”或“参数”。
    /// @return 其他角色返回空。
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    /// @brief 节点是否可选。
    /// @param index 页面或参数索引。
    /// @return 有效索引可选、可用。
    Qt::ItemFlags flags(const QModelIndex& index) const override;

private:
    /// 本模型拥有的目录。投影里的参数指针指向它，因此不能再移动本对象。
    ernc::domain::CatalogLoad catalog_;

    /// 成功时持有九个页面。失败时为空。
    std::optional<NavigatorProjection> projection_;

    /// 投影失败时的说明。成功时为空。
    QString error_;
};

} // namespace ernc::presentation
