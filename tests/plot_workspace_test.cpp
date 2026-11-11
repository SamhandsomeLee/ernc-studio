#include "analysis/plot_workspace.hpp"

#include "ernc/domain/analysis/dataset.hpp"

#include <QApplication>

#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_layout.h>
#include <qwt_plot_panner.h>
#include <qwt_plot_picker.h>
#include <qwt_plot_zoomer.h>
#include <qwt_text.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::AnalysisDataset;
using ernc::domain::DecodedField;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::presentation::PlotWorkspace;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 组装手写数据集。数值不是设备黄金样本。
/// @param path 字段路径。
/// @param shape 各级长度。
/// @param values 存储顺序的 float。
/// @return 点数和 shape 一致时为成功结果。调用方拥有数据集。
Result<AnalysisDataset> makeDataset(std::string path, std::vector<std::uint32_t> shape, std::vector<float> values) {
    DecodedField field;
    field.path = std::move(path);
    field.shape = std::move(shape);
    field.values = std::move(values);
    return AnalysisDataset::fromDecoded(field);
}

/// @brief 按下标填入可精确表示的整数 float。
/// @param count 个数。
/// @return 下标 i 的值等于 float(i)。
std::vector<float> indexValues(std::size_t count) {
    std::vector<float> values(count);
    for (std::size_t index = 0; index < count; ++index) {
        values[index] = static_cast<float>(index);
    }
    return values;
}

/// @brief 空页签已经装好缩放、中键平移、十字光标和图例。
/// @return 轴标题和工具都符合约定时为 true。
bool chartToolsAreInstalled() {
    PlotWorkspace workspace;
    Qt::MouseButton button = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::ShiftModifier;
    if (workspace.panner() != nullptr) {
        workspace.panner()->getMouseButton(button, modifiers);
    }
    const bool axesOk = workspace.plot() != nullptr
        && workspace.plot()->axisTitle(QwtAxis::XBottom).text() == QStringLiteral("索引")
        && workspace.plot()->axisTitle(QwtAxis::YLeft).text() == QStringLiteral("值（单位未确认）")
        && workspace.plot()->plotLayout() != nullptr
        && workspace.plot()->plotLayout()->legendPosition() == QwtPlot::RightLegend;
    if (workspace.tabCount() != 1 || workspace.tabTitle() != QStringLiteral("未载入") || workspace.curveCount() != 0U
        || workspace.rawSampleCount() != 0U || workspace.rawValue(0).ok() || workspace.zoomer() == nullptr
        || workspace.panner() == nullptr || button != Qt::MiddleButton || modifiers != Qt::NoModifier
        || workspace.picker() == nullptr || workspace.picker()->rubberBand() != QwtPicker::CrossRubberBand
        || workspace.picker()->trackerMode() != QwtPicker::AlwaysOn || workspace.legend() == nullptr
        || workspace.plot()->legend() == nullptr || !axesOk) {
        return fail("chart_tools_are_installed");
    }
    return true;
}

/// @brief 清空 Qwt 显示点后，原始 120 点和 NaN 仍在。
/// @return 显示点数和原始值都符合约定时为 true。
bool rawSamplesSurviveClearingTheCurve() {
    std::vector<float> values = indexValues(120);
    values[3] = std::numeric_limits<float>::quiet_NaN();
    const Result<AnalysisDataset> built = makeDataset("ERNC_TF.SpecRadius", {120}, std::move(values));
    if (!built.ok()) {
        return fail("raw_samples_survive_clearing_the_curve");
    }
    PlotWorkspace workspace;
    const Result<void> loaded = workspace.setDataset(built.value());
    const Result<std::size_t> displayed = loaded.ok() ? workspace.displayedPointCount(0) : Result<std::size_t>::failure(ErrorCode::InvalidParameter);
    const QwtPlotItemList items = workspace.plot()->itemList(QwtPlotItem::Rtti_PlotCurve);
    QwtPlotCurve* curve = items.isEmpty() ? nullptr : static_cast<QwtPlotCurve*>(items.first());
    if (!loaded.ok() || workspace.curveCount() != 1U || !displayed.ok() || displayed.value() != 120U || curve == nullptr
        || curve->data() == nullptr || curve->data()->size() != 120U || curve->data()->sample(0).x() != 0.0
        || curve->data()->sample(119).x() != 119.0 || !std::isnan(curve->data()->sample(3).y())) {
        return fail("raw_samples_survive_clearing_the_curve");
    }
    curve->setSamples(QVector<QPointF>{});
    const Result<float> nanSample = workspace.rawValue(3);
    const Result<float> last = workspace.rawValue(119);
    const Result<std::size_t> afterClear = workspace.displayedPointCount(0);
    if (workspace.rawSampleCount() != 120U || !nanSample.ok() || !std::isnan(nanSample.value()) || !last.ok()
        || last.value() != 119.0F || workspace.rawValue(120).ok() || !afterClear.ok() || afterClear.value() != 0U) {
        return fail("raw_samples_survive_clearing_the_curve");
    }
    return true;
}

/// @brief {2,3} 在同一个页签上画出 2 条曲线。
/// @return 第 1 条曲线的第一个纵坐标等于存储下标 3，且页签数仍是 1 时为 true。
bool twoByThreeKeepsOneTab() {
    const Result<AnalysisDataset> first = makeDataset("first", {1}, {9.0F});
    const Result<AnalysisDataset> grid = makeDataset("grid", {2, 3}, indexValues(6));
    if (!first.ok() || !grid.ok()) {
        return fail("two_by_three_keeps_one_tab");
    }
    PlotWorkspace workspace;
    if (!workspace.setDataset(first.value()).ok() || !workspace.setDataset(grid.value()).ok()) {
        return fail("two_by_three_keeps_one_tab");
    }
    QwtPlotCurve* seriesOne = nullptr;
    for (QwtPlotItem* item : workspace.plot()->itemList(QwtPlotItem::Rtti_PlotCurve)) {
        auto* curve = static_cast<QwtPlotCurve*>(item);
        if (curve->title().text() == QStringLiteral("曲线 1")) {
            seriesOne = curve;
        }
    }
    const QRectF base = workspace.zoomer()->zoomBase();
    const double xMin = std::min(base.left(), base.right());
    const double xMax = std::max(base.left(), base.right());
    const double yMin = std::min(base.top(), base.bottom());
    const double yMax = std::max(base.top(), base.bottom());
    if (workspace.tabCount() != 1 || workspace.tabTitle() != QStringLiteral("grid") || workspace.curveCount() != 2U
        || seriesOne == nullptr || seriesOne->data() == nullptr || seriesOne->data()->size() != 3U
        || seriesOne->data()->sample(0).x() != 0.0 || seriesOne->data()->sample(0).y() != 3.0
        || workspace.rawValue(3).value() != 3.0F || xMin > 0.0 || xMax < 2.0 || yMin > 0.0 || yMax < 5.0) {
        return fail("two_by_three_keeps_one_tab");
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。这些用例不读取 manifest，也不使用设备样本。
/// @return 用例通过时为 true。
bool runCase(std::string_view name) {
    if (name == "chart_tools_are_installed") {
        return chartToolsAreInstalled();
    }
    if (name == "raw_samples_survive_clearing_the_curve") {
        return rawSamplesSurviveClearingTheCurve();
    }
    if (name == "two_by_three_keeps_one_tab") {
        return twoByThreeKeepsOneTab();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_plot_workspace_tests <case>\n";
        return 2;
    }
    const std::string caseName = argv[1];
    QApplication application(argc, argv);
    return runCase(caseName) ? 0 : 1;
}
