#include "ernc/domain/array_parameter.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"

#include <iostream>

namespace {

using ernc::domain::CatalogLoad;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::shouldUseCompactArrayUi;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool compact_array_classification(const char* catalogPath, const char* layoutPath) {
    const CatalogLoad catalogLoad = ParameterCatalogLoader::loadFile(catalogPath);
    const auto layoutLoad = FirmwareLayoutLoader::loadFile(layoutPath);
    if (!catalogLoad.ok() || !layoutLoad.ok()) {
        return fail("load");
    }
    const auto* spk = catalogLoad.catalog().find("SpkOrderAmpThre");
    const auto* mic = catalogLoad.catalog().find("MicState");
    const auto* region = catalogLoad.catalog().find("check_data");
    const auto* curve = catalogLoad.catalog().find("rnc_dicergence.rnc_diverge_threshold");
    if (!shouldUseCompactArrayUi(spk, layoutLoad.layout()) || !shouldUseCompactArrayUi(mic, layoutLoad.layout())
        || shouldUseCompactArrayUi(region, layoutLoad.layout()) || shouldUseCompactArrayUi(curve, layoutLoad.layout())) {
        return fail("classify");
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: ernc_array_parameter_tests <catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    if (!compact_array_classification(argv[1], argv[2])) {
        return 1;
    }
    std::cout << "array parameter tests passed\n";
    return 0;
}
