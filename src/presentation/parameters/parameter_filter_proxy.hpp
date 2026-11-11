#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QSortFilterProxyModel>
#include <QString>

namespace ernc::presentation {

/// 参数表的搜索、分组和 Dirty 筛选。源模型的行序不被改写。
/// 必须在 UI 主线程使用。不写 Store，也不连接设备。
class ParameterFilterProxy final : public QSortFilterProxyModel {
public:
    /// @brief 绑定与源模型相同的目录和运行状态。
    /// @param catalog 参数目录，只借用。源模型第 i 行必须是 `entries()` 的第 i 条。空指针时不显示行。
    /// @param store 运行状态，只借用。Dirty 判断读它的 `dirty`，不读状态列文字。
    /// @param parent Qt 父对象。非空时由父子关系拥有本代理。必须在 UI 主线程创建。
    explicit ParameterFilterProxy(const ernc::domain::ParameterCatalog* catalog, ernc::domain::ParameterRuntimeStore* store,
                                  QObject* parent = nullptr);

    /// @brief 设置当前项目树页面。
    /// @param pageId 页面 id。`ALL` 或空文本表示不按分类限制。其它文本必须与目录 `category` 完全相同。
    /// @note 不改源模型顺序。只在 UI 主线程调用。
    void setPage(const QString& pageId);

    /// @brief 设置搜索文字。
    /// @param text 用户输入。首尾空白会被去掉。空文本不按文字排除。
    /// @note 匹配参数 id、固件路径或中文说明，大小写不区分。
    void setQuery(const QString& text);

    /// @brief 设置是否只显示有未提交目标的参数。
    /// @param enabled 为 true 时只保留 Store 里 `dirty == true` 的行。
    /// @note 不把状态文字「待写入」当作条件。过期但仍然 dirty 的行会留下。
    void setDirtyOnly(bool enabled);

protected:
    /// @brief 判断源模型的一行是否进入当前视图。
    /// @param sourceRow 源模型行号，与目录顺序一致。
    /// @param sourceParent 只接受无效父索引。
    /// @return 页面、搜索文字和 Dirty 开关都通过时为 true。
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    /// 参数目录。本代理不拥有。
    const ernc::domain::ParameterCatalog* catalog_;

    /// 运行状态。本代理不拥有。
    ernc::domain::ParameterRuntimeStore* store_;

    /// 当前页面 id。空或 `ALL` 表示全部分类。
    QString page_;

    /// 去掉首尾空白后的搜索文字。
    QString query_;

    /// 为 true 时只显示 dirty 行。
    bool dirtyOnly_{false};
};

} // namespace ernc::presentation
