#include "cancellation.hpp"

namespace ernc::application {

FrameDisposition disposeFrame(bool discardNextFrame, bool commandStillCurrent) noexcept {
    FrameDisposition disposition;
    disposition.dropFrame = discardNextFrame || !commandStillCurrent;
    return disposition;
}

}  // namespace ernc::application
