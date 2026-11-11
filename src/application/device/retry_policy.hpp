#pragma once

#include "ernc/domain/types.hpp"

#include <chrono>
#include <cstdint>

namespace ernc::application {

/// 读重试使用的时刻。调用方传入，策略函数不读取系统钟。
/// 它不是参数记录里的 system_clock，避免墙上时间回拨被当成超时。
using RetryTime = std::chrono::steady_clock::time_point;

/// @brief 一次读命令的等待上限和允许提交次数。
///
/// 数值由调用方给定。这里不保存设备链路的默认毫秒数。
struct ReadRetryLimit {
    /// 每一次 submit 之后的等待长度。0 表示这份上限本身无效。
    std::chrono::milliseconds timeout{0};

    /// 允许的 submit 次数，含第一次发送。1 表示超时后不重发。0 表示这份上限本身无效。
    std::uint8_t maxAttempts{0};
};

/// 一条仍在等待响应的读命令下一步该做什么。
enum class ReadRetryDecision {
    /// 截止时间未到。不重发，也不结束命令。
    Waiting,

    /// 这一次发送已经超时，还可以再 submit 同一帧。
    Resubmit,

    /// 不能再提交。terminal 是命令的失败类别。
    Finished
};

/// @brief 读重试判断的一步结果。
///
/// Waiting 和 Resubmit 不使用 terminal。只有 Finished 才表示命令应结束。
struct ReadRetryStep {
    /// 继续等待、重发，或结束。
    ReadRetryDecision decision{ReadRetryDecision::Waiting};

    /// Finished 时的失败类别。其他决定里不读取它。
    ernc::domain::ErrorCode terminal{ernc::domain::ErrorCode::ResponseTimeout};
};

/// @brief 判断一次已经发出的读命令是否还要等、重发或结束。
///
/// 不访问端口，也不读取系统钟。完整响应、CRC 失败和断连不在这里判断。
/// @param limit 调用方给出的等待长度和次数。timeout 或 maxAttempts 为 0 时上限无效。
/// @param sentAt 最近一次 submit 的时刻。它不包含时区。
/// @param now 调用方给出的当前时刻。早于 sentAt 时视为还没到截止时间。
/// @param attempt 已经 submit 的次数，含第一次。0 表示计时还没开始。
/// @return 未到截止时间为 Waiting。仍有剩余次数时为 Resubmit。
/// 最后一次也超时时，只发过一次为 ResponseTimeout，已经重发过为 RetryExhausted。
/// 上限无效或 attempt 为 0 时为 Finished，错误码是 InvalidParameter。
ReadRetryStep decideReadRetry(const ReadRetryLimit& limit, RetryTime sentAt, RetryTime now,
                              std::uint8_t attempt);

}  // namespace ernc::application
