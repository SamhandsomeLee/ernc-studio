#include "pages/can_names.hpp"
#include "workspace/editor_tabs.hpp"

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <QApplication>
#include <QWidget>

#include <iostream>
#include <string_view>

namespace {

using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterRuntimeStore;
using ernc::presentation::CanNamePage;
using ernc::presentation::EditorTabs;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool canNamesStayUnmapped(const QString& canPath) {
    CanNamePage page;
    page.loadFile(canPath);
    if (page.nameCount() != 36 || page.nameAt(0) != QStringLiteral("车速") || page.statusAt(0) != QStringLiteral("未接入")
        || page.freshnessAt(35) != QStringLiteral("无")) {
        return fail("can_names_stay_unmapped");
    }
    for (int index = 0; index < page.nameCount(); ++index) {
        if (page.statusAt(index) != QStringLiteral("未接入") || page.nameAt(index).contains(QStringLiteral("Can_Msg"))) {
            return fail("can_names_stay_unmapped");
        }
    }
    return true;
}

bool canCatalogOpensFromCheck(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    EditorTabs tabs(&catalog, &store, &layout);
    QWidget* check = tabs.findChild<QWidget*>(QStringLiteral("erncCheckPage"));
    QWidget* can = tabs.findChild<QWidget*>(QStringLiteral("erncCanNamePage"));
    if (check == nullptr || can == nullptr) {
        return fail("can_catalog_opens_from_check");
    }
    tabs.selectPage(QStringLiteral("Check"));
    tabs.showCanCatalog();
    if (!check->isHidden() || can->isHidden()) {
        return fail("can_catalog_opens_from_check");
    }
    tabs.hideCanCatalog();
    if (check->isHidden() || !can->isHidden()) {
        return fail("can_catalog_opens_from_check");
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 5 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr || argv[4] == nullptr) {
        std::cerr << "usage: ernc_can_names_page_tests <case> <parameter_catalog.json> <layout_manifest.json> <can_names.raw.json>\n";
        return 2;
    }
    const auto catalog = ParameterCatalogLoader::loadFile(argv[2]);
    const auto layout = FirmwareLayoutLoader::loadFile(argv[3]);
    if (!catalog.ok() || !layout.ok()) {
        std::cerr << "failed: load\n";
        return 1;
    }
    const std::string_view name = argv[1];
    const QString canPath = QString::fromUtf8(argv[4]);
    const bool ok = name == "can_names_stay_unmapped" ? canNamesStayUnmapped(canPath)
        : name == "can_catalog_opens_from_check" ? canCatalogOpensFromCheck(catalog.catalog(), layout.layout())
        : false;
    if (!ok) {
        if (name != "can_names_stay_unmapped" && name != "can_catalog_opens_from_check") {
            std::cerr << "unknown case: " << name << '\n';
        }
        return 1;
    }
    return 0;
}
