#include "analysis/check_data_plot.hpp"
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
using ernc::presentation::CheckDataPlot;
using ernc::presentation::CheckDataViewMode;
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

/// @brief 按图例标题找一条曲线。
/// @param plot 一张图，只在本次调用期间借用。空指针时返回空。
/// @param title 图例文字，例如「曲线 1」。
/// @return 找到时指向该曲线。没有时为空。
QwtPlotCurve* curveNamed(PlotWorkspace* plot, const QString& title) {
    if (plot == nullptr || plot->plot() == nullptr) {
        return nullptr;
    }
    for (QwtPlotItem* item : plot->plot()->itemList(QwtPlotItem::Rtti_PlotCurve)) {
        auto* curve = static_cast<QwtPlotCurve*>(item);
        if (curve->title().text() == title) {
            return curve;
        }
    }
    return nullptr;
}

/// @brief 轴标题和页面文字里没有 Hz、dB。
/// @param plot 页面，只在本次调用期间借用。
/// @return 横轴是「索引」且文字里没有这两个单位时为 true。
bool unitsStayBlank(CheckDataPlot& plot) {
    if (plot.plotCount() < 1 || plot.plotAt(0) == nullptr || plot.plotAt(0)->plot() == nullptr) {
        return false;
    }
    const QString xTitle = plot.plotAt(0)->plot()->axisTitle(QwtAxis::XBottom).text();
    const QString yTitle = plot.plotAt(0)->plot()->axisTitle(QwtAxis::YLeft).text();
    const QString chrome = plot.chromeText();
    return xTitle == QStringLiteral("索引") && yTitle == QStringLiteral("值（单位未确认）") && !chrome.contains(QStringLiteral("Hz"))
        && !chrome.contains(QStringLiteral("dB")) && !xTitle.contains(QStringLiteral("Hz")) && !yTitle.contains(QStringLiteral("dB"));
}

/// @brief OutputCheck 叠加时是 6 条 200 点曲线。
/// @return 第 1 条的第一个值等于存储下标 200，完整点数仍是 1200 时为 true。
bool outputCheckOverlayKeeps6By200() {
    const Result<AnalysisDataset> output = makeDataset("check_data.OutputCheck", {6, 200}, indexValues(1200));
    if (!output.ok()) {
        return fail("output_check_overlay_keeps_6_by_200");
    }
    CheckDataPlot plot;
    if (!plot.setField(output.value()).ok() || plot.mode() != CheckDataViewMode::Overlay) {
        return fail("output_check_overlay_keeps_6_by_200");
    }
    const QwtPlotCurve* seriesOne = curveNamed(plot.plotAt(0), QStringLiteral("曲线 1"));
    const Result<std::size_t> displayed = plot.displayedPointCount(0);
    const Result<float> stored = plot.valueAt("check_data.OutputCheck", 200);
    if (plot.plotCount() != 1 || plot.plotAt(0) == nullptr || plot.plotAt(0)->curveCount() != 6U || !displayed.ok()
        || displayed.value() != 200U || seriesOne == nullptr || seriesOne->data() == nullptr || seriesOne->data()->size() != 200U
        || seriesOne->data()->sample(0).x() != 0.0 || seriesOne->data()->sample(0).y() != 200.0 || !stored.ok() || stored.value() != 200.0F
        || plot.fullSampleCount("check_data.OutputCheck") != 1200U || !unitsStayBlank(plot)) {
        return fail("output_check_overlay_keeps_6_by_200");
    }
    return true;
}

/// @brief 同一份 OutputCheck 堆叠成 6 张图。
/// @return 每张 200 点，第 1 张的第一个值仍是下标 200 时为 true。
bool outputCheckStackSplitsSixPlots() {
    const Result<AnalysisDataset> output = makeDataset("check_data.OutputCheck", {6, 200}, indexValues(1200));
    if (!output.ok()) {
        return fail("output_check_stack_splits_six_plots");
    }
    CheckDataPlot plot;
    if (!plot.setField(output.value()).ok() || !plot.setMode(CheckDataViewMode::Stack).ok()) {
        return fail("output_check_stack_splits_six_plots");
    }
    const QwtPlotCurve* second = curveNamed(plot.plotAt(1), QStringLiteral("曲线 0"));
    if (plot.plotCount() != 6 || second == nullptr || second->data() == nullptr || second->data()->sample(0).y() != 200.0
        || plot.fullSampleCount("check_data.OutputCheck") != 1200U || plot.valueAt("check_data.OutputCheck", 200).value() != 200.0F
        || !plot.chromeText().contains(QStringLiteral("组 1")) || !unitsStayBlank(plot) || plot.displayedPointCount(6).ok()) {
        return fail("output_check_stack_splits_six_plots");
    }
    for (int index = 0; index < 6; ++index) {
        const Result<std::size_t> displayed = plot.displayedPointCount(index);
        if (!displayed.ok() || displayed.value() != 200U || plot.plotAt(index) == nullptr || plot.plotAt(index)->curveCount() != 1U) {
            return fail("output_check_stack_splits_six_plots");
        }
    }
    return true;
}

/// @brief 空 shape 的标量在两种模式下都是 1 张图、1 个点。
/// @return 数值保持 1.5 时为 true。
bool scalarFieldIsOnePoint() {
    const Result<AnalysisDataset> scalar = makeDataset("check_data.RNCAdapCheckRefNum", {}, {1.5F});
    if (!scalar.ok()) {
        return fail("scalar_field_is_one_point");
    }
    CheckDataPlot plot;
    if (!plot.setField(scalar.value()).ok()) {
        return fail("scalar_field_is_one_point");
    }
    const QwtPlotCurve* overlay = curveNamed(plot.plotAt(0), QStringLiteral("曲线 0"));
    if (plot.plotCount() != 1 || !plot.displayedPointCount(0).ok() || plot.displayedPointCount(0).value() != 1U || overlay == nullptr
        || overlay->data() == nullptr || overlay->data()->sample(0).y() != 1.5 || plot.fullSampleCount("check_data.RNCAdapCheckRefNum") != 1U
        || plot.valueAt("check_data.RNCAdapCheckRefNum", 0).value() != 1.5F) {
        return fail("scalar_field_is_one_point");
    }
    if (!plot.setMode(CheckDataViewMode::Stack).ok() || plot.plotCount() != 1 || plot.displayedPointCount(0).value() != 1U
        || plot.valueAt("check_data.RNCAdapCheckRefNum", 0).value() != 1.5F || !unitsStayBlank(plot)) {
        return fail("scalar_field_is_one_point");
    }
    return true;
}

/// @brief 切到 250 点字段后，OutputCheck 的 1200 点还在。
/// @return 当前图有 250 个显示点时为 true。
bool fieldSwitchUsesThatFieldsLength() {
    const Result<AnalysisDataset> output = makeDataset("check_data.OutputCheck", {6, 200}, indexValues(1200));
    const Result<AnalysisDataset> horn = makeDataset("check_data.HornRefCheck_F", {250}, indexValues(250));
    if (!output.ok() || !horn.ok()) {
        return fail("field_switch_uses_that_fields_length");
    }
    CheckDataPlot plot;
    if (!plot.setField(output.value()).ok() || !plot.setField(horn.value()).ok() || !plot.selectField("check_data.HornRefCheck_F").ok()) {
        return fail("field_switch_uses_that_fields_length");
    }
    const QwtPlotCurve* curve = curveNamed(plot.plotAt(0), QStringLiteral("曲线 0"));
    if (plot.plotCount() != 1 || plot.plotAt(0)->curveCount() != 1U || plot.displayedPointCount(0).value() != 250U || curve == nullptr
        || curve->data() == nullptr || curve->data()->size() != 250U || curve->data()->sample(249).y() != 249.0
        || plot.fullSampleCount("check_data.OutputCheck") != 1200U || plot.valueAt("check_data.OutputCheck", 200).value() != 200.0F) {
        return fail("field_switch_uses_that_fields_length");
    }
    return true;
}

/// @brief 非 check_data 路径被拒绝，原来的曲线还在。
/// @return 拒绝后第 1 条曲线的第一个值仍是 200 时为 true。
bool badPathKeepsPrevious() {
    const Result<AnalysisDataset> output = makeDataset("check_data.OutputCheck", {6, 200}, indexValues(1200));
    const Result<AnalysisDataset> other = makeDataset("ERNC_TF.SpecRadius", {120}, indexValues(120));
    if (!output.ok() || !other.ok()) {
        return fail("bad_path_keeps_previous");
    }
    CheckDataPlot plot;
    if (!plot.setField(output.value()).ok()) {
        return fail("bad_path_keeps_previous");
    }
    const Result<void> rejected = plot.setField(other.value());
    const QwtPlotCurve* seriesOne = curveNamed(plot.plotAt(0), QStringLiteral("曲线 1"));
    if (rejected.ok() || rejected.tryError() == nullptr || *rejected.tryError() != ErrorCode::InvalidParameter || seriesOne == nullptr
        || seriesOne->data() == nullptr || seriesOne->data()->sample(0).y() != 200.0 || plot.fullSampleCount("check_data.OutputCheck") != 1200U
        || plot.fullSampleCount("ERNC_TF.SpecRadius") != 0U || plot.selectField("check_data.HornRefCheck_F").ok()) {
        return fail("bad_path_keeps_previous");
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。这些用例不读取黄金 BIN。
/// @return 用例通过时为 true。
bool runCase(std::string_view name) {
    if (name == "output_check_overlay_keeps_6_by_200") {
        return outputCheckOverlayKeeps6By200();
    }
    if (name == "output_check_stack_splits_six_plots") {
        return outputCheckStackSplitsSixPlots();
    }
    if (name == "scalar_field_is_one_point") {
        return scalarFieldIsOnePoint();
    }
    if (name == "field_switch_uses_that_fields_length") {
        return fieldSwitchUsesThatFieldsLength();
    }
    if (name == "bad_path_keeps_previous") {
        return badPathKeepsPrevious();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_check_data_plot_tests <case>\n";
        return 2;
    }
    const std::string caseName = argv[1];
    QApplication application(argc, argv);
    return runCase(caseName) ? 0 : 1;
}
