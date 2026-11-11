#pragma once

namespace ernc::application {

/// @brief 一帧已经到齐的响应要不要记到当前命令上。
///
/// 线上帧没有 SessionId 或 CommandId。调用方用本地标志告诉本函数这帧属于哪一次提交。
struct FrameDisposition {
    /// 为 true 时调用方丢掉这一帧，不把它写成任何命令的成功结果。
    bool dropFrame{false};
};

/// @brief 判断这一帧是否因为取消或会话更换而过期。
///
/// 不读取端口，也不修改调度器。半帧还没到齐时不要调用。
/// @param discardNextFrame 调度器是否还欠一帧旧响应。取消或断连了一条已发出的命令时为 true。
/// @param commandStillCurrent 当前是否仍有一条在途命令可以接收这一帧。
/// @return dropFrame 为 true 时这一帧不能更新命令。discardNextFrame 为 true，或者已经没有当前命令时，都会丢弃。
FrameDisposition disposeFrame(bool discardNextFrame, bool commandStillCurrent) noexcept;

}  // namespace ernc::application
