#include "analysis/dataset_manager.hpp"
#include "analysis/plot_workspace.hpp"

#include "ernc/domain/analysis/dataset.hpp"

#include <QApplication>
#include <QPen>

#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_text.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::AnalysisDataset;
using ernc::domain::DecodedField;
using ernc::domain::Result;
using ernc::presentation::DatasetManager;
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
/// @param manager 数据集管理器，只在本次调用期间借用。
/// @param title 图例文字。
/// @return 找到时指向该曲线。没有时为空。
QwtPlotCurve* curveNamed(DatasetManager& manager, const QString& title) {
    PlotWorkspace* workspace = manager.plotWorkspace();
    if (workspace == nullptr || workspace->plot() == nullptr) {
        return nullptr;
    }
    for (QwtPlotItem* item : workspace->plot()->itemList(QwtPlotItem::Rtti_PlotCurve)) {
        auto* curve = static_cast<QwtPlotCurve*>(item);
        if (curve->title().text() == title) {
            return curve;
        }
    }
    return nullptr;
}

/// @brief 图上的曲线条数。
/// @param manager 数据集管理器，只在本次调用期间借用。
/// @return 曲线条数。没有图时为 0。
int curveCount(DatasetManager& manager) {
    PlotWorkspace* workspace = manager.plotWorkspace();
    if (workspace == nullptr || workspace->plot() == nullptr) {
        return 0;
    }
    return workspace->plot()->itemList(QwtPlotItem::Rtti_PlotCurve).size();
}

/// @brief 加一份数据集并校验计数和唯一命名。
/// @return 计数、名字列表、重名拒绝、空名拒绝都符合约定时为 true。
bool addAndNameIsUnique() {
    DatasetManager manager;
    const Result<AnalysisDataset> dataset = makeDataset("field.path", {5}, indexValues(5));
    if (!dataset.ok() || !manager.addDataset(QStringLiteral("first"), dataset.value()).ok()) {
        return fail("add_and_name_is_unique");
    }
    if (manager.datasetCount() != 1U || manager.datasetNames() != QStringList{QStringLiteral("first")}
        || manager.rawSampleCount(QStringLiteral("first")) != 5U) {
        return fail("add_and_name_is_unique");
    }
    if (manager.addDataset(QStringLiteral("first"), dataset.value()).ok()) {
        return fail("add_and_name_is_unique");
    }
    if (manager.addDataset(QStringLiteral(""), dataset.value()).ok()) {
        return fail("add_and_name_is_unique");
    }
    return manager.datasetCount() == 1U;
}

/// @brief 切换可见性后，已有数据集的颜色不变。
/// @return 颜色在重画前后一致，且曲线画笔与颜色一致时为 true。
bool stableColorAfterVisibilityToggle() {
    DatasetManager manager;
    const Result<AnalysisDataset> first = makeDataset("first", {5}, indexValues(5));
    const Result<AnalysisDataset> second = makeDataset("second", {5}, indexValues(5));
    if (!first.ok() || !second.ok() || !manager.addDataset(QStringLiteral("first"), first.value()).ok()
        || !manager.addDataset(QStringLiteral("second"), second.value()).ok()) {
        return fail("stable_color_after_visibility_toggle");
    }
    const QColor before = manager.colorOf(QStringLiteral("first"));
    if (!before.isValid()) {
        return fail("stable_color_after_visibility_toggle");
    }
    if (!manager.setVisible(QStringLiteral("second"), false).ok() || !manager.setVisible(QStringLiteral("second"), true).ok()) {
        return fail("stable_color_after_visibility_toggle");
    }
    QwtPlotCurve* curve = curveNamed(manager, QStringLiteral("first"));
    if (manager.colorOf(QStringLiteral("first")) != before || curve == nullptr || curve->pen().color() != before) {
        return fail("stable_color_after_visibility_toggle");
    }
    return true;
}

/// @brief 隐藏和移除只影响显示，不破坏数据。
/// @return 曲线数、数据集数和导出失败都符合约定时为 true。
bool overlayRemoveAndHide() {
    DatasetManager manager;
    const Result<AnalysisDataset> first = makeDataset("first", {5}, indexValues(5));
    const Result<AnalysisDataset> second = makeDataset("second", {5}, indexValues(5));
    if (!first.ok() || !second.ok() || !manager.addDataset(QStringLiteral("first"), first.value()).ok()
        || !manager.addDataset(QStringLiteral("second"), second.value()).ok()) {
        return fail("overlay_remove_and_hide");
    }
    if (curveCount(manager) != 2) {
        return fail("overlay_remove_and_hide");
    }
    if (!manager.setVisible(QStringLiteral("second"), false).ok()) {
        return fail("overlay_remove_and_hide");
    }
    if (curveCount(manager) != 1 || manager.datasetCount() != 2U || manager.rawSampleCount(QStringLiteral("second")) != 5U
        || manager.isVisible(QStringLiteral("second"))) {
        return fail("overlay_remove_and_hide");
    }
    if (!manager.removeDataset(QStringLiteral("second")).ok() || manager.datasetCount() != 1U || curveCount(manager) != 1) {
        return fail("overlay_remove_and_hide");
    }
    const std::filesystem::path out = std::filesystem::temp_directory_path() / "ernc_dm_missing.bin";
    if (manager.exportRaw(QStringLiteral("second"), out).ok() || manager.removeDataset(QStringLiteral("nope")).ok()) {
        return fail("overlay_remove_and_hide");
    }
    return true;
}

/// @brief 显示降采样不改变导出的原始数据。
/// @return 显示点数小于原始，且导出文件与原始逐点一致时为 true。
bool downsampleKeepsRawExport() {
    DatasetManager manager;
    const std::vector<float> values = indexValues(10000);
    const Result<AnalysisDataset> dataset = makeDataset("big", {10000}, values);
    if (!dataset.ok()) {
        return fail("downsample_keeps_raw_export");
    }
    manager.setDisplayPointLimit(1000);
    if (!manager.addDataset(QStringLiteral("big"), dataset.value()).ok()) {
        return fail("downsample_keeps_raw_export");
    }
    const std::size_t raw = manager.rawSampleCount(QStringLiteral("big"));
    const std::size_t displayed = manager.displayedPointCount(QStringLiteral("big"));
    if (raw != 10000U || displayed >= raw) {
        return fail("downsample_keeps_raw_export");
    }
    const std::filesystem::path out = std::filesystem::temp_directory_path() / "ernc_dm_export.bin";
    if (!manager.exportRaw(QStringLiteral("big"), out).ok()) {
        return fail("downsample_keeps_raw_export");
    }
    std::ifstream in(out, std::ios::binary);
    if (!in) {
        return fail("downsample_keeps_raw_export");
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        float value = 0.0F;
        in.read(reinterpret_cast<char*>(&value), sizeof(value));
        if (!in || value != values[index]) {
            return fail("downsample_keeps_raw_export");
        }
    }
    in.close();
    std::filesystem::remove(out);
    return true;
}

/// @brief 轴标题不写 Hz 或 dB。
/// @return 横轴是「索引」、纵轴是「值（单位未确认）」时为 true。
bool unitsStayBlank() {
    DatasetManager manager;
    const Result<AnalysisDataset> dataset = makeDataset("field.path", {5}, indexValues(5));
    if (!dataset.ok() || !manager.addDataset(QStringLiteral("a"), dataset.value()).ok()) {
        return fail("units_stay_blank");
    }
    PlotWorkspace* workspace = manager.plotWorkspace();
    if (workspace == nullptr || workspace->plot() == nullptr) {
        return fail("units_stay_blank");
    }
    const QString xTitle = workspace->plot()->axisTitle(QwtAxis::XBottom).text();
    const QString yTitle = workspace->plot()->axisTitle(QwtAxis::YLeft).text();
    return xTitle == QStringLiteral("索引") && yTitle == QStringLiteral("值（单位未确认）") && !xTitle.contains(QStringLiteral("Hz"))
        && !yTitle.contains(QStringLiteral("dB"));
}

/// @brief 按用例名运行。
/// @param name 用例名。这些用例不读取 manifest，也不使用设备样本。
/// @return 用例通过时为 true。
bool runCase(std::string_view name) {
    if (name == "add_and_name_is_unique") {
        return addAndNameIsUnique();
    }
    if (name == "stable_color_after_visibility_toggle") {
        return stableColorAfterVisibilityToggle();
    }
    if (name == "overlay_remove_and_hide") {
        return overlayRemoveAndHide();
    }
    if (name == "downsample_keeps_raw_export") {
        return downsampleKeepsRawExport();
    }
    if (name == "units_stay_blank") {
        return unitsStayBlank();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_dataset_manager_tests <case>\n";
        return 2;
    }
    const std::string caseName = argv[1];
    QApplication application(argc, argv);
    return runCase(caseName) ? 0 : 1;
}
