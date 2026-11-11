#include "analysis/plot_workspace.hpp"
#include "analysis/spec_radius_plot.hpp"

#include "ernc/domain/analysis/dataset.hpp"

#include <QApplication>

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_text.h>

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
using ernc::presentation::SpecRadiusPlot;

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

/// @brief 取出当前唯一曲线。
/// @param plot 曲线页，只在本次调用期间借用。
/// @return 只有一条曲线时指向它。没有或不止一条时为空。
QwtPlotCurve* onlyCurve(PlotWorkspace* plot) {
    if (plot == nullptr || plot->plot() == nullptr) {
        return nullptr;
    }
    const QwtPlotItemList items = plot->plot()->itemList(QwtPlotItem::Rtti_PlotCurve);
    if (items.size() != 1) {
        return nullptr;
    }
    return static_cast<QwtPlotCurve*>(items.first());
}

/// @brief 120 个下标保留，清空显示点后 NaN 仍能读到。
/// @return 首尾横坐标和原始值都成立时为 true。
bool indexesSurviveClearingTheCurve() {
    std::vector<float> values = indexValues(120);
    values[3] = std::numeric_limits<float>::quiet_NaN();
    values[4] = std::numeric_limits<float>::infinity();
    const Result<AnalysisDataset> built = makeDataset("ERNC_TF.SpecRadius", {120}, std::move(values));
    if (!built.ok()) {
        return fail("indexes_survive_clearing_the_curve");
    }
    SpecRadiusPlot plot;
    if (!plot.setCurve(built.value()).ok() || plot.sampleCount() != 120U) {
        return fail("indexes_survive_clearing_the_curve");
    }
    QwtPlotCurve* curve = onlyCurve(plot.plotWorkspace());
    if (curve == nullptr || curve->data() == nullptr || curve->data()->size() != 120U || curve->data()->sample(0).x() != 0.0
        || curve->data()->sample(0).y() != 0.0 || curve->data()->sample(119).x() != 119.0 || curve->data()->sample(119).y() != 119.0
        || !std::isnan(curve->data()->sample(3).y())) {
        return fail("indexes_survive_clearing_the_curve");
    }
    curve->setSamples(QVector<QPointF>{});
    const Result<float> nanSample = plot.valueAt(3);
    const Result<float> infSample = plot.valueAt(4);
    if (plot.sampleCount() != 120U || !nanSample.ok() || !std::isnan(nanSample.value()) || !infSample.ok() || !std::isinf(infSample.value())
        || plot.valueAt(119).value() != 119.0F || plot.valueAt(120).ok()) {
        return fail("indexes_survive_clearing_the_curve");
    }
    return true;
}

/// @brief 横轴标题是索引，页面文字里没有 Hz。
/// @return 标题和说明都符合时为 true。
bool axisUsesIndexWithoutHz() {
    const Result<AnalysisDataset> built = makeDataset("ERNC_TF.SpecRadius", {120}, indexValues(120));
    if (!built.ok()) {
        return fail("axis_uses_index_without_hz");
    }
    SpecRadiusPlot plot;
    if (!plot.setCurve(built.value()).ok() || plot.plotWorkspace() == nullptr || plot.plotWorkspace()->plot() == nullptr) {
        return fail("axis_uses_index_without_hz");
    }
    const QString xTitle = plot.plotWorkspace()->plot()->axisTitle(QwtAxis::XBottom).text();
    const QString yTitle = plot.plotWorkspace()->plot()->axisTitle(QwtAxis::YLeft).text();
    const QString chrome = plot.chromeText();
    if (xTitle != QStringLiteral("索引") || yTitle != QStringLiteral("值（单位未确认）") || chrome != QStringLiteral("ERNC_TF.SpecRadius\n索引 0–119")
        || chrome.contains(QStringLiteral("Hz")) || xTitle.contains(QStringLiteral("Hz")) || yTitle.contains(QStringLiteral("Hz"))) {
        return fail("axis_uses_index_without_hz");
    }
    return true;
}

/// @brief 点数或路径不符时拒绝，原来的曲线还在。
/// @return 拒绝后第 0 个点仍是 0 时为 true。
bool rejectedCurveKeepsPrevious() {
    const Result<AnalysisDataset> good = makeDataset("ERNC_TF.SpecRadius", {120}, indexValues(120));
    const Result<AnalysisDataset> shortCurve = makeDataset("ERNC_TF.SpecRadius", {119}, indexValues(119));
    const Result<AnalysisDataset> other = makeDataset("check_data.xPowCheck", {120}, indexValues(120));
    if (!good.ok() || !shortCurve.ok() || !other.ok()) {
        return fail("rejected_curve_keeps_previous");
    }
    SpecRadiusPlot plot;
    if (!plot.setCurve(good.value()).ok()) {
        return fail("rejected_curve_keeps_previous");
    }
    const Result<void> rejectedCount = plot.setCurve(shortCurve.value());
    const Result<void> rejectedPath = plot.setCurve(other.value());
    const QwtPlotCurve* curve = onlyCurve(plot.plotWorkspace());
    if (rejectedCount.ok() || rejectedPath.ok() || rejectedCount.tryError() == nullptr || *rejectedCount.tryError() != ErrorCode::InvalidParameter
        || curve == nullptr || curve->data() == nullptr || curve->data()->sample(0).y() != 0.0 || plot.sampleCount() != 120U
        || plot.valueAt(119).value() != 119.0F) {
        return fail("rejected_curve_keeps_previous");
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。这些用例不读取黄金 BIN，也不使用频率公式。
/// @return 用例通过时为 true。
bool runCase(std::string_view name) {
    if (name == "indexes_survive_clearing_the_curve") {
        return indexesSurviveClearingTheCurve();
    }
    if (name == "axis_uses_index_without_hz") {
        return axisUsesIndexWithoutHz();
    }
    if (name == "rejected_curve_keeps_previous") {
        return rejectedCurveKeepsPrevious();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_spec_radius_plot_tests <case>\n";
        return 2;
    }
    const std::string caseName = argv[1];
    QApplication application(argc, argv);
    return runCase(caseName) ? 0 : 1;
}
