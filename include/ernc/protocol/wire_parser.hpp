#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace ernc::protocol {

/// @brief 从字节流里取出一帧之后的下一步。
///
/// NeedMore 表示还要等待。LengthDropped 和 CrcRejected 都不是成功帧。
/// 只有 Frame 的 frame 里有字节，内容是恰好 1 + 长度 的线字节。
struct ParseStep {
    /// 这一步是等待、交出一帧、丢掉非法长度，还是因 CRC 丢弃整段。
    enum class Kind {
        /// 缓冲为空，或长度合法但后续字节还没到齐。已收到的字节仍留在解析器里。
        NeedMore,

        /// 长度在 5 到 255 之间，字节到齐，并且 CRC 通过。
        Frame,

        /// 长度字节小于 5。这一字节已经从缓冲中删除。
        LengthDropped,

        /// 声明长度的字节已经到齐，但 CRC 不符。整段 1 + 长度 已从缓冲中删除。
        CrcRejected
    };

    /// 本步类别。Frame 以外都不表示拆出了可用帧。
    Kind kind{Kind::NeedMore};

    /// 成功帧的线字节，含长度前缀。其他类别为空。调用方拥有这份副本。
    std::vector<std::uint8_t> frame;
};

/// @brief 长度前缀帧的流式解析器。
///
/// 缓冲由本对象持有。调用方可以一次送入 1 字节或多帧粘在一起的字节。
/// 长度小于 5 时丢掉 1 字节后继续；CRC 失败时丢掉声明的整段，不把该段当成帧。
/// 证据等级：旧版 sim_slave 与 bridge_core 主机缓冲的长度前缀拆法。没有已批准 Golden。
/// 不解析功能码，不调用 validateResponse，也不访问串口。对象没有线程归属。
class WireParser {
public:
    /// @brief 把新到的字节追加进缓冲。
    /// @param bytes 新字节，只在本次调用期间借用。解析器保存自己的副本。空输入不改变缓冲。
    void append(std::span<const std::uint8_t> bytes);

    /// @brief 从缓冲开头处理一步。
    /// @return 需要更多数据、一帧、一次非法长度丢弃，或一次 CRC 失败。Frame 的 frame 由调用方拥有。
    ParseStep pull();

    /// @brief 丢弃尚未交出去的字节。
    ///
    /// 用于连接断开。清空后，上一段半帧不会和后续连接的字节拼成一帧。
    void clear() noexcept;

private:
    /// 尚未交出去的字节。下标 0 是下一次判断使用的长度字节。
    std::vector<std::uint8_t> pending_;
};

}  // namespace ernc::protocol
