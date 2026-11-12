#include "can_lamp.hpp"

#include <cmath>
#include <iostream>
#include <string_view>

namespace {

using ernc::application::CanLampColor;
using ernc::application::evaluateCanLamp;
using ernc::application::kCanLampValueEpsilon;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

bool lamp_colors_0_1_gray() {
    constexpr const char* name = "lamp_colors_0_1_gray";
    if (evaluateCanLamp(0.0f, false) != CanLampColor::Gray) {
        return fail(name);
    }
    if (evaluateCanLamp(0.0f, true) != CanLampColor::Red) {
        return fail(name);
    }
    if (evaluateCanLamp(1.0f, true) != CanLampColor::Green) {
        return fail(name);
    }
    if (evaluateCanLamp(0.5f, true) != CanLampColor::Gray) {
        return fail(name);
    }
    if (evaluateCanLamp(kCanLampValueEpsilon * 0.5f, true) != CanLampColor::Red) {
        return fail(name);
    }
    if (evaluateCanLamp(1.0f - kCanLampValueEpsilon * 0.5f, true) != CanLampColor::Green) {
        return fail(name);
    }
    if (evaluateCanLamp(1.0f + kCanLampValueEpsilon * 2.0f, true) != CanLampColor::Gray) {
        return fail(name);
    }
    return true;
}

bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "lamp_colors_0_1_gray") {
        return lamp_colors_0_1_gray();
    }
    recognized = false;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_can_lamp_tests <case>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
