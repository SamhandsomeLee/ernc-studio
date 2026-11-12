#include "pages/ernc_in_out.hpp"
#include "workspace/editor_tabs.hpp"

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QApplication>
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
using ernc::presentation::ErncInOutPage;

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

/// @brief 说明文字是否含有伪造的通道名。
/// @param text 不含参数 ID 的说明和区域名。
/// @return 出现 Mic、Spk 或带序号的通道名时为 true。
bool hasForgedChannelName(const QString& text) {
    return text.contains(QStringLiteral("Mic")) || text.contains(QStringLiteral("Spk"))
        || text.contains(QStringLiteral("麦克风1")) || text.contains(QStringLiteral("扬声器1"));
}

/// @brief 参考通道数确认 6 后暂存 int32。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 说明含「参考信号通道数」，暂存值为 6，设备读数不变时为 true。
bool refernumStagesCount(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId refernum = ParameterId::parse("RNC_param.refernum").value();
    if (!parseId("RNC_param.refernum", "refernum_stages_count", refernum)) {
        return false;
    }
    store.observe(refernum, ScalarValue::fromInt32(2), RuntimeTime{std::chrono::seconds{1}});
    ErncInOutPage page(&catalog, &store, &layout);
    if (page.detailText(QStringLiteral("RNC_param.refernum")) != QStringLiteral("数量"))
        || !page.hasEditor(QStringLiteral("RNC_param.refernum"))
        || page.valueText(QStringLiteral("RNC_param.refernum")) != QStringLiteral("2")) {
        return fail("refernum_stages_count");
    }
    page.commitValue(QStringLiteral("RNC_param.refernum"), QStringLiteral("6"));
    const ParameterRuntimeState* state = store.find(refernum);
    if (state == nullptr || !state->dirty || !state->stagedValue || !state->deviceValue) {
        return fail("refernum_stages_count");
    }
    const Result<std::int32_t> staged = state->stagedValue->asInt32();
    const Result<std::int32_t> device = state->deviceValue->asInt32();
    if (!staged.ok() || staged.value() != 6 || !device.ok() || device.value() != 2
        || page.valueText(QStringLiteral("RNC_param.refernum")) != QStringLiteral("6")) {
        return fail("refernum_stages_count");
    }
    return true;
}

/// @brief 数组入口不造通道名，点击后仍是原参数 ID。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return InputPoi 与 ENC_Mic_Gain 都显示数组，且确认数字不会暂存时为 true。
bool arrayShortcutKeepsParameterId(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ErncInOutPage page(&catalog, &store, &layout);
    const QString input = QStringLiteral("InputPoi");
    const QString gain = QStringLiteral("ENC_Mic_Gain");
    if (page.hasEditor(input) || page.hasEditor(gain) || page.valueText(input) != QStringLiteral("数组")
        || page.valueText(gain) != QStringLiteral("数组") || hasForgedChannelName(page.detailText(input))
        || hasForgedChannelName(page.detailText(gain))) {
        return fail("array_shortcut_keeps_parameter_id");
    }
    page.activate(input);
    if (page.selectedParameterId() != input) {
        return fail("array_shortcut_keeps_parameter_id");
    }
    page.activate(gain);
    if (page.selectedParameterId() != gain) {
        return fail("array_shortcut_keeps_parameter_id");
    }
    page.commitValue(input, QStringLiteral("1"));
    page.commitValue(gain, QStringLiteral("1"));
    ParameterId inputId = ParameterId::parse("InputPoi").value();
    ParameterId gainId = ParameterId::parse("ENC_Mic_Gain").value();
    if (!parseId("InputPoi", "array_shortcut_keeps_parameter_id", inputId)
        || !parseId("ENC_Mic_Gain", "array_shortcut_keeps_parameter_id", gainId)) {
        return false;
    }
    if (store.find(inputId) != nullptr || store.find(gainId) != nullptr) {
        return fail("array_shortcut_keeps_parameter_id");
    }
    return true;
}

/// @brief 1 slot 的输出增益暂存 float 原值。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 确认 -1.5 后显示 -1.5，且数值文字不是扬声器通道名时为 true。
bool outputAmplitudeStagesFloat(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId gain = ParameterId::parse("output_Amplitude").value();
    if (!parseId("output_Amplitude", "output_amplitude_stages_float", gain)) {
        return false;
    }
    ErncInOutPage page(&catalog, &store, &layout);
    if (!page.hasEditor(QStringLiteral("output_Amplitude"))) {
        return fail("output_amplitude_stages_float");
    }
    page.commitValue(QStringLiteral("output_Amplitude"), QStringLiteral("-1.5"));
    const ParameterRuntimeState* state = store.find(gain);
    if (state == nullptr || !state->dirty || !state->stagedValue) {
        return fail("output_amplitude_stages_float");
    }
    const Result<float> staged = state->stagedValue->asFloat();
    const QString shown = page.valueText(QStringLiteral("output_Amplitude"));
    if (!staged.ok() || staged.value() != -1.5F || shown != QStringLiteral("-1.5") || shown.contains(QStringLiteral("扬声器"))) {
        return fail("output_amplitude_stages_float");
    }
    return true;
}

/// @brief 蓝牙增益不属于 ERNC_IN_OUT。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 页面没有 ENC_Mic_Gain_BT 时为 true。
bool bluetoothGainIsAbsent(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ErncInOutPage page(&catalog, &store, &layout);
    if (page.containsParameter(QStringLiteral("ENC_Mic_Gain_BT")) || !page.containsParameter(QStringLiteral("ENC_Mic_Gain"))) {
        return fail("bluetooth_gain_is_absent");
    }
    return true;
}

/// @brief 只有项目树选中 ERNC_IN_OUT 时才显示通道页。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 初始隐藏，选中该页后显示，改到旗标页后重新隐藏时为 true。
bool inOutPageFollowsTree(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EditorTabs tabs(&catalog, &store, &layout);
    QWidget* inOut = tabs.findChild<QWidget*>(QStringLiteral("erncInOutPage"));
    QWidget* flag = tabs.findChild<QWidget*>(QStringLiteral("erncFlagPage"));
    QWidget* summary = tabs.findChild<QWidget*>(QStringLiteral("erncFunctionSummary"));
    if (inOut == nullptr || flag == nullptr || summary == nullptr || !inOut->isHidden() || !flag->isHidden() || summary->isHidden()) {
        return fail("in_out_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("ERNC_IN_OUT"));
    if (inOut->isHidden() || !flag->isHidden() || !summary->isHidden()) {
        return fail("in_out_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("ERNC_Flag"));
    if (!inOut->isHidden() || flag->isHidden() || !summary->isHidden()) {
        return fail("in_out_page_follows_tree");
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
    if (name == "refernum_stages_count") {
        return refernumStagesCount(catalog, layout);
    }
    if (name == "array_shortcut_keeps_parameter_id") {
        return arrayShortcutKeepsParameterId(catalog, layout);
    }
    if (name == "output_amplitude_stages_float") {
        return outputAmplitudeStagesFloat(catalog, layout);
    }
    if (name == "bluetooth_gain_is_absent") {
        return bluetoothGainIsAbsent(catalog, layout);
    }
    if (name == "in_out_page_follows_tree") {
        return inOutPageFollowsTree(catalog, layout);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个 ERNC_IN_OUT 页面用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是 layout manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_in_out_page_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
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
