#include "faults/fault_sequence.hpp"

namespace ernc::simulator {
namespace {

/// Numerical Recipes 的 32 位 LCG 乘数。只用来展开可重复的故障种类。
constexpr std::uint32_t kMultiplier = 1664525U;

/// 同一个 LCG 的加数。
constexpr std::uint32_t kIncrement = 1013904223U;

/// 六种故障按这个顺序对应状态除以 6 的余数。
constexpr FaultKind kKinds[] = {FaultKind::None,     FaultKind::Drop,     FaultKind::Delay,
                                FaultKind::BadFrame, FaultKind::BadCrc, FaultKind::Disconnect};

}  // namespace

FaultSequence::FaultSequence(std::uint32_t seed) noexcept : state_(seed) {}

FaultKind FaultSequence::next() {
    state_ = state_ * kMultiplier + kIncrement;
    return kKinds[state_ % 6U];
}

}  // namespace ernc::simulator
