#pragma once

#include <cstdint>
#include <vector>

namespace ernc::simulator {

/// 一次请求上要施加的故障。None 表示仍走从站的正常响应。
enum class FaultKind {
    /// 不改从站给出的响应。
    None,

    /// 请求不到达从站。镜像不变，也不交还字节。
    Drop,

    /// 从站已经处理请求，但响应晚一拍 poll 才交还。
    Delay,

    /// 交还固定短坏帧，不是原来的成功响应。
    BadFrame,

    /// 交还原来的响应，但最后一个字节被翻转。
    BadCrc,

    /// 请求不到达从站，并且链路关闭。
    Disconnect
};

/// @brief 按种子产生故障种类的来源。
///
/// 派生类决定下一种故障。内存端口只借用它，不拥有它。
class FaultSource {
public:
    /// @brief 释放派生对象。本身不产生故障。
    virtual ~FaultSource() = default;

    /// @brief 取出下一种故障。
    /// @return 这一次 submit 应施加的种类。
    virtual FaultKind next() = 0;
};

/// @brief 用固定种子展开故障序列。
///
/// 同一个种子、同样的调用次数，得到同一种类顺序。不读取系统钟，也不使用 std::rand。
class FaultSequence final : public FaultSource {
public:
    /// @brief 保存种子。此时还没有取出故障。
    /// @param seed 序列起点，无单位。0 也是一个确定的起点。
    explicit FaultSequence(std::uint32_t seed) noexcept;

    /// @brief 推进一次内部状态并返回对应故障。
    /// @return None、Drop、Delay、BadFrame、BadCrc、Disconnect 之一。
    FaultKind next() override;

private:
    /// 下一次推进前的内部状态。构造时等于种子。
    std::uint32_t state_;
};

}  // namespace ernc::simulator
