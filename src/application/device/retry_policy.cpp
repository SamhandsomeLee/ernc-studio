#include "retry_policy.hpp"

namespace ernc::application {

ReadRetryStep decideReadRetry(const ReadRetryLimit& limit, RetryTime sentAt, RetryTime now,
                              std::uint8_t attempt) {
    ReadRetryStep step;
    if (limit.timeout.count() <= 0 || limit.maxAttempts == 0 || attempt == 0) {
        step.decision = ReadRetryDecision::Finished;
        step.terminal = ernc::domain::ErrorCode::InvalidParameter;
        return step;
    }
    if (now < sentAt + limit.timeout) {
        step.decision = ReadRetryDecision::Waiting;
        return step;
    }
    if (attempt < limit.maxAttempts) {
        step.decision = ReadRetryDecision::Resubmit;
        return step;
    }
    step.decision = ReadRetryDecision::Finished;
    step.terminal = attempt == 1 ? ernc::domain::ErrorCode::ResponseTimeout
                                 : ernc::domain::ErrorCode::RetryExhausted;
    return step;
}

}  // namespace ernc::application
