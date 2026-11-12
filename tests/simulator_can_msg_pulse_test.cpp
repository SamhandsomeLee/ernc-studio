#include "ernc/application/simulator_can_msg_pulse.hpp"
#include "ernc/application/simulator_mirror_seed.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/protocol/typed_codec.hpp"

#include "slave_image.hpp"

#include <iostream>
#include <string_view>
#include <vector>

namespace {

using ernc::application::makeDemoSimulatorImage;
using ernc::application::writeCanMsgMirrorLevel;
using ernc::domain::CatalogLoad;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ScalarValue;
using ernc::simulator::SlaveImage;

bool fail(std::string_view name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool alternates_zero_and_one(const char* catalogPath, const char* layoutPath) {
    const CatalogLoad catalogLoad = ParameterCatalogLoader::loadFile(catalogPath);
    const LayoutLoad layoutLoad = FirmwareLayoutLoader::loadFile(layoutPath);
    if (!catalogLoad.ok() || !layoutLoad.ok()) {
        return fail("alternates_zero_and_one");
    }
    std::vector<std::uint8_t> bytes = makeDemoSimulatorImage(catalogLoad.catalog(), layoutLoad.layout());
    SlaveImage image{std::move(bytes)};
    if (!writeCanMsgMirrorLevel(image, layoutLoad.layout(), true)) {
        return fail("alternates_zero_and_one");
    }
    const ernc::domain::LayoutResolve resolved = layoutLoad.layout().locate("Can_Msg");
    if (!resolved.ok()) {
        return fail("alternates_zero_and_one");
    }
    const std::size_t offset = static_cast<std::size_t>(resolved.field().slot) * 4U;
    const auto decode = [&](std::size_t off) {
        return ernc::protocol::decodeScalar(image.bytes().subspan(off, 4U), ScalarValue::Kind::Float32);
    };
    const auto one = decode(offset);
    if (!one.ok() || one.value().asFloat().value() != 1.0F) {
        return fail("alternates_zero_and_one");
    }
    if (!writeCanMsgMirrorLevel(image, layoutLoad.layout(), false)) {
        return fail("alternates_zero_and_one");
    }
    const auto zero = decode(offset);
    if (!zero.ok() || zero.value().asFloat().value() != 0.0F) {
        return fail("alternates_zero_and_one");
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: ernc_simulator_can_msg_pulse_tests <case> <catalog> <layout>\n";
        return 2;
    }
    const std::string_view name = argv[1];
    if (name == "alternates_zero_and_one") {
        return alternates_zero_and_one(argv[2], argv[3]) ? 0 : 1;
    }
    std::cerr << "unknown case\n";
    return 2;
}
