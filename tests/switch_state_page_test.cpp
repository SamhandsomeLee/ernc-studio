#include "pages/switch_state.hpp"
#include "workspace/editor_tabs.hpp"

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QApplication>
#include <QWidget>

#include <iostream>
#include <string_view>

namespace {

using ernc::domain::CatalogLoad;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterRuntimeStore;
using ernc::presentation::EditorTabs;
using ernc::presentation::SwitchStatePage;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool switchStateIsUnnamed() {
    SwitchStatePage page;
    const QString joined = page.stateLabel(0) + page.stateLabel(1) + page.stateLabel(2) + page.stateValue(0);
    if (page.stateLabel(0) != QStringLiteral("State0") || page.stateLabel(1) != QStringLiteral("State1")
        || page.stateLabel(2) != QStringLiteral("State2") || page.stateValue(1) != QStringLiteral("未读取")
        || page.updatedAtText() != QStringLiteral("—") || joined.contains(QStringLiteral("ANC"))
        || joined.contains(QStringLiteral("ENC")) || joined.contains(QStringLiteral("RNC"))) {
        return fail("switch_state_is_unnamed");
    }
    return true;
}

bool switchPageFollowsTree(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EditorTabs tabs(&catalog, &store, &layout);
    QWidget* state = tabs.findChild<QWidget*>(QStringLiteral("erncSwitchStatePage"));
    QWidget* check = tabs.findChild<QWidget*>(QStringLiteral("erncCheckPage"));
    if (state == nullptr || check == nullptr || !state->isHidden()) {
        return fail("switch_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("ERNC_Switch_State"));
    if (state->isHidden() || !check->isHidden()) {
        return fail("switch_page_follows_tree");
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_switch_state_page_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    const auto catalog = ParameterCatalogLoader::loadFile(argv[2]);
    const LayoutLoad layout = FirmwareLayoutLoader::loadFile(argv[3]);
    if (!catalog.ok() || !layout.ok()) {
        std::cerr << "failed: load\n";
        return 1;
    }
    const std::string_view name = argv[1];
    const bool ok = name == "switch_state_is_unnamed" ? switchStateIsUnnamed()
        : name == "switch_page_follows_tree" ? switchPageFollowsTree(catalog.catalog(), layout.layout())
        : false;
    if (!ok) {
        if (name != "switch_state_is_unnamed" && name != "switch_page_follows_tree") {
            std::cerr << "unknown case: " << name << '\n';
        }
        return 1;
    }
    return 0;
}
