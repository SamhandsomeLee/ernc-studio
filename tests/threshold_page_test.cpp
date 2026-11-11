#include "pages/threshold.hpp"
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

using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::presentation::EditorTabs;
using ernc::presentation::ThresholdPage;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool thresholdGroupsHaveNoFormula(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    const Result<ParameterId> hornId = ParameterId::parse("HornOFFContinueTime");
    if (!hornId.ok() || hornId.tryValue() == nullptr) {
        return fail("threshold_groups_have_no_formula");
    }
    store.observe(hornId.value(), ScalarValue::fromFloat(1100.0F), RuntimeTime{std::chrono::seconds{3}});
    ThresholdPage page(&catalog, &store, &layout);
    const QString hornText = page.valueText(QStringLiteral("HornOFFContinueTime"));
    if (page.hasFormulaButton() || page.groupOf(QStringLiteral("ENCEchoControlFlag")) != QStringLiteral("ENC Echo")
        || page.groupOf(QStringLiteral("ENC_Divergence_Con_Flag")) != QStringLiteral("ENC Divergence")
        || page.groupOf(QStringLiteral("horn_control")) != QStringLiteral("Horn")
        || page.groupOf(QStringLiteral("rnc_dicergence.rnc_divergence_con_flag")) != QStringLiteral("RNC Divergence")
        || page.groupOf(QStringLiteral("Input_threshold")) != QStringLiteral("IO Threshold")
        || page.groupOf(QStringLiteral("SpkOrderAmpThre")) != QStringLiteral("其他阈值") || page.hasEditor(QStringLiteral("SpkOrderAmpThre"))
        || hornText != QStringLiteral("1100") || hornText.contains(QStringLiteral("秒"))) {
        return fail("threshold_groups_have_no_formula");
    }
    page.activate(QStringLiteral("rnc_dicergence.rnc_diverge_threshold"));
    if (page.selectedParameterId() != QStringLiteral("rnc_dicergence.rnc_diverge_threshold")
        || page.valueText(QStringLiteral("rnc_dicergence.rnc_diverge_threshold")) != QStringLiteral("数组")) {
        return fail("threshold_groups_have_no_formula");
    }
    return true;
}

bool thresholdPageFollowsTree(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EditorTabs tabs(&catalog, &store, &layout);
    QWidget* threshold = tabs.findChild<QWidget*>(QStringLiteral("erncThresholdPage"));
    QWidget* can = tabs.findChild<QWidget*>(QStringLiteral("erncCanNamePage"));
    if (threshold == nullptr || can == nullptr || !threshold->isHidden()) {
        return fail("threshold_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("Threshold"));
    if (threshold->isHidden() || !can->isHidden()) {
        return fail("threshold_page_follows_tree");
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_threshold_page_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    const auto catalog = ParameterCatalogLoader::loadFile(argv[2]);
    const auto layout = FirmwareLayoutLoader::loadFile(argv[3]);
    if (!catalog.ok() || !layout.ok()) {
        std::cerr << "failed: load\n";
        return 1;
    }
    const std::string_view name = argv[1];
    const bool ok = name == "threshold_groups_have_no_formula"
        ? thresholdGroupsHaveNoFormula(catalog.catalog(), layout.layout())
        : name == "threshold_page_follows_tree" ? thresholdPageFollowsTree(catalog.catalog(), layout.layout())
                                                : false;
    if (!ok) {
        if (name != "threshold_groups_have_no_formula" && name != "threshold_page_follows_tree") {
            std::cerr << "unknown case: " << name << '\n';
        }
        return 1;
    }
    return 0;
}
