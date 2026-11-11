#include "analysis/ernc_tf_plot.hpp"
#include "analysis/plot_workspace.hpp"

#include "ernc/domain/analysis/dataset.hpp"

#include <QApplication>

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_text.h>

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::AnalysisDataset;
using ernc::domain::DecodedField;
using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::presentation::ErncTfField;
using ernc::presentation::ErncTfPlot;

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
/// @param offset 加到每个下标上的数。实部和虚部用不同偏移区分。
/// @return 下标 i 的值等于 float(i + offset)。
std::vector<float> indexValues(std::size_t count, float offset) {
    std::vector<float> values(count);
    for (std::size_t index = 0; index < count; ++index) {
        values[index] = offset + static_cast<float>(index);
    }
    return values;
}

/// @brief 取出当前唯一曲线。
/// @param plot 曲线页，只在本次调用期间借用。
/// @return 只有一条曲线时指向它。没有或不止一条时为空。
QwtPlotCurve* onlyCurve(ErncTfPlot& plot) {
    if (plot.plotWorkspace() == nullptr || plot.plotWorkspace()->plot() == nullptr) {
        return nullptr;
    }
    const QwtPlotItemList items = plot.plotWorkspace()->plot()->itemList(QwtPlotItem::Rtti_PlotCurve);
    if (items.size() != 1) {
        return nullptr;
    }
    return static_cast<QwtPlotCurve*>(items.first());
}

/// @brief 载入实部和虚部。实部下标值是 i，虚部是 10000 + i。
/// @param plot 页面，只在本次调用期间借用。
/// @return 两份都是 {6, 6, 120} 且载入成功时为 true。
bool loadPair(ErncTfPlot& plot) {
    const Result<AnalysisDataset> real = makeDataset("ERNC_TF.fSecPathTransR", {6, 6, 120}, indexValues(4320, 0.0F));
    const Result<AnalysisDataset> imag = makeDataset("ERNC_TF.fSecPathTransI", {6, 6, 120}, indexValues(4320, 10000.0F));
    return real.ok() && imag.ok() && plot.setFields(real.value(), imag.value()).ok();
}

/// @brief 实部组 (1, 2) 的首点等于存储下标 960。
/// @return 显示 120 点且完整副本仍是 4320 时为 true。
bool realGroupUsesStorageIndex() {
    ErncTfPlot plot;
    if (!loadPair(plot) || !plot.selectGroup(1, 2).ok()) {
        return fail("real_group_uses_storage_index");
    }
    const QwtPlotCurve* curve = onlyCurve(plot);
    const Result<std::size_t> displayed = plot.displayedPointCount();
    const Result<float> stored = plot.valueAt(ErncTfField::Real, 960);
    if (curve == nullptr || curve->data() == nullptr || !displayed.ok() || displayed.value() != 120U || !stored.ok()
        || stored.value() != 960.0F || curve->data()->sample(0).x() != 0.0 || curve->data()->sample(0).y() != 960.0
        || curve->data()->sample(119).x() != 119.0 || curve->data()->sample(119).y() != 1079.0
        || plot.fullSampleCount(ErncTfField::Real) != 4320U || plot.fullSampleCount(ErncTfField::Imag) != 4320U
        || plot.plotWorkspace()->plot()->axisTitle(QwtAxis::XBottom).text() != QStringLiteral("索引")) {
        return fail("real_group_uses_storage_index");
    }
    return true;
}

/// @brief 切到虚部后显示虚部的值，实部副本不变。
/// @return 虚部首点是 10960，实部下标 960 仍是 960 时为 true。
bool imagSwitchKeepsRealSamples() {
    ErncTfPlot plot;
    if (!loadPair(plot) || !plot.selectGroup(1, 2).ok() || !plot.selectField(ErncTfField::Imag).ok()) {
        return fail("imag_switch_keeps_real_samples");
    }
    const QwtPlotCurve* curve = onlyCurve(plot);
    const Result<float> realSample = plot.valueAt(ErncTfField::Real, 960);
    const Result<float> imagSample = plot.valueAt(ErncTfField::Imag, 960);
    if (curve == nullptr || curve->data() == nullptr || curve->data()->sample(0).y() != 10960.0 || !realSample.ok()
        || realSample.value() != 960.0F || !imagSample.ok() || imagSample.value() != 10960.0F
        || plot.fullSampleCount(ErncTfField::Real) != 4320U || plot.displayedPointCount().value() != 120U) {
        return fail("imag_switch_keeps_real_samples");
    }
    return true;
}

/// @brief shape 不符时拒绝，原来的曲线还在。
/// @return 失败后首点仍是 960 时为 true。
bool badShapeKeepsPrevious() {
    ErncTfPlot plot;
    if (!loadPair(plot) || !plot.selectGroup(1, 2).ok()) {
        return fail("bad_shape_keeps_previous");
    }
    const Result<AnalysisDataset> real = makeDataset("ERNC_TF.fSecPathTransR", {6, 6, 119}, indexValues(6U * 6U * 119U, 0.0F));
    const Result<AnalysisDataset> imag = makeDataset("ERNC_TF.fSecPathTransI", {6, 6, 120}, indexValues(4320, 10000.0F));
    if (!real.ok() || !imag.ok()) {
        return fail("bad_shape_keeps_previous");
    }
    const Result<void> rejected = plot.setFields(real.value(), imag.value());
    const QwtPlotCurve* curve = onlyCurve(plot);
    if (rejected.ok() || rejected.tryError() == nullptr || *rejected.tryError() != ErrorCode::InvalidParameter || curve == nullptr
        || curve->data() == nullptr || curve->data()->sample(0).y() != 960.0 || plot.fullSampleCount(ErncTfField::Real) != 4320U) {
        return fail("bad_shape_keeps_previous");
    }
    return true;
}

/// @brief 越界组号不改变当前曲线。
/// @return 组 (6, 0) 被拒绝且首点仍是 960 时为 true。
bool groupOutOfRangeKeepsSelection() {
    ErncTfPlot plot;
    if (!loadPair(plot) || !plot.selectGroup(1, 2).ok()) {
        return fail("group_out_of_range_keeps_selection");
    }
    const Result<void> rejected = plot.selectGroup(6, 0);
    const QwtPlotCurve* curve = onlyCurve(plot);
    if (rejected.ok() || curve == nullptr || curve->data() == nullptr || curve->data()->sample(0).y() != 960.0
        || plot.displayedPointCount().value() != 120U) {
        return fail("group_out_of_range_keeps_selection");
    }
    return true;
}

/// @brief 点读数使用下标，文字里没有 Hz。
/// @return 改点号后曲线首点不变，且读数是「点 7 = 967」时为 true。
bool pointReadoutHasNoHz() {
    ErncTfPlot plot;
    if (!loadPair(plot) || !plot.selectGroup(1, 2).ok() || !plot.selectPoint(7).ok()) {
        return fail("point_readout_has_no_hz");
    }
    const QwtPlotCurve* curve = onlyCurve(plot);
    const QString readout = plot.pointReadout();
    const QString xTitle = plot.plotWorkspace()->plot()->axisTitle(QwtAxis::XBottom).text();
    const QString yTitle = plot.plotWorkspace()->plot()->axisTitle(QwtAxis::YLeft).text();
    if (curve == nullptr || curve->data() == nullptr || curve->data()->sample(0).y() != 960.0 || plot.displayedPointCount().value() != 120U
        || readout != QStringLiteral("点 7 = 967") || readout.contains(QStringLiteral("Hz")) || xTitle.contains(QStringLiteral("Hz"))
        || yTitle.contains(QStringLiteral("Hz")) || plot.selectPoint(120).ok() || plot.pointReadout() != QStringLiteral("点 7 = 967")) {
        return fail("point_readout_has_no_hz");
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。这些用例不读取黄金 BIN。
/// @return 用例通过时为 true。
bool runCase(std::string_view name) {
    if (name == "real_group_uses_storage_index") {
        return realGroupUsesStorageIndex();
    }
    if (name == "imag_switch_keeps_real_samples") {
        return imagSwitchKeepsRealSamples();
    }
    if (name == "bad_shape_keeps_previous") {
        return badShapeKeepsPrevious();
    }
    if (name == "group_out_of_range_keeps_selection") {
        return groupOutOfRangeKeepsSelection();
    }
    if (name == "point_readout_has_no_hz") {
        return pointReadoutHasNoHz();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_tf_plot_tests <case>\n";
        return 2;
    }
    const std::string caseName = argv[1];
    QApplication application(argc, argv);
    return runCase(caseName) ? 0 : 1;
}
