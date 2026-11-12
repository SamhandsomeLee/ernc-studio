#pragma once

#include "ernc/domain/analysis/dataset.hpp"
#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/types.hpp"

#include <QWidget>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>

class QLabel;
class QSplitter;
class QTabWidget;

namespace ernc::application {
class RegionReader;
class RegionWriter;
}  // namespace ernc::application

namespace ernc::presentation {

class CheckDataPlot;
class DatasetManager;
class ErncTfPlot;
class RegionWorkspace;
class SpecRadiusPlot;

/// @brief 区域导入与曲线分析的一体面板。
///
/// 左侧 RegionWorkspace 做 BIN/TXT 导入；右侧按 Tab 显示叠加曲线与专业图。
/// 必须在 UI 主线程创建和调用。不持有 QSerialPort。
class AnalysisPanel final : public QWidget {
public:
    /// @brief 绑定区域目录。读写端口为空时仍可做离线导入。
    /// @param catalog 区域目录，只借用，须比本控件活得久。
    /// @param reader 区域读，只借用，可为空。
    /// @param writer 区域写，只借用，可为空。
    /// @param parent Qt 父对象。
    explicit AnalysisPanel(const ernc::domain::RegionCatalog& catalog, ernc::application::RegionReader* reader,
                           ernc::application::RegionWriter* writer, QWidget* parent = nullptr);

    /// @brief 在批量灌入前清理与某块区域相关的显示缓存。
    /// @param regionPath 父区域或子字段路径。只用于后续专业图切换。
    void prepareIngest(std::string_view regionPath);

    /// @brief 接收一条已解码字段：叠加数据集并尝试刷新专业图。
    /// @param dataset 已构造成功的数据集，只在本次调用期间借用。
    /// @return 专业图拒绝该字段时仍可能已成功写入 DatasetManager。
    ernc::domain::Result<void> ingestField(const ernc::domain::AnalysisDataset& dataset);

    /// @brief 从原始区域字节载入。供 RegionWorkspace 回调和测试使用。
    /// @param region 当前区域定义，只借用。
    /// @param bytes 与 region.size 一致的字节，只借用。
    ernc::domain::Result<void> loadRegionBytes(const ernc::domain::RegionDefinition& region,
                                               std::span<const std::uint8_t> bytes);

    /// @brief 借出区域工作区。
    RegionWorkspace* regionWorkspace() const noexcept;

    /// @brief 把 UI 会话记录器接到区域工作区。
    void setUiActivityRecorder(
        std::function<void(std::string_view action, std::string_view detail, bool success,
                           ernc::domain::ErrorCode error)>
            recorder);

private:
    /// @brief 根据路径激活合适的专业图 Tab。
    /// @param regionPath 父区域路径。
    void focusSpecializedTab(std::string_view regionPath);

    /// @brief 在叠加管理器里更新或追加一份数据集。
    /// @param dataset 数据集，只借用。
    ernc::domain::Result<void> upsertOverlay(const ernc::domain::AnalysisDataset& dataset);

    /// @brief 实部与虚部都已 ingest 时刷新 ERNC_TF 专业图。
    void bindTfPlotIfReady();

    /// 区域目录。本控件不拥有。
    const ernc::domain::RegionCatalog& catalog_;

    /// 导入与传输控件。本控件拥有。
    RegionWorkspace* regionWorkspace_ = nullptr;

    /// 专业图页签。本控件拥有。
    QTabWidget* tabs_ = nullptr;

    /// 叠加曲线管理器。本控件拥有。
    DatasetManager* datasetManager_ = nullptr;

    /// check_data 专业图。本控件拥有。
    CheckDataPlot* checkPlot_ = nullptr;

    /// ERNC_TF 专业图。本控件拥有。
    ErncTfPlot* tfPlot_ = nullptr;

    /// 谱半径专业图。本控件拥有。
    SpecRadiusPlot* specPlot_ = nullptr;

    /// 状态说明。本控件拥有。
    QLabel* status_ = nullptr;

    /// 最近一次批量导入的顶层路径。
    std::string lastRegionPath_;

    /// 等待虚部配对的实部副本。空表示尚未载入实部。
    std::optional<ernc::domain::AnalysisDataset> pendingReal_;

    /// 等待实部配对的虚部副本。空表示尚未载入虚部。
    std::optional<ernc::domain::AnalysisDataset> pendingImag_;

    /// 区域与会话操作记录。空表示不记录。
    std::function<void(std::string_view action, std::string_view detail, bool success,
                       ernc::domain::ErrorCode error)>
        uiActivityRecorder_;
};

}  // namespace ernc::presentation
