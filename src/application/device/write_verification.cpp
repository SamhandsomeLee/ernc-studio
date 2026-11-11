#include "write_verification.hpp"

#include <algorithm>

namespace ernc::application {

WriteWaitDecision decideWriteAckWait(std::chrono::milliseconds timeout, RetryTime sentAt, RetryTime now) {
    if (timeout.count() <= 0 || now < sentAt + timeout) {
        return WriteWaitDecision::Waiting;
    }
    return WriteWaitDecision::Finished;
}

WriteVerification compareWriteReadBack(std::span<const std::uint8_t> target,
                                       std::span<const std::uint8_t> actual) {
    WriteVerification result;
    result.target.assign(target.begin(), target.end());
    result.actual.assign(actual.begin(), actual.end());
    const bool same = target.size() == actual.size() && std::equal(target.begin(), target.end(), actual.begin());
    result.phase = same ? WritePhase::Verified : WritePhase::Mismatch;
    return result;
}

}  // namespace ernc::application
