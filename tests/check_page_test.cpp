#include "pages/check.hpp"
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
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeState;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::presentation::CheckPage;
using ernc::presentation::EditorTabs;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool parseId(const char* text, const char* name, ParameterId& id) {
    const Result<ParameterId> parsed = ParameterId::parse(text);
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail(name);
    }
    id = *parsed.tryValue();
    return true;
}

bool checkModeRequiresConfirm(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    CheckPage page(&catalog, &store, &layout);
    const QString mode = QStringLiteral("check_mod");
    if (!page.hasEditor(mode) || page.containsParameter(QStringLiteral("check_data.OutputCheck"))) {
        return fail("check_mode_requires_confirm");
    }
    page.setDraft(mode, QStringLiteral("1"));
    ParameterId id = ParameterId::parse("check_mod").value();
    if (!parseId("check_mod", "check_mode_requires_confirm", id) || store.find(id) != nullptr) {
        return fail("check_mode_requires_confirm");
    }
    page.confirm(mode);
    const ParameterRuntimeState* state = store.find(id);
    if (state == nullptr || !state->dirty || !state->stagedValue) {
        return fail("check_mode_requires_confirm");
    }
    const Result<float> staged = state->stagedValue->asFloat();
    if (!staged.ok() || staged.value() != 1.0F || page.valueText(mode) == QStringLiteral("开")) {
        return fail("check_mode_requires_confirm");
    }
    return true;
}

bool checkRegionKeepsParentId(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    CheckPage page(&catalog, &store, &layout);
    bool opened = false;
    page.setCanOpener([&opened] { opened = true; });
    const QString region = QStringLiteral("check_data");
    if (page.hasEditor(region) || page.valueText(region) != QStringLiteral("数组")
        || page.valueText(QStringLiteral("ERNC_TF")) != QStringLiteral("数组")) {
        return fail("check_region_keeps_parent_id");
    }
    page.activate(region);
    page.openCanCatalog();
    if (page.selectedParameterId() != region || !opened) {
        return fail("check_region_keeps_parent_id");
    }
    return true;
}

bool checkPageFollowsTree(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EditorTabs tabs(&catalog, &store, &layout);
    QWidget* check = tabs.findChild<QWidget*>(QStringLiteral("erncCheckPage"));
    QWidget* summary = tabs.findChild<QWidget*>(QStringLiteral("erncFunctionSummary"));
    if (check == nullptr || summary == nullptr || !check->isHidden() || summary->isHidden()) {
        return fail("check_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("Check"));
    if (check->isHidden() || !summary->isHidden()) {
        return fail("check_page_follows_tree");
    }
    tabs.selectPage(QStringLiteral("RNC_Param"));
    if (!check->isHidden()) {
        return fail("check_page_follows_tree");
    }
    return true;
}

bool runCase(std::string_view name, const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    if (name == "check_mode_requires_confirm") {
        return checkModeRequiresConfirm(catalog, layout);
    }
    if (name == "check_region_keeps_parent_id") {
        return checkRegionKeepsParentId(catalog, layout);
    }
    if (name == "check_page_follows_tree") {
        return checkPageFollowsTree(catalog, layout);
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_check_page_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    const CatalogLoad catalog = ParameterCatalogLoader::loadFile(argv[2]);
    const LayoutLoad layout = FirmwareLayoutLoader::loadFile(argv[3]);
    if (!catalog.ok() || !layout.ok()) {
        std::cerr << "failed: load\n";
        return 1;
    }
    return runCase(argv[1], catalog.catalog(), layout.layout()) ? 0 : 1;
}
