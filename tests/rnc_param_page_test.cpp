#include "pages/rnc_param.hpp"
#include "workspace/editor_tabs.hpp"

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QApplication>
#include <QLineEdit>
#include <QWidget>

#include <chrono>
#include <iostream>
#include <string_view>

namespace {

using ernc::domain::CatalogLoad;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeState;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::presentation::EditorTabs;
using ernc::presentation::RncParamPage;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 解析参数 ID。
/// @param text 标识文本。
/// @param name 用例名。
/// @param id 成功时写入解析结果。
/// @return 解析成功时为 true。
bool parseId(const char* text, const char* name, ParameterId& id) {
    const Result<ParameterId> parsed = ParameterId::parse(text);
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail(name);
    }
    id = *parsed.tryValue();
    return true;
}

/// @brief Step 只显示，确认不会暂存。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 没有输入框，读数仍是 3，且没有 stagedValue 时为 true。
bool stepHasNoWriter(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId step = ParameterId::parse("Step").value();
    if (!parseId("Step", "step_has_no_writer", step)) {
        return false;
    }
    store.observe(step, ScalarValue::fromFloat(3.0F), RuntimeTime{std::chrono::seconds{1}});
    RncParamPage page(&catalog, &store, &layout);
    if (page.hasEditor(QStringLiteral("Step")) || page.valueText(QStringLiteral("Step")) != QStringLiteral("3")
        || !page.detailText(QStringLiteral("Step")).contains(QStringLiteral("实时步长显示"))) {
        return fail("step_has_no_writer");
    }
    page.commitValue(QStringLiteral("Step"), QStringLiteral("1"));
    const ParameterRuntimeState* state = store.find(step);
    if (state == nullptr || state->dirty || state->stagedValue || !state->deviceValue) {
        return fail("step_has_no_writer");
    }
    const Result<float> device = state->deviceValue->asFloat();
    if (!device.ok() || device.value() != 3.0F) {
        return fail("step_has_no_writer");
    }
    return true;
}

/// @brief running_time 和 max_time 保持固件原值。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return Read 仍显示原值 1100，输入 60 后 Set 为 float 60，未进行单位换算。
bool runningTimeKeepsFirmwareValue(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId running = ParameterId::parse("running_time").value();
    ParameterId maximum = ParameterId::parse("max_time").value();
    if (!parseId("running_time", "running_time_keeps_firmware_value", running)
        || !parseId("max_time", "running_time_keeps_firmware_value", maximum)) {
        return false;
    }
    store.observe(running, ScalarValue::fromFloat(1100.0F), RuntimeTime{std::chrono::seconds{2}});
    store.observe(maximum, ScalarValue::fromFloat(1100.0F), RuntimeTime{std::chrono::seconds{2}});
    RncParamPage page(&catalog, &store, &layout);
    const QString runningText = page.valueText(QStringLiteral("running_time"));
    const QString maximumText = page.valueText(QStringLiteral("max_time"));
    if (!page.hasEditor(QStringLiteral("running_time")) || !page.hasEditor(QStringLiteral("max_time")) || runningText != QStringLiteral("1100")
        || maximumText != QStringLiteral("1100") || runningText.contains(QStringLiteral("秒")) || maximumText.contains(QStringLiteral("秒"))) {
        return fail("running_time_keeps_firmware_value");
    }
    page.commitValue(QStringLiteral("running_time"), QStringLiteral("60"));
    const ParameterRuntimeState* state = store.find(running);
    if (state == nullptr || !state->dirty || !state->stagedValue) {
        return fail("running_time_keeps_firmware_value");
    }
    const Result<float> staged = state->stagedValue->asFloat();
    const auto* editor = page.findChild<QLineEdit*>(QStringLiteral("erncRncSet.running_time"));
    if (!staged.ok() || staged.value() != 60.0F || page.valueText(QStringLiteral("running_time")) != QStringLiteral("1100")
        || editor == nullptr || editor->text() != QStringLiteral("60")) {
        return fail("running_time_keeps_firmware_value");
    }
    return true;
}

/// @brief alpha 可以暂存，Leaky 数组点击后 ID 不变。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return alpha1 暂存为 0.5，LeakyRNC 显示数组且没有 stagedValue 时为 true。
bool alphaStagesAndLeakyKeepsId(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    RncParamPage page(&catalog, &store, &layout);
    const QString alpha = QStringLiteral("RNC_param.alpha1");
    const QString leaky = QStringLiteral("RNC_param.LeakyRNC");
    if (!page.hasEditor(alpha) || page.hasEditor(leaky) || page.valueText(leaky) != QStringLiteral("数组")
        || !page.containsParameter(QStringLiteral("RNC_param.LeakyRNCFre"))
        || !page.hasEditor(QStringLiteral("forth_fre_num"))) {
        return fail("alpha_stages_and_leaky_keeps_id");
    }
    page.commitValue(alpha, QStringLiteral("0.5"));
    page.activate(leaky);
    page.commitValue(leaky, QStringLiteral("1"));
    ParameterId alphaId = ParameterId::parse("RNC_param.alpha1").value();
    ParameterId leakyId = ParameterId::parse("RNC_param.LeakyRNC").value();
    if (!parseId("RNC_param.alpha1", "alpha_stages_and_leaky_keeps_id", alphaId)
        || !parseId("RNC_param.LeakyRNC", "alpha_stages_and_leaky_keeps_id", leakyId)) {
        return false;
    }
    const ParameterRuntimeState* alphaState = store.find(alphaId);
    if (alphaState == nullptr || !alphaState->dirty || !alphaState->stagedValue || store.find(leakyId) != nullptr
        || page.selectedParameterId() != leaky) {
        return fail("alpha_stages_and_leaky_keeps_id");
    }
    const Result<float> staged = alphaState->stagedValue->asFloat();
    if (!staged.ok() || staged.value() != 0.5F) {
        return fail("alpha_stages_and_leaky_keeps_id");
    }
    return true;
}

/// @brief 谱半径在函数视图显示为数组入口，并提示读写在参数视图。
bool specRadiusShowsParameterViewHint(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    RncParamPage page(&catalog, &store, &layout);
    const QString id = QStringLiteral("ERNC_TF.SpecRadius");
    if (!page.containsParameter(id) || page.valueText(id) != QStringLiteral("数组") || page.hasEditor(id)
        || page.parameterViewHintText(id) != QStringLiteral("读写在参数视图")) {
        return fail("spec_radius_shows_parameter_view_hint");
    }
    return true;
}

/// @brief 通道数量等仍不在本页。
bool excludedRowsAreAbsent(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    RncParamPage page(&catalog, &store, &layout);
    if (page.containsParameter(QStringLiteral("RNC_param.refernum"))
        || page.containsParameter(QStringLiteral("RNC_param.eta"))) {
        return fail("excluded_rows_are_absent");
    }
    return true;
}

/// @brief 只有项目树选中 RNC_Param 时才显示本页。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 初始隐藏，选中该页后显示，改到阶次页后重新隐藏时为 true。
bool treeParameterEmphasizesFunctionRow(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EditorTabs tabs(&catalog, &store, &layout);
    tabs.selectPage(QStringLiteral("RNC_Param"));
    tabs.selectParameter(QStringLiteral("RNC_param.alpha1"));
    QWidget* rncWidget = tabs.findChild<QWidget*>(QStringLiteral("erncRncParamPage"));
    if (rncWidget == nullptr || rncWidget->isHidden()) {
        return fail("tree_parameter_emphasizes_function_row");
    }
    const auto* rncPage = static_cast<const RncParamPage*>(rncWidget);
    if (!rncPage->isParameterEmphasized(QStringLiteral("RNC_param.alpha1"))) {
        return fail("tree_parameter_emphasizes_function_row");
    }
    return true;
}

bool rncPageFollowsTree(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EditorTabs tabs(&catalog, &store, &layout);
    QWidget* rnc = tabs.findChild<QWidget*>(QStringLiteral("erncRncParamPage"));
    QWidget* enc = tabs.findChild<QWidget*>(QStringLiteral("erncEncParamPage"));
    QWidget* summary = tabs.findChild<QWidget*>(QStringLiteral("erncFunctionSummary"));
    if (rnc == nullptr || enc == nullptr || summary == nullptr || !rnc->isHidden() || !enc->isHidden() || summary->isHidden()) {
        return fail("rnc_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("RNC_Param"));
    if (rnc->isHidden() || !enc->isHidden() || !summary->isHidden()) {
        return fail("rnc_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("ENC_Param"));
    if (!rnc->isHidden() || enc->isHidden() || !summary->isHidden()) {
        return fail("rnc_page_follows_tree");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const ParameterCatalog& catalog, const FirmwareLayout& layout, bool& recognized) {
    recognized = true;
    if (name == "step_has_no_writer") {
        return stepHasNoWriter(catalog, layout);
    }
    if (name == "running_time_keeps_firmware_value") {
        return runningTimeKeepsFirmwareValue(catalog, layout);
    }
    if (name == "alpha_stages_and_leaky_keeps_id") {
        return alphaStagesAndLeakyKeepsId(catalog, layout);
    }
    if (name == "spec_radius_shows_parameter_view_hint") {
        return specRadiusShowsParameterViewHint(catalog, layout);
    }
    if (name == "excluded_rows_are_absent") {
        return excludedRowsAreAbsent(catalog, layout);
    }
    if (name == "tree_parameter_emphasizes_function_row") {
        return treeParameterEmphasizesFunctionRow(catalog, layout);
    }
    if (name == "rnc_page_follows_tree") {
        return rncPageFollowsTree(catalog, layout);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个 RNC_Param 页面用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是 layout manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_rnc_param_page_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    const CatalogLoad catalog = ParameterCatalogLoader::loadFile(argv[2]);
    const LayoutLoad layout = FirmwareLayoutLoader::loadFile(argv[3]);
    if (!catalog.ok() || !layout.ok()) {
        std::cerr << "failed: load\n";
        return 1;
    }
    bool recognized = false;
    if (!runCase(argv[1], catalog.catalog(), layout.layout(), recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
