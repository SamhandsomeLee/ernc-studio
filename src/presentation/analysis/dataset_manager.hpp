#pragma once

#include "ernc/domain/analysis/dataset.hpp"

#include <QColor>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <cstddef>
#include <filesystem>
#include <vector>

namespace ernc::presentation {

class PlotWorkspace;

/// @brief 命名多份数据集，叠加显示并导出原始数据。
///
/// 必须在 UI 主线程创建和调用。控件拥有每份数据集的独立副本，图只保存降采样后的显示点。
/// 不访问串口，不组帧，也不给频率轴标 Hz。横轴是曲线内的点序号。
/// 颜色在加入时按固定色板分配，移除其他数据集不会改变已有数据集的颜色。
class DatasetManager final : public QWidget {
public:
    /// @brief 建立空的数据集管理器。
    /// @param parent Qt 父对象。为空时调用方负责销毁。非空时随父对象销毁。
    /// 必须在 UI 主线程调用。
    explicit DatasetManager(QWidget* parent = nullptr);

    /// @brief 加入一份命名数据集。
    /// @param name 数据集名。空文本或与已有数据集重名时拒绝，不覆盖。
    /// @param dataset 已构造成功的数据集，只在本次调用期间借用。成功时控件拷贝它。
    /// @return 成功时没有载荷，新数据集默认可见并参与叠加。名字为空或重名时为 InvalidParameter。
    ernc::domain::Result<void> addDataset(const QString& name, const ernc::domain::AnalysisDataset& dataset);

    /// @brief 移除一份数据集。
    /// @param name 数据集名，只在本次调用期间借用。
    /// @return 成功时没有载荷。该名字不存在时为 InvalidParameter，图和已有数据保持不变。
    ernc::domain::Result<void> removeDataset(const QString& name);

    /// @brief 显示或隐藏一份数据集的曲线。
    /// @param name 数据集名，只在本次调用期间借用。
    /// @param visible 是否参与叠加显示。只影响图上的曲线，不删除原始数据。
    /// @return 成功时没有载荷。名字不存在时为 InvalidParameter。
    ernc::domain::Result<void> setVisible(const QString& name, bool visible);

    /// @brief 把一份数据集的原始 float 写成 little-endian 二进制文件。
    ///
    /// 写入的是完整原始样本，绝不经过显示降采样。顺序与 dataset 的存储顺序一致。
    /// @param name 数据集名，只在本次调用期间借用。
    /// @param path 目标文件路径。只写这个文件，不打开别的路径。
    /// @return 名字不存在时为 InvalidParameter。文件打不开或写入失败时为 FileIoFailed。
    /// 平台假设 x86_64 小端，与旧版曲线 BIN 一致；没有文件头。
    ernc::domain::Result<void> exportRaw(const QString& name, const std::filesystem::path& path);

    /// @brief 数据集个数。
    /// @return 加入过的数据集数量。移除会减少它，隐藏不改变它。
    std::size_t datasetCount() const noexcept;

    /// @brief 全部数据集名。
    /// @return 按加入顺序排列。没有数据集时为空列表。
    QStringList datasetNames() const;

    /// @brief 一份数据集现在是否可见。
    /// @param name 数据集名。该名字不存在时返回 false。
    bool isVisible(const QString& name) const noexcept;

    /// @brief 一份数据集的原始样本个数。
    /// @param name 数据集名。该名字不存在时返回 0。不读取图上的曲线。
    std::size_t rawSampleCount(const QString& name) const noexcept;

    /// @brief 一份数据集降采样后的显示点总数。
    /// @param name 数据集名。该名字不存在时返回 0。多曲线数据集是各曲线显示点之和。
    std::size_t displayedPointCount(const QString& name) const noexcept;

    /// @brief 一份数据集的固定颜色。
    /// @param name 数据集名。该名字不存在时返回无效 QColor。
    QColor colorOf(const QString& name) const noexcept;

    /// @brief 当前每根曲线的显示点上限。
    /// @return 超过这个上限的点数会被抽稀。0 表示不降采样。
    std::size_t displayPointLimit() const noexcept;

    /// @brief 设置每根曲线的显示点上限。
    /// @param limit 上限。0 表示不降采样。改后立即重画。
    void setDisplayPointLimit(std::size_t limit);

    /// @brief 借出叠加用的曲线页。
    /// @return 指向内部 PlotWorkspace。不转移所有权，本控件销毁后失效。
    PlotWorkspace* plotWorkspace() const noexcept;

private:
    /// 一份命名数据集及其显示状态。
    struct Entry {
        /// 数据集名。唯一，空文本不会进入列表。
        QString name;

        /// 数据集的独立副本。本控件拥有。
        ernc::domain::AnalysisDataset dataset;

        /// 加入时分配的颜色。重画不变。
        QColor color;

        /// 是否参与叠加显示。隐藏不删除数据。
        bool visible{true};
    };

    /// @brief 按当前可见项重建曲线区。
    /// @return 所有可见数据集的映射都成功时没有载荷。任一失败时保留上一次的图。
    ernc::domain::Result<void> rebuild();

    /// @brief 按名字查找条目。
    /// @param name 数据集名，只在本次调用期间借用。
    /// @return 找到时指向内部条目。没有时为空。
    const Entry* find(const QString& name) const noexcept;

    /// 嵌入的曲线页。本控件拥有。
    PlotWorkspace* workspace_ = nullptr;

    /// 按加入顺序排列的数据集。本控件拥有。
    std::vector<Entry> entries_;

    /// 每根曲线的显示点上限。0 表示不降采样。
    std::size_t displayPointLimit_ = 4096;
};

}  // namespace ernc::presentation
