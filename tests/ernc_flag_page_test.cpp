#include "pages/ernc_flag.hpp"
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
using ernc::presentation::ErncFlagPage;

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

/// @brief 读出 float 暂存值。
/// @param state 运行状态。空表示没有记录。
/// @param value 成功时写入 float。没有物理单位。
/// @return stagedValue 是 float 时为 true。
bool stagedFloat(const ParameterRuntimeState* state, float& value) {
    if (state == nullptr || !state->stagedValue) {
        return false;
    }
    const Result<float> number = state->stagedValue->asFloat();
    if (!number.ok()) {
        return false;
    }
    value = number.value();
    return true;
}

/// @brief 三个反向开关把关暂存成线值 1，开显示线值 0。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 三项都从开切到关，且暂存 float 为 1、设备读数仍为 0 时为 true。
bool reverseSwitchesStageWireValues(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ErncFlagPage page(&catalog, &store, &layout);
    const char* ids[] = {"ANC_OFF", "ENC_OFF", "RNC_OFF"};
    for (const char* idText : ids) {
        ParameterId id = ParameterId::parse(idText).value();
        if (!parseId(idText, "reverse_switches_stage_wire_values", id)) {
            return false;
        }
        store.observe(id, ScalarValue::fromFloat(0.0F), RuntimeTime{std::chrono::seconds{1}});
    }
    page.reload();
    for (const char* idText : ids) {
        const QString id = QString::fromUtf8(idText);
        if (page.valueText(id) != QStringLiteral("开") || !page.hasToggle(id) || page.toggleCount(id) != 2
            || page.toggleItem(id, 0) != QStringLiteral("开") || page.toggleItem(id, 1) != QStringLiteral("关")) {
            return fail("reverse_switches_stage_wire_values");
        }
        page.chooseToggle(id, QStringLiteral("关"));
        ParameterId parsed = ParameterId::parse(idText).value();
        if (!parseId(idText, "reverse_switches_stage_wire_values", parsed)) {
            return false;
        }
        const ParameterRuntimeState* state = store.find(parsed);
        float staged = 0.0F;
        const Result<float> device = state == nullptr || !state->deviceValue
            ? Result<float>::failure(ernc::domain::ErrorCode::Unknown)
            : state->deviceValue->asFloat();
        if (!stagedFloat(state, staged) || staged != 1.0F || !state->dirty || !device.ok() || device.value() != 0.0F
            || page.valueText(id) != QStringLiteral("关")) {
            return fail("reverse_switches_stage_wire_values");
        }
    }
    return true;
}

/// @brief 对不上的线值显示 Unknown，不能显示成关。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return ANC_OFF 的 2 显示 Unknown，且选择项仍然只有开和关时为 true。
bool unknownToggleIsNotOff(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    if (!parseId("ANC_OFF", "unknown_toggle_is_not_off", anc)) {
        return false;
    }
    store.observe(anc, ScalarValue::fromFloat(2.0F), RuntimeTime{std::chrono::seconds{2}});
    ErncFlagPage page(&catalog, &store, &layout);
    const QString text = page.valueText(QStringLiteral("ANC_OFF"));
    if (text != QStringLiteral("Unknown") || text == QStringLiteral("关") || page.toggleCount(QStringLiteral("ANC_OFF")) != 2
        || page.toggleItem(QStringLiteral("ANC_OFF"), 0) != QStringLiteral("开")
        || page.toggleItem(QStringLiteral("ANC_OFF"), 1) != QStringLiteral("关")) {
        return fail("unknown_toggle_is_not_off");
    }
    return true;
}

/// @brief Speaker 的 1 保持数字，不显示成开。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 没有开/关选择，显示 1，确认 4 后暂存 uint32 4 时为 true。
bool speakerKeepsRawNumber(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId speaker = ParameterId::parse("SpeakerCheckValue").value();
    if (!parseId("SpeakerCheckValue", "speaker_keeps_raw_number", speaker)) {
        return false;
    }
    store.observe(speaker, ScalarValue::fromUInt32(1U), RuntimeTime{std::chrono::seconds{3}});
    ErncFlagPage page(&catalog, &store, &layout);
    if (page.hasToggle(QStringLiteral("SpeakerCheckValue")) || page.valueText(QStringLiteral("SpeakerCheckValue")) != QStringLiteral("1")
        || page.valueText(QStringLiteral("SpeakerCheckValue")) == QStringLiteral("开")) {
        return fail("speaker_keeps_raw_number");
    }
    page.commitRaw(QStringLiteral("SpeakerCheckValue"), QStringLiteral("4"));
    const ParameterRuntimeState* state = store.find(speaker);
    if (state == nullptr || !state->dirty || !state->stagedValue) {
        return fail("speaker_keeps_raw_number");
    }
    const Result<std::uint32_t> staged = state->stagedValue->asUInt32();
    if (!staged.ok() || staged.value() != 4U || page.valueText(QStringLiteral("SpeakerCheckValue")) != QStringLiteral("4")) {
        return fail("speaker_keeps_raw_number");
    }
    return true;
}

/// @brief Bypass 没有开/关选择，只接受 uint32 文本。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 「开」不暂存，数字 3 暂存为 uint32 3 时为 true。
bool bypassHasNoToggle(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId bypass = ParameterId::parse("Bypass_mod").value();
    if (!parseId("Bypass_mod", "bypass_has_no_toggle", bypass)) {
        return false;
    }
    ErncFlagPage page(&catalog, &store, &layout);
    if (page.hasToggle(QStringLiteral("Bypass_mod"))) {
        return fail("bypass_has_no_toggle");
    }
    page.commitRaw(QStringLiteral("Bypass_mod"), QStringLiteral("开"));
    if (store.find(bypass) != nullptr) {
        return fail("bypass_has_no_toggle");
    }
    page.commitRaw(QStringLiteral("Bypass_mod"), QStringLiteral("3"));
    const ParameterRuntimeState* state = store.find(bypass);
    if (state == nullptr || !state->dirty || !state->stagedValue) {
        return fail("bypass_has_no_toggle");
    }
    const Result<std::uint32_t> staged = state->stagedValue->asUInt32();
    if (!staged.ok() || staged.value() != 3U || page.valueText(QStringLiteral("Bypass_mod")) == QStringLiteral("开")) {
        return fail("bypass_has_no_toggle");
    }
    return true;
}

/// @brief RNC_refresh 不能执行，也不写入 1 再写 0。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 控件不可用，按下后原值仍是 4 且没有暂存时为 true。
bool refreshStaysBlocked(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId refresh = ParameterId::parse("RNC_refresh").value();
    if (!parseId("RNC_refresh", "refresh_stays_blocked", refresh)) {
        return false;
    }
    store.observe(refresh, ScalarValue::fromInt32(4), RuntimeTime{std::chrono::seconds{5}});
    ErncFlagPage page(&catalog, &store, &layout);
    if (page.refreshEnabled() || page.hasToggle(QStringLiteral("RNC_refresh"))
        || page.valueText(QStringLiteral("RNC_refresh")) != QStringLiteral("4")) {
        return fail("refresh_stays_blocked");
    }
    page.pressRefresh();
    page.chooseToggle(QStringLiteral("RNC_refresh"), QStringLiteral("开"));
    page.commitRaw(QStringLiteral("RNC_refresh"), QStringLiteral("1"));
    const ParameterRuntimeState* state = store.find(refresh);
    if (state == nullptr || state->dirty || state->stagedValue || page.valueText(QStringLiteral("RNC_refresh")) != QStringLiteral("4")) {
        return fail("refresh_stays_blocked");
    }
    const Result<std::int32_t> device = state->deviceValue->asInt32();
    if (!device.ok() || device.value() != 4) {
        return fail("refresh_stays_blocked");
    }
    return true;
}

/// @brief 只有项目树选中 ERNC_Flag 时才显示功能页。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 初始隐藏，选中 ERNC_Flag 后显示，改到 ALL 后重新隐藏时为 true。
bool flagPageFollowsTree(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EditorTabs tabs(&catalog, &store, &layout);
    QWidget* flag = tabs.findChild<QWidget*>(QStringLiteral("erncFlagPage"));
    QWidget* summary = tabs.findChild<QWidget*>(QStringLiteral("erncFunctionSummary"));
    if (flag == nullptr || summary == nullptr || !flag->isHidden() || summary->isHidden()) {
        return fail("flag_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("ERNC_Flag"));
    if (flag->isHidden() || !summary->isHidden()) {
        return fail("flag_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("ALL"));
    if (!flag->isHidden() || summary->isHidden()) {
        return fail("flag_page_follows_tree");
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
    if (name == "reverse_switches_stage_wire_values") {
        return reverseSwitchesStageWireValues(catalog, layout);
    }
    if (name == "unknown_toggle_is_not_off") {
        return unknownToggleIsNotOff(catalog, layout);
    }
    if (name == "speaker_keeps_raw_number") {
        return speakerKeepsRawNumber(catalog, layout);
    }
    if (name == "bypass_has_no_toggle") {
        return bypassHasNoToggle(catalog, layout);
    }
    if (name == "refresh_stays_blocked") {
        return refreshStaysBlocked(catalog, layout);
    }
    if (name == "flag_page_follows_tree") {
        return flagPageFollowsTree(catalog, layout);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个 ERNC_Flag 页面用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是 layout manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_flag_page_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
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
