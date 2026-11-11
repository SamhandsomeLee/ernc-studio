#pragma once

#include "ernc/domain/analysis/dataset.hpp"

#include <QColor>
#include <QPointF>
#include <QString>
#include <QWidget>

#include <cstddef>
#include <optional>
#include <vector>

class QTabWidget;
class QwtLegend;
class QwtPlot;
class QwtPlotCurve;
class QwtPlotPanner;
class QwtPlotPicker;
class QwtPlotZoomer;

namespace ernc::presentation {

/// 一条叠加曲线。DatasetManager 用它把多份数据集画到同一张图。
struct OverlayCurve {
    /// 图例标题。单曲线数据集是数据集名，多曲线是「数据集名 · 曲线号」。
    QString title;

    /// 稳定颜色。由 DatasetManager 在加入时分配，重画不变。
    QColor color;

    /// 显示点，已经降采样。x 是曲线内序号，不是 Hz。
    std::vector<QPointF> points;
};

/// @brief 一块 Qwt 曲线页。
///
/// 必须在 UI 主线程创建和调用。控件拥有一份 AnalysisDataset 副本，Qwt 曲线只保存显示点。
/// 不访问串口，不组帧，也不把频率标成 Hz。横轴是曲线内的点序号。
class PlotWorkspace final : public QWidget {
public:
    /// @brief 建立空的图表页。
    /// @param parent Qt 父对象。为空时调用方负责销毁。非空时随父对象销毁。
    /// 必须在 UI 主线程调用。
    explicit PlotWorkspace(QWidget* parent = nullptr);

    /// @brief 用一份数据集替换当前曲线。
    /// @param dataset 已构造成功的数据集，只在本次调用期间借用。成功时控件拷贝它。
    /// @return 成功时没有载荷。映射失败时为 InvalidParameter，原来的数据集和曲线都保持不变。
    /// 成功后仍只有一个页签，标题换成数据集路径。
    ernc::domain::Result<void> setDataset(const ernc::domain::AnalysisDataset& dataset);

    /// @brief 用一组叠加曲线替换当前曲线区。
    ///
    /// 不修改 dataset_，所以 rawSampleCount() / rawValue() 仍反映上一次 setDataset 或为空。
    /// @param curves 曲线列表，只在本次调用期间借用。空列表清空所有曲线。
    /// @param tabTitle 页签标题。空字符串保留原标题。
    /// @return 成功时没有载荷。曲线名或点数没有需要拒绝的条件，颜色无效时 Qwt 用默认笔。
    ernc::domain::Result<void> setOverlayCurves(const std::vector<OverlayCurve>& curves, const QString& tabTitle);

    /// @brief 页签个数。
    /// @return 本切片始终为 1。
    int tabCount() const;

    /// @brief 当前页签标题。
    /// @return 未载入时为「未载入」。载入成功后为数据集路径。
    QString tabTitle() const;

    /// @brief 借出内部的图。
    /// @return 指向页签里的 QwtPlot。不转移所有权，控件销毁后失效。
    QwtPlot* plot() const noexcept;

    /// @brief 借出缩放器。
    /// @return 左键拖矩形使用的 QwtPlotZoomer。不转移所有权，控件销毁后失效。
    QwtPlotZoomer* zoomer() const noexcept;

    /// @brief 借出平移器。
    /// @return 鼠标中键拖动使用的 QwtPlotPanner。不转移所有权，控件销毁后失效。
    QwtPlotPanner* panner() const noexcept;

    /// @brief 借出十字光标。
    /// @return 跟踪鼠标的 QwtPlotPicker。不转移所有权，控件销毁后失效。
    QwtPlotPicker* picker() const noexcept;

    /// @brief 借出图例。
    /// @return 图右侧的 QwtLegend。不转移所有权，控件销毁后失效。
    QwtLegend* legend() const noexcept;

    /// @brief 当前显示的曲线条数。
    /// @return Qwt 上的曲线个数。未载入时为 0。
    std::size_t curveCount() const noexcept;

    /// @brief 一条曲线上的显示点数。
    /// @param series 从 0 开始的曲线号。等于 curveCount() 时越界。
    /// @return 成功时为该 Qwt 曲线的点数。越界时为 InvalidParameter。
    ernc::domain::Result<std::size_t> displayedPointCount(std::size_t series) const;

    /// @brief 原始样本个数。
    /// @return 内部数据集的样本数。未载入时为 0。不读取 Qwt 曲线。
    std::size_t rawSampleCount() const noexcept;

    /// @brief 读取一个原始样本。
    /// @param storageIndex 从 0 开始的存储下标。等于 rawSampleCount() 时越界。
    /// @return 成功时为内部数据集里的 float。未载入或越界时为 InvalidParameter。
    /// NaN 和 Inf 原样返回。不读取 Qwt 曲线。
    ernc::domain::Result<float> rawValue(std::size_t storageIndex) const;

private:
    /// 唯一的图表页签。本控件拥有。
    QTabWidget* tabs_ = nullptr;

    /// 页签中的图。由页签拥有。
    QwtPlot* plot_ = nullptr;

    /// 左键矩形缩放。画布拥有。
    QwtPlotZoomer* zoomer_ = nullptr;

    /// 中键平移。画布拥有。
    QwtPlotPanner* panner_ = nullptr;

    /// 十字光标。画布拥有。
    QwtPlotPicker* picker_ = nullptr;

    /// 右侧图例。图拥有。
    QwtLegend* legend_ = nullptr;

    /// 按曲线号排列的显示曲线。图拥有这些对象，本向量只保存指针。
    std::vector<QwtPlotCurve*> curves_;

    /// 原始样本。空表示还没有载入。成功载入后控件拥有这份副本。
    std::optional<ernc::domain::AnalysisDataset> dataset_;
};

}  // namespace ernc::presentation
