#pragma once

#include "retry_policy.hpp"

#include <chrono>
#include <span>
#include <vector>

namespace ernc::application {

/// 一次要求读回的写命令所处的阶段。Sent、Acked、Verifying 仍在途。
enum class WritePhase {
    /// FC16 已提交，还没有匹配的 ACK。
    Sent,

    /// ACK 已匹配，读回请求还没有提交。
    Acked,

    /// 读回用的 FC03 已提交，还没有可比的数据区。
    Verifying,

    /// 读回数据区和写入字节相同。
    Verified,

    /// 读回数据区和写入字节不同。失败类别是 ReadBackMismatch。
    Mismatch
};

/// 写 ACK 等待的下一步。没有重发。
enum class WriteWaitDecision {
    /// 截止时间未到。
    Waiting,

    /// 等待结束。调用方不得再次提交 FC16。
    Finished
};

/// @brief 写入字节和读回字节的对照结果。
///
/// 两份字节都由本对象持有。Mismatch 不用读回覆盖写入目标。
struct WriteVerification {
    /// Verified 或 Mismatch。对照函数不返回在途阶段。
    WritePhase phase{WritePhase::Mismatch};

    /// 入队时的写入字节。调用方拥有对照结果里的这份副本。
    std::vector<std::uint8_t> target;

    /// 读回数据区。长度可以和 target 不同。
    std::vector<std::uint8_t> actual;
};

/// @brief 判断一次仍在等待 ACK 的写命令是否继续等。
///
/// 没有重发结果。函数不读取系统钟，也不提交端口。
/// @param timeout 等待长度。0 或负数由调用方当成无效上限，不在这里重发。
/// @param sentAt 最近一次 FC16 submit 的时刻。
/// @param now 调用方给出的当前时刻。早于 sentAt 时继续等待。
/// @return 未到截止时间为 Waiting。已经到达且 timeout 为正时为 Finished，调用方以 ResponseTimeout 结束。
WriteWaitDecision decideWriteAckWait(std::chrono::milliseconds timeout, RetryTime sentAt, RetryTime now);

/// @brief 比较写入字节和读回数据区。
/// @param target 写入字节，只在本次调用期间借用。
/// @param actual 读回数据区，只在本次调用期间借用。
/// @return 长度和每个字节都相同为 Verified。否则为 Mismatch。两份字节都被复制进结果。
WriteVerification compareWriteReadBack(std::span<const std::uint8_t> target,
                                       std::span<const std::uint8_t> actual);

}  // namespace ernc::application
