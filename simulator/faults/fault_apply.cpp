#include "faults/fault_apply.hpp"

namespace ernc::simulator {
namespace {

/// BadFrame 交还的固定短帧。它不是任何合法的 FC03 或 FC16 成功响应。
constexpr std::uint8_t kBadFrame[] = {0x01, 0xFF};

/// @brief 把正常交换结果包成无故障回复。
/// @param reply exchange 的结果，只在本次调用期间借用。
/// @return countsAsSuccess 与 responded 相同。
FaultReply normalReply(const SlaveReply& reply) {
    FaultReply fault;
    fault.kind = FaultKind::None;
    fault.deliversBytes = reply.responded;
    fault.countsAsSuccess = reply.responded;
    fault.frame = reply.frame;
    return fault;
}

}  // namespace

FaultReply applyFault(SlaveImage& image, std::span<const std::uint8_t> request, FaultKind kind) {
    if (kind == FaultKind::Drop || kind == FaultKind::Disconnect) {
        FaultReply fault;
        fault.kind = kind;
        return fault;
    }
    const SlaveReply reply = exchange(image, request);
    if (kind == FaultKind::None) {
        return normalReply(reply);
    }
    if (kind == FaultKind::Delay) {
        FaultReply fault;
        fault.kind = FaultKind::Delay;
        fault.frame = reply.frame;
        fault.delayPolls = reply.responded ? 1U : 0U;
        return fault;
    }
    if (kind == FaultKind::BadFrame) {
        FaultReply fault;
        fault.kind = FaultKind::BadFrame;
        fault.deliversBytes = true;
        fault.frame.assign(kBadFrame, kBadFrame + 2);
        return fault;
    }
    FaultReply fault;
    fault.kind = FaultKind::BadCrc;
    if (!reply.frame.empty()) {
        fault.frame = reply.frame;
        fault.frame.back() = static_cast<std::uint8_t>(fault.frame.back() ^ 0xFFU);
        fault.deliversBytes = true;
    }
    return fault;
}

}  // namespace ernc::simulator
