#include "pages/enc_param.hpp"
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
using ernc::presentation::EncParamPage;

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

/// @brief 三张卡片都有标志和 Amp、Step、Leaky，开闭标志保持数字。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 线值 1 显示为 1 而不是开，确认 2 后暂存 float 2 时为 true。
bool ordersShareFieldsAndKeepRawFlag(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId flag = ParameterId::parse("enc_order.OrderOne_Flag").value();
    if (!parseId("enc_order.OrderOne_Flag", "orders_share_fields_and_keep_raw_flag", flag)) {
        return false;
    }
    store.observe(flag, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{1}});
    EncParamPage page(&catalog, &store, &layout);
    const QString flagId = QStringLiteral("enc_order.OrderOne_Flag");
    const char* required[] = {"enc_order.OrderOne_Flag", "enc_order.OrderTwo_Flag", "enc_order.OrderThree_Flag",
        "ENC_param.TachofAmp1", "ENC_param.Amp1", "ENC_param.TachofStep1", "ENC_param.Step1", "ENC_param.TachofLeaky1",
        "ENC_param.Leaky1", "ENC_param.TachofAmp2", "ENC_param.Step2", "ENC_param.TachofLeaky3", "ENC_param.Leaky3",
        "ENC_param.ErrWei1", "ENC_param.Delta3", "ENC_param.MaxEngineSpeed"};
    if (page.orderCardCount() != 3) {
        return fail("orders_share_fields_and_keep_raw_flag");
    }
    for (const char* id : required) {
        if (!page.containsParameter(QString::fromUtf8(id))) {
            return fail("orders_share_fields_and_keep_raw_flag");
        }
    }
    if (!page.hasEditor(flagId) || page.valueText(flagId) != QStringLiteral("1") || page.valueText(flagId) == QStringLiteral("开")
        || !page.detailText(flagId).contains(QStringLiteral("阶次1开闭标志"))) {
        return fail("orders_share_fields_and_keep_raw_flag");
    }
    page.commitValue(flagId, QStringLiteral("开"));
    const ParameterRuntimeState* before = store.find(flag);
    if (before == nullptr || before->dirty || before->stagedValue) {
        return fail("orders_share_fields_and_keep_raw_flag");
    }
    page.commitValue(flagId, QStringLiteral("2"));
    const ParameterRuntimeState* state = store.find(flag);
    if (state == nullptr || !state->dirty || !state->stagedValue || !state->deviceValue) {
        return fail("orders_share_fields_and_keep_raw_flag");
    }
    const Result<float> staged = state->stagedValue->asFloat();
    const Result<float> device = state->deviceValue->asFloat();
    if (!staged.ok() || staged.value() != 2.0F || !device.ok() || device.value() != 1.0F
        || page.valueText(flagId) != QStringLiteral("2")) {
        return fail("orders_share_fields_and_keep_raw_flag");
    }
    return true;
}

/// @brief Amp1 是数组入口，点击后 ID 不变。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 没有数字框，显示数组，且确认数字不会暂存时为 true。
bool amp1ArrayKeepsParameterId(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EncParamPage page(&catalog, &store, &layout);
    const QString amp = QStringLiteral("ENC_param.Amp1");
    if (page.hasEditor(amp) || page.valueText(amp) != QStringLiteral("数组")) {
        return fail("amp1_array_keeps_parameter_id");
    }
    page.activate(amp);
    page.commitValue(amp, QStringLiteral("1"));
    ParameterId id = ParameterId::parse("ENC_param.Amp1").value();
    if (!parseId("ENC_param.Amp1", "amp1_array_keeps_parameter_id", id)) {
        return false;
    }
    if (page.selectedParameterId() != amp || store.find(id) != nullptr) {
        return fail("amp1_array_keeps_parameter_id");
    }
    return true;
}

/// @brief 阶次 4、5 和噪声类型不在本页。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 这些 ID 都查找不到时为 true。
bool orderFourFiveAreAbsent(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EncParamPage page(&catalog, &store, &layout);
    const char* absent[] = {"OrderFour_Flag", "OrderFive_Flag", "ENC_param.Amp4", "ENC_param.Step5", "enc_order.OrderOne_Type"};
    for (const char* id : absent) {
        if (page.containsParameter(QString::fromUtf8(id))) {
            return fail("order_four_five_are_absent");
        }
    }
    return true;
}

/// @brief 只有项目树选中 ENC_Param 时才显示阶次页。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 初始隐藏，选中该页后显示，改到旗标页后重新隐藏时为 true。
bool encPageFollowsTree(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EditorTabs tabs(&catalog, &store, &layout);
    QWidget* enc = tabs.findChild<QWidget*>(QStringLiteral("erncEncParamPage"));
    QWidget* flag = tabs.findChild<QWidget*>(QStringLiteral("erncFlagPage"));
    QWidget* inOut = tabs.findChild<QWidget*>(QStringLiteral("erncInOutPage"));
    QWidget* summary = tabs.findChild<QWidget*>(QStringLiteral("erncFunctionSummary"));
    if (enc == nullptr || flag == nullptr || inOut == nullptr || summary == nullptr || !enc->isHidden() || !flag->isHidden()
        || !inOut->isHidden() || summary->isHidden()) {
        return fail("enc_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("ENC_Param"));
    if (enc->isHidden() || !flag->isHidden() || !inOut->isHidden() || !summary->isHidden()) {
        return fail("enc_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("ERNC_Flag"));
    if (!enc->isHidden() || flag->isHidden() || !summary->isHidden()) {
        return fail("enc_page_follows_tree");
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
    if (name == "orders_share_fields_and_keep_raw_flag") {
        return ordersShareFieldsAndKeepRawFlag(catalog, layout);
    }
    if (name == "amp1_array_keeps_parameter_id") {
        return amp1ArrayKeepsParameterId(catalog, layout);
    }
    if (name == "order_four_five_are_absent") {
        return orderFourFiveAreAbsent(catalog, layout);
    }
    if (name == "enc_page_follows_tree") {
        return encPageFollowsTree(catalog, layout);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个 ENC_Param 页面用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是 layout manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_enc_param_page_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
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
