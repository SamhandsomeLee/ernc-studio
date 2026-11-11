#include "slave_image.hpp"
#include "memory_transport.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::ports::TransportStep;
using ernc::simulator::MemorySlaveTransport;
using ernc::simulator::SlaveImage;
using ernc::simulator::SlaveReply;
using ernc::simulator::exchange;

/// 读 slot 0 的两寄存器请求。长度前缀和 CRC 都写在字面量里，不由编码器生成。
constexpr std::uint8_t kReadSlot0[] = {0x08, 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

/// 上面对应的 FC03 响应。下标 4 起的 4 字节是 slot 0 的数据区。
constexpr std::uint8_t kReadResponse[] = {0x09, 0x01, 0x03, 0x04, 0x78, 0x56, 0x34, 0x12, 0x94, 0x4E};

/// 单 slot 镜像的初始字节。一个逻辑 slot 是 4 字节。
constexpr std::uint8_t kSlotData[] = {0x78, 0x56, 0x34, 0x12};

/// 把 slot 0 写成 11 22 33 44 的 FC16 请求。长度前缀和 CRC 都是字面量。
constexpr std::uint8_t kWriteSlot0[] = {0x0D, 0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x11, 0x22, 0x33, 0x44, 0x42, 0x5A};

/// 写入成功后 slot 0 应保存的 4 字节。
constexpr std::uint8_t kWritten[] = {0x11, 0x22, 0x33, 0x44};

/// 从站地址为 0x02 的读请求。本从站只接受 0x01。
constexpr std::uint8_t kWrongSlave[] = {0x08, 0x02, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x38};

/// 一次读取 0x7E 个寄存器，即 63 个 slot，超过 FC03 的 62 slot 上限。
constexpr std::uint8_t kRead63Slots[] = {0x08, 0x01, 0x03, 0x00, 0x00, 0x00, 0x7E, 0xC5, 0xEA};

/// 读取 slot 1。单 slot 镜像里没有这个范围。
constexpr std::uint8_t kReadSlot1[] = {0x08, 0x01, 0x03, 0x00, 0x02, 0x00, 0x02, 0x65, 0xCB};

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
/// @param step 被检查的步骤，按值传入，调用方仍拥有原对象。
/// @return 满足 Idle 契约时为 true。
bool isIdleStep(const TransportStep& step) {
    return step.kind == TransportStep::Kind::Idle && step.bytes.empty() && !step.error.has_value();
}

/// @brief 用字面量 kSlotData 构造只有一个 slot 的从站镜像。
/// @return 调用方拥有的镜像。初始四字节是 78 56 34 12，不访问串口。
SlaveImage oneSlotImage() {
    return SlaveImage{std::vector<std::uint8_t>(kSlotData, kSlotData + 4)};
}

/// @brief 比较 FC03 响应里一个 slot 的数据区。
/// @param frame 带长度前缀的响应，只在本次调用期间借用。数据区从下标 4 开始，长度 4 字节。
/// @param expected 期望的 4 字节 slot 内容，只在本次调用期间借用。
/// @return 帧至少有 8 字节且这 4 字节与 expected 相同时为 true。
bool oneSlotDataArea(std::span<const std::uint8_t> frame, std::span<const std::uint8_t> expected) {
    if (frame.size() < 8U || expected.size() != 4U) {
        return false;
    }
    return sameBytes(frame.subspan(4U, 4U), expected);
}

/// @brief 在一份新的单 slot 镜像上交换一帧，检查拒绝且镜像不变。
/// @param request 带长度前缀的请求，只在本次调用期间借用。
/// @return responded 为 false、响应帧为空、镜像字节仍为 kSlotData 时为 true。
bool rejectedWithoutTouchingImage(std::span<const std::uint8_t> request) {
    SlaveImage image = oneSlotImage();
    const SlaveReply reply = exchange(image, request);
    return !reply.responded && reply.frame.empty() && sameBytes(image.bytes(), kSlotData);
}

/// @brief 读已植入的 slot 0，响应等于字面量，镜像不被读操作改写。
/// @return responded 为 true、帧为 kReadResponse、镜像仍为 kSlotData 时为 true。
bool readReturnsPlantedBytes() {
    SlaveImage image = oneSlotImage();
    const SlaveReply reply = exchange(image, kReadSlot0);
    if (!reply.responded || !sameBytes(reply.frame, kReadResponse) || !sameBytes(image.bytes(), kSlotData)) {
        return fail("read_returns_planted_bytes");
    }
    return true;
}

/// @brief 先写入新的 4 字节，再读回同一 slot，数据区应是刚写入的字节。
/// @return 写响应非空、镜像变为 kWritten，随后读响应下标 4 的 4 字节也是 kWritten 时为 true。
bool writeThenReadShowsNewBytes() {
    SlaveImage image = oneSlotImage();
    const SlaveReply written = exchange(image, kWriteSlot0);
    if (!written.responded || written.frame.empty() || !sameBytes(image.bytes(), kWritten)) {
        return fail("write_then_read_shows_new_bytes");
    }
    const SlaveReply readBack = exchange(image, kReadSlot0);
    if (!readBack.responded || !oneSlotDataArea(readBack.frame, kWritten)) {
        return fail("write_then_read_shows_new_bytes");
    }
    return true;
}

/// @brief 错误从站、63 slot 读取和越界 slot 都不改镜像。
/// @return 三次交换各自拒绝、响应为空、镜像仍为 kSlotData 时为 true。
bool illegalRequestLeavesImage() {
    if (!rejectedWithoutTouchingImage(kWrongSlave) || !rejectedWithoutTouchingImage(kRead63Slots) ||
        !rejectedWithoutTouchingImage(kReadSlot1)) {
        return fail("illegal_request_leaves_image");
    }
    return true;
}

/// @brief 相同初始镜像上的两次相同读请求得到相同响应。
/// @return 两次 responded 都为 true 且两帧逐字节相同时为 true。
bool sameRequestIsDeterministic() {
    SlaveImage first = oneSlotImage();
    SlaveImage second = oneSlotImage();
    const SlaveReply left = exchange(first, kReadSlot0);
    const SlaveReply right = exchange(second, kReadSlot0);
    if (!left.responded || !right.responded || !sameBytes(left.frame, right.frame)) {
        return fail("same_request_is_deterministic");
    }
    return true;
}

/// @brief 关闭时提交被拒绝；打开后同一读请求经 poll 交还字面量响应，下一拍为空闲。
/// @return 关闭提交为 DeviceDisconnected，打开后的 Bytes 等于 kReadResponse，再 poll 为 Idle 时为 true。
bool memoryTransportPollsResponse() {
    SlaveImage image = oneSlotImage();
    MemorySlaveTransport transport{image};
    const Result<void> closedSubmit = transport.submit(kReadSlot0);
    const ErrorCode* closedError = closedSubmit.tryError();
    if (closedSubmit.ok() || closedError == nullptr || *closedError != ErrorCode::DeviceDisconnected) {
        return fail("memory_transport_polls_response");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("memory_transport_polls_response");
    }
    const Result<void> opened = transport.open();
    if (!opened.ok() || opened.tryError() != nullptr) {
        return fail("memory_transport_polls_response");
    }
    const Result<void> submitted = transport.submit(kReadSlot0);
    if (!submitted.ok() || submitted.tryError() != nullptr) {
        return fail("memory_transport_polls_response");
    }
    const TransportStep bytes = transport.poll();
    if (bytes.kind != TransportStep::Kind::Bytes || bytes.error.has_value() ||
        !sameBytes(bytes.bytes, kReadResponse)) {
        return fail("memory_transport_polls_response");
    }
    if (!isIdleStep(transport.poll())) {
        return fail("memory_transport_polls_response");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "read_returns_planted_bytes") {
        return readReturnsPlantedBytes();
    }
    if (name == "write_then_read_shows_new_bytes") {
        return writeThenReadShowsNewBytes();
    }
    if (name == "illegal_request_leaves_image") {
        return illegalRequestLeavesImage();
    }
    if (name == "same_request_is_deterministic") {
        return sameRequestIsDeterministic();
    }
    if (name == "memory_transport_polls_response") {
        return memoryTransportPollsResponse();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个从站镜像单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_simulator_slave_tests <case>\n";
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
