#include "ernc/protocol/wire_encoder.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::protocol::encodeLengthPrefixed;

/// 已带字面量 CRC 的 8 字节 Modbus 本体。长度前缀应为 0x08，不由编码器重新计算 CRC。
constexpr std::uint8_t kReadFrame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};

/// kReadFrame 加上长度字节后的整帧。
constexpr std::uint8_t kReadWire[] = {0x08, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};

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

/// @brief 8 字节本体编码成 0x08 加原字节。
/// @return 输出与 kReadWire 逐字节相同时为 true。
bool eightByteFramePrefixed() {
    const Result<std::vector<std::uint8_t>> encoded = encodeLengthPrefixed(kReadFrame);
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr || !sameBytes(*wire, kReadWire)) {
        return fail("eight_byte_frame_prefixed");
    }
    return true;
}

/// @brief 255 字节本体可以编码，首字节为 0xFF，后面原样复制，没有填充。
/// @return 输出长度是 256 且载荷未被改写时为 true。
bool maxFrameEncodes() {
    std::vector<std::uint8_t> body(255);
    for (std::size_t index = 0; index < body.size(); ++index) {
        body[index] = static_cast<std::uint8_t>(index);
    }
    const Result<std::vector<std::uint8_t>> encoded = encodeLengthPrefixed(body);
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr || wire->size() != 256 || (*wire)[0] != 0xFF) {
        return fail("max_frame_encodes");
    }
    for (std::size_t index = 0; index < body.size(); ++index) {
        if ((*wire)[index + 1] != body[index]) {
            return fail("max_frame_encodes");
        }
    }
    return true;
}

/// @brief 256 字节本体被拒绝，并且没有成功载荷。
/// @return 错误码为 FrameLengthInvalid 时为 true。
bool overMaxRejected() {
    const std::vector<std::uint8_t> body(256, 0x11);
    const Result<std::vector<std::uint8_t>> encoded = encodeLengthPrefixed(body);
    const ErrorCode* error = encoded.tryError();
    if (encoded.ok() || encoded.tryValue() != nullptr || error == nullptr
        || *error != ErrorCode::FrameLengthInvalid) {
        return fail("over_max_rejected");
    }
    return true;
}

/// @brief 空本体编码成单字节 0x00。
/// @return 输出只有一个 0x00 时为 true。
bool emptyFrameIsZeroLength() {
    const Result<std::vector<std::uint8_t>> encoded = encodeLengthPrefixed({});
    const std::uint8_t expected[] = {0x00};
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr || !sameBytes(*wire, expected)) {
        return fail("empty_frame_is_zero_length");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "eight_byte_frame_prefixed") {
        return eightByteFramePrefixed();
    }
    if (name == "max_frame_encodes") {
        return maxFrameEncodes();
    }
    if (name == "over_max_rejected") {
        return overMaxRejected();
    }
    if (name == "empty_frame_is_zero_length") {
        return emptyFrameIsZeroLength();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个长度前缀编码单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_wire_encoder_tests <case>\n";
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
