#pragma once

#include "ernc/domain/analysis/dataset.hpp"

#include <QColor>
#include <QPen>
#include <QPoint>
#include <QPointF>
#include <QString>
#include <QWidget>

class QEvent;
class QHideEvent;
class QShowEvent;
class QWheelEvent;

#include <cstddef>
#include <optional>
#include <vector>

class QTabWidget;
class QToolButton;
class QLabel;
class QwtLegend;
class QwtPlot;
class QwtPlotCurve;
class QwtPlotGrid;
class QwtPlotMagnifier;
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
    ernc::domain::Result<void> setDataset(const ernc::domain::AnalysisDataset& dataset,
                                         std::size_t colorOffset = 0);

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
    /// @return 右键拖动平移使用的 QwtPlotPanner。不转移所有权，控件销毁后失效。
    QwtPlotPanner* panner() const noexcept;

    /// @return 滚轮缩放使用的 QwtPlotMagnifier。不转移所有权，控件销毁后失效。
    QwtPlotMagnifier* magnifier() const noexcept;

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

    /// @brief 把滚轮事件交给 Qwt 缩放器。ScrollArea 内调用以避免滚轮变成页面滚动。
    /// @return 事件已被缩放器消费时为 true。
    bool handleWheelZoom(QWheelEvent* event);

    /// @brief 轴自适应并清空缩放历史，恢复载入数据时的默认视窗。
    /// 不改变曲线数据。须在 UI 主线程调用。
    void resetView();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// 记录曲线载入时的完整绘制样式，用于取消图例选中后无损恢复。
    struct CurvePenStyle {
        QPen pen;
        double z = 0.0;
    };

    void configurePlotNavigation();
    void configurePlotGrid();

    /// @brief 配置图例模式，并在应用事件入口监听本工作区的标签和画布。
    /// 应用级过滤器只处理 legend_ 的后代和本 plot_ 的画布，随本控件销毁自动移除。
    void configureLegendInteraction();

    /// @brief 把当前 Qwt 图例标签设置为可点击模式。
    /// 点击解析由应用级过滤器完成，不依赖会在图例刷新期间失效的 itemInfo 或对象地址映射。
    void syncLegendClickHandlers();

    /// @brief 立即同步一次，并在本轮事件结束后再同步一次 Qwt 延迟创建的标签。
    void scheduleLegendHandlerSync();

    /// @brief 把图例点击排到当前 Qwt 鼠标回调之后执行，避免回调重入。
    /// @param curve 当前曲线中的借用指针；换图后由代际号使排队任务失效。
    void scheduleCurveEmphasis(QwtPlotCurve* curve);
    void resetPlotView();
    void updateResetViewButtonGeometry();
    void updateCurveHintGeometry();
    void createResetViewButton();
    void recordCurveStyles();

    /// @brief 销毁本控件创建的临时光晕曲线。
    /// 光晕不出现在图例中；detach 后由本函数释放。
    void clearEmphasisGlow();

    /// @brief 恢复所有曲线的载入样式并隐藏提示。
    /// 必须在 UI 主线程调用；若样式发生变化则立即重绘。
    void clearCurveEmphasis();

    /// @brief 切换一条仍属于当前工作区曲线的轻量高亮状态。
    /// @param curve 借用指针；不属于 curves_ 时忽略，再次传入已选曲线时恢复原样。
    void emphasizeCurve(QwtPlotCurve* curve);

    /// @brief 为选中曲线重建一层不参与图例和自动缩放的轻光晕。
    /// @param selected 当前曲线中的借用指针。
    /// @param neon 选中曲线使用的不透明强调色。
    void rebuildEmphasisGlow(QwtPlotCurve* selected, const QColor& neon);
    /// 唯一的图表页签。本控件拥有。
    QTabWidget* tabs_ = nullptr;

    /// 页签中的图。由页签拥有。
    QwtPlot* plot_ = nullptr;

    /// 左键矩形缩放。画布拥有。
    QwtPlotZoomer* zoomer_ = nullptr;

    /// 右键平移。画布拥有。
    QwtPlotPanner* panner_ = nullptr;

    /// 滚轮缩放。画布拥有。
    QwtPlotMagnifier* magnifier_ = nullptr;

    /// 十字光标。画布拥有。
    QwtPlotPicker* picker_ = nullptr;

    /// 右侧图例。图拥有。
    QwtLegend* legend_ = nullptr;

    /// 主/次网格。图拥有。
    QwtPlotGrid* grid_ = nullptr;

    /// 图例选中后在画布上方显示的曲线名。父对象为 plot。
    QLabel* curveHintLabel_ = nullptr;

    /// 画布右下角「复位视图」。父对象为 plot 画布。
    QToolButton* resetViewButton_ = nullptr;

    /// 按曲线号排列的显示曲线。图拥有这些对象，本向量只保存指针。
    std::vector<QwtPlotCurve*> curves_;

    /// 与 curves_ 对齐的默认画笔。重画后重建。
    std::vector<CurvePenStyle> curveStyles_;

    /// 曲线集合的代际号。换图时递增，用于丢弃旧图例点击排入的任务。
    std::size_t curveGeneration_ = 0;

    /// 画布左键按下位置。仅用于区分空白点击与拖动缩放。
    std::optional<QPoint> canvasPressPosition_;

    /// 图例当前选中的曲线。不拥有。
    QwtPlotCurve* emphasizedCurve_ = nullptr;

    /// 选中曲线的低透明度轻光晕层。图拥有，当前至多一层。
    std::vector<QwtPlotCurve*> emphasisGlow_;

    /// 原始样本。空表示还没有载入。成功载入后控件拥有这份副本。
    std::optional<ernc::domain::AnalysisDataset> dataset_;
};

}  // namespace ernc::presentation
