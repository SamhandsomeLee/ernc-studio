#include "faults/fault_sequence.hpp"
#include "faults/fault_apply.hpp"
#include "memory_transport.hpp"
#include "slave_image.hpp"
#include "ernc/protocol/crc16.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::ports::TransportStep;
using ernc::protocol::verifyCrc16;
using ernc::simulator::FaultKind;
using ernc::simulator::FaultReply;
using ernc::simulator::FaultSequence;
using ernc::simulator::FaultSource;
using ernc::simulator::MemorySlaveTransport;
using ernc::simulator::SlaveImage;
using ernc::simulator::applyFault;

/// 读 slot 0 的两寄存器请求。长度前缀和 CRC 都写在字面量里，不由编码器生成。
constexpr std::uint8_t kReadSlot0[] = {0x08, 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

/// 上面对应的 FC03 响应。下标 4 起的 4 字节是 slot 0 的数据区。
constexpr std::uint8_t kReadResponse[] = {0x09, 0x01, 0x03, 0x04, 0x78, 0x56, 0x34, 0x12, 0x94, 0x4E};

/// 单 slot 镜像的初始字节。一个逻辑 slot 是 4 字节。
constexpr std::uint8_t kSlotData[] = {0x78, 0x56, 0x34, 0x12};

/// BadFrame 应交还的固定短坏帧。它不是任何合法成功响应。
constexpr std::uint8_t kBadFrame[] = {0x01, 0xFF};

/// 同一种子上连续比较 next() 的次数。
constexpr int kPairCount = 8;

/// @brief 按预先存好的种类依次交出故障。只存在于本测试，不进入生产代码。
///
/// 内存端口只借用它，不拥有它。种类取完之后 next() 返回 None。
class ScriptedFaultSource final : public FaultSource {
public:
    /// @brief 保存要依次交还的故障种类。此时还没有调用 next()。
    /// @param kinds 按 next() 调用顺序交还的种类。按值接收，本对象持有自己的副本。空序列也允许。
    explicit ScriptedFaultSource(std::vector<FaultKind> kinds) : kinds_(std::move(kinds)) {}

    /// @brief 取出下一种已存故障。
    /// @return 还有未取种类时为当前下标的值，并把下标加一。已经取完或序列为空时为 None。
    FaultKind next() override {
        if (index_ >= kinds_.size()) {
            return FaultKind::None;
        }
        const FaultKind kind = kinds_[index_];
        ++index_;
        return kind;
    }

private:
    /// 调用方植入的故障顺序。下标 0 是第一次 next()。本对象拥有这份副本。
    std::vector<FaultKind> kinds_;

    /// 下一次 next() 读取的下标。0 表示还没有取出过。达到 kinds_ 的长度后不再前进。
    std::size_t index_{0};
};

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 两个缓冲区的长度和每个字节都相同。
/// @param actual 被测输出，只在本次调用期间借用。
/// @param expected 字面量期望，只在本次调用期间借用。
/// @return 完全一致时为 true。
bool sameBytes(std::span<const std::uint8_t> actual, std::span<const std::uint8_t> expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (actual[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

/// @brief poll 步骤为 Idle，字节为空，无错误码。
/// @param step 被检查的步骤，按常量引用借用，调用方仍拥有原对象。
/// @return 满足 Idle 契约时为 true。
bool isIdleStep(const TransportStep& step) {
    return step.kind == TransportStep::Kind::Idle && step.bytes.empty() && !step.error.has_value();
}

/// @brief 用字面量 kSlotData 构造只有一个 slot 的从站镜像。
/// @return 调用方拥有的镜像。初始四字节是 78 56 34 12，不访问串口。
SlaveImage oneSlotImage() {
    return SlaveImage{std::vector<std::uint8_t>(kSlotData, kSlotData + 4)};
}

/// @brief 两个种子 0x12345678 的序列前 8 次 next() 逐对相同。
/// @return 八个 FaultKind 两两相等时为 true。不读取系统钟。
bool sameSeedSameSequence() {
    FaultSequence left{0x12345678};
    FaultSequence right{0x12345678};
    for (int sample = 0; sample < kPairCount; ++sample) {
        if (left.next() != right.next()) {
            return fail("same_seed_same_sequence");
        }
    }
    return true;
}

/// @brief Drop 不到达从站：不成功、不交还字节、帧为空，镜像仍是 kSlotData。
/// @return 四项都成立时为 true。
bool dropLeavesImage() {
    SlaveImage image = oneSlotImage();
    const FaultReply reply = applyFault(image, kReadSlot0, FaultKind::Drop);
    if (reply.countsAsSuccess || reply.deliversBytes || !reply.frame.empty() ||
        !sameBytes(image.bytes(), kSlotData)) {
        return fail("drop_leaves_image");
    }
    return true;
}

/// @brief Disconnect 不到达从站：不成功、不交还字节、帧为空，镜像仍是 kSlotData。
/// @return 四项都成立时为 true。
bool disconnectLeavesImage() {
    SlaveImage image = oneSlotImage();
    const FaultReply reply = applyFault(image, kReadSlot0, FaultKind::Disconnect);
    if (reply.countsAsSuccess || reply.deliversBytes || !reply.frame.empty() ||
        !sameBytes(image.bytes(), kSlotData)) {
        return fail("disconnect_leaves_image");
    }
    return true;
}

/// @brief BadCrc 交还与成功响应等长但 CRC 已被破坏的帧，并且不改镜像。
/// @return countsAsSuccess 为 false、长度等于 kReadResponse、内容不同、去掉长度前缀后 verifyCrc16 的 tryError 为 CrcMismatch，镜像仍为 kSlotData 时为 true。
bool badCrcIsNotSuccess() {
    SlaveImage image = oneSlotImage();
    const FaultReply reply = applyFault(image, kReadSlot0, FaultKind::BadCrc);
    if (reply.countsAsSuccess || reply.frame.size() != sizeof(kReadResponse) ||
        sameBytes(reply.frame, kReadResponse) || !sameBytes(image.bytes(), kSlotData)) {
        return fail("bad_crc_is_not_success");
    }
    const Result<void> crc =
        verifyCrc16(std::span<const std::uint8_t>(reply.frame.data() + 1, reply.frame.size() - 1));
    const ErrorCode* crcError = crc.tryError();
    if (crc.ok() || crcError == nullptr || *crcError != ErrorCode::CrcMismatch) {
        return fail("bad_crc_is_not_success");
    }
    return true;
}

/// @brief BadFrame 交还固定短坏帧，不能当成成功响应。
/// @return countsAsSuccess 为 false、deliversBytes 为 true、帧等于 kBadFrame 且不等于 kReadResponse 时为 true。
bool badFrameIsNotSuccess() {
    SlaveImage image = oneSlotImage();
    const FaultReply reply = applyFault(image, kReadSlot0, FaultKind::BadFrame);
    if (reply.countsAsSuccess || !reply.deliversBytes || !sameBytes(reply.frame, kBadFrame) ||
        sameBytes(reply.frame, kReadResponse)) {
        return fail("bad_frame_is_not_success");
    }
    return true;
}

/// @brief Delay 的第一次 poll 空闲，第二次 poll 才交还正常读响应。不睡眠。
/// @return open 与 submit 成功，第一拍为 Idle 且字节为空，第二拍 Bytes 等于 kReadResponse 时为 true。
bool delayReleasesOnSecondPoll() {
    SlaveImage image = oneSlotImage();
    ScriptedFaultSource faults{std::vector<FaultKind>{FaultKind::Delay}};
    MemorySlaveTransport transport{image, &faults};
    const Result<void> opened = transport.open();
    if (!opened.ok() || opened.tryError() != nullptr) {
        return fail("delay_releases_on_second_poll");
    }
    const Result<void> submitted = transport.submit(kReadSlot0);
    if (!submitted.ok() || submitted.tryError() != nullptr) {
        return fail("delay_releases_on_second_poll");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("delay_releases_on_second_poll");
    }
    const TransportStep bytes = transport.poll();
    if (bytes.kind != TransportStep::Kind::Bytes || bytes.error.has_value() ||
        !sameBytes(bytes.bytes, kReadResponse)) {
        return fail("delay_releases_on_second_poll");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "same_seed_same_sequence") {
        return sameSeedSameSequence();
    }
    if (name == "drop_leaves_image") {
        return dropLeavesImage();
    }
    if (name == "disconnect_leaves_image") {
        return disconnectLeavesImage();
    }
    if (name == "bad_crc_is_not_success") {
        return badCrcIsNotSuccess();
    }
    if (name == "bad_frame_is_not_success") {
        return badFrameIsNotSuccess();
    }
    if (name == "delay_releases_on_second_poll") {
        return delayReleasesOnSecondPoll();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个从站故障单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对或用例名未知时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_simulator_fault_tests <case>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
