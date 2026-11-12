#include "ernc/application/simulator_mirror_seed.hpp"
#include "ernc/domain/types.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/protocol/typed_codec.hpp"

#include <cstdint>
#include <iostream>
#include <string_view>

namespace {

using ernc::application::makeDefaultSimulatorImage;
using ernc::application::makeDemoSimulatorImage;
using ernc::application::seedSimulatorMirrorDefaults;
using ernc::domain::CatalogLoad;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ScalarValue;

bool fail(std::string_view name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool demo_array_spk_order_nonzero(const char* catalogPath, const char* layoutPath) {
    const CatalogLoad catalogLoad = ParameterCatalogLoader::loadFile(catalogPath);
    const LayoutLoad layoutLoad = FirmwareLayoutLoader::loadFile(layoutPath);
    if (!catalogLoad.ok() || !layoutLoad.ok()) {
        return fail("demo_array_spk_order_nonzero");
    }
    std::vector<std::uint8_t> bytes = makeDemoSimulatorImage(catalogLoad.catalog(), layoutLoad.layout());
    const auto* definition = catalogLoad.catalog().find("SpkOrderAmpThre");
    if (definition == nullptr || !definition->firmwarePath) {
        return fail("demo_array_spk_order_nonzero");
    }
    const auto resolved = layoutLoad.layout().locate(*definition->firmwarePath);
    if (!resolved.ok()) {
        return fail("demo_array_spk_order_nonzero");
    }
    for (std::uint32_t index = 0U; index < resolved.field().slotCount; ++index) {
        const std::size_t offset = static_cast<std::size_t>(resolved.field().slot + index) * 4U;
        const ernc::domain::Result<ScalarValue> decoded =
            ernc::protocol::decodeScalar({bytes.data() + offset, 4U}, ScalarValue::Kind::Float32);
        if (!decoded.ok()) {
            return fail("demo_array_spk_order_nonzero");
        }
        const auto held = decoded.value().asFloat();
        if (!held.ok() || held.value() == 0.0F) {
            return fail("demo_array_spk_order_nonzero");
        }
    }
    return true;
}

bool demo_refernum_is_twelve(const char* catalogPath, const char* layoutPath) {
    const CatalogLoad catalogLoad = ParameterCatalogLoader::loadFile(catalogPath);
    const LayoutLoad layoutLoad = FirmwareLayoutLoader::loadFile(layoutPath);
    if (!catalogLoad.ok() || !layoutLoad.ok()) {
        return fail("demo_refernum_is_twelve");
    }
    std::vector<std::uint8_t> bytes = makeDemoSimulatorImage(catalogLoad.catalog(), layoutLoad.layout());
    const auto* definition = catalogLoad.catalog().find("RNC_param.refernum");
    if (definition == nullptr || !definition->firmwarePath) {
        return fail("demo_refernum_is_twelve");
    }
    const auto resolved = layoutLoad.layout().locate(*definition->firmwarePath);
    if (!resolved.ok()) {
        return fail("demo_refernum_is_twelve");
    }
    const std::size_t offset = static_cast<std::size_t>(resolved.field().slot) * 4U;
    const ernc::domain::Result<ScalarValue> decoded =
        ernc::protocol::decodeScalar({bytes.data() + offset, 4U}, ScalarValue::Kind::Int32);
    if (!decoded.ok()) {
        return fail("demo_refernum_is_twelve");
    }
    const auto held = decoded.value().asInt32();
    if (!held.ok() || held.value() != 12) {
        return fail("demo_refernum_is_twelve");
    }
    return true;
}

bool defaults_cover_anc_off(const char* catalogPath, const char* layoutPath) {
    const CatalogLoad catalogLoad = ParameterCatalogLoader::loadFile(catalogPath);
    const LayoutLoad layoutLoad = FirmwareLayoutLoader::loadFile(layoutPath);
    if (!catalogLoad.ok() || !layoutLoad.ok()) {
        return fail("defaults_cover_anc_off");
    }
    std::vector<std::uint8_t> bytes = makeDefaultSimulatorImage(catalogLoad.catalog(), layoutLoad.layout());
    if (bytes.empty()) {
        return fail("defaults_cover_anc_off");
    }
    const auto* definition = catalogLoad.catalog().find("ANC_OFF");
    if (definition == nullptr || !definition->firmwarePath) {
        return fail("defaults_cover_anc_off");
    }
    const auto resolved = layoutLoad.layout().locate(*definition->firmwarePath);
    if (!resolved.ok()) {
        return fail("defaults_cover_anc_off");
    }
    const std::uint32_t slot = resolved.field().slot;
    const std::size_t offset = static_cast<std::size_t>(slot) * 4U;
    if (offset + 4U > bytes.size()) {
        return fail("defaults_cover_anc_off");
    }
    const ernc::domain::Result<ScalarValue> decoded =
        ernc::protocol::decodeScalar({bytes.data() + offset, 4U}, ScalarValue::Kind::Float32);
    if (!decoded.ok() || decoded.tryValue() == nullptr) {
        return fail("defaults_cover_anc_off");
    }
    const auto held = decoded.value().asFloat();
    if (!held.ok() || held.value() != 0.0F) {
        return fail("defaults_cover_anc_off");
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: ernc_simulator_mirror_seed_tests <case> <catalog> <layout>\n";
        return 2;
    }
    const std::string_view name = argv[1];
    if (name == "defaults_cover_anc_off") {
        return defaults_cover_anc_off(argv[2], argv[3]) ? 0 : 1;
    }
    if (name == "demo_refernum_is_twelve") {
        return demo_refernum_is_twelve(argv[2], argv[3]) ? 0 : 1;
    }
    if (name == "demo_array_spk_order_nonzero") {
        return demo_array_spk_order_nonzero(argv[2], argv[3]) ? 0 : 1;
    }
    std::cerr << "unknown case\n";
    return 2;
}
