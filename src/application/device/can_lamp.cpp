#include "can_lamp.hpp"

#include <cmath>

namespace ernc::application {

CanLampColor evaluateCanLamp(float value, bool hasValue) noexcept {
    if (!hasValue) {
        return CanLampColor::Gray;
    }
    if (std::fabs(value) <= kCanLampValueEpsilon) {
        return CanLampColor::Red;
    }
    if (std::fabs(value - 1.0f) <= kCanLampValueEpsilon) {
        return CanLampColor::Green;
    }
    return CanLampColor::Gray;
}

}  // namespace ernc::application
