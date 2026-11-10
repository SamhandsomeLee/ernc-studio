#include "ernc/protocol/crc16.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::protocol::appendCrc16;
using ernc::protocol::calculateCrc16;
using ernc::protocol::verifyCrc16;

/// ASCII "123456789"。公开 CRC-16/MODBUS 校验输入，期望值 0x4B37 写在用例里。
constexpr std::uint8_t kDigits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

/// kDigits 加上小端 CRC 37 4B。期望字节是字面量，不由被测函数在测试里生成。
constexpr std::uint8_t kDigitsFrame[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', 0x37, 0x4B};

/// 从站 1、功能码 0x03、起始寄存器 0、数量 1 的本体。期望 CRC 0x0A84 是字面量。
/// 这是旧版 calc_crc16 对同一输入的输出，不是已批准的设备黄金帧。
constexpr std::uint8_t kReadBody[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};

/// kReadBody 加上小端 CRC 84 0A。
constexpr std::uint8_t kReadFrame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};

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

/// @brief 公开校验串的 CRC 数值是 0x4B37。
/// @return calculateCrc16 等于该字面量时为 true。
bool catalogue123456789() {
    if (calculateCrc16(kDigits) != 0x4B37) {
        return fail("catalogue_123456789");
    }
    return true;
}

/// @brief 附加结果的低字节在前，并且覆盖字节保持原样。
/// @return 与 kDigitsFrame 逐字节相同时为 true。
bool appendLaysLowByteFirst() {
    const std::vector<std::uint8_t> framed = appendCrc16(kDigits);
    if (!sameBytes(framed, kDigitsFrame)) {
        return fail("append_lays_low_byte_first");
    }
    return true;
}

/// @brief 读请求本体的 CRC 数值和附加字节都对照字面量。
/// @return 数值为 0x0A84 且附加结果为 kReadFrame 时为 true。
bool readBodyCrc() {
    if (calculateCrc16(kReadBody) != 0x0A84) {
        return fail("read_body_crc");
    }
    const std::vector<std::uint8_t> framed = appendCrc16(kReadBody);
    if (!sameBytes(framed, kReadFrame)) {
        return fail("read_body_crc");
    }
    return true;
}

/// @brief 字面量正确帧可以通过验证。
/// @return ok() 为 true 且 tryError() 为空时为 true。
bool literalFrameVerifies() {
    const Result<void> checked = verifyCrc16(kDigitsFrame);
    if (!checked.ok() || checked.tryError() != nullptr) {
        return fail("literal_frame_verifies");
    }
    return true;
}

/// @brief 只改 CRC 高字节时拒绝，并且错误码是 CrcMismatch。
/// @return 失败码为 CrcMismatch 时为 true。
bool tamperedCrcRejected() {
    std::uint8_t tampered[sizeof(kDigitsFrame)];
    for (std::size_t index = 0; index < sizeof(kDigitsFrame); ++index) {
        tampered[index] = kDigitsFrame[index];
    }
    tampered[sizeof(tampered) - 1] = static_cast<std::uint8_t>(tampered[sizeof(tampered) - 1] ^ 0xFFU);
    const Result<void> checked = verifyCrc16(tampered);
    const ErrorCode* error = checked.tryError();
    if (checked.ok() || error == nullptr || *error != ErrorCode::CrcMismatch) {
        return fail("tampered_crc_rejected");
    }
    return true;
}

/// @brief 只改覆盖字节、CRC 尾部不动时拒绝。
/// @return 失败码为 CrcMismatch 时为 true。
bool tamperedBodyRejected() {
    std::uint8_t tampered[sizeof(kDigitsFrame)];
    for (std::size_t index = 0; index < sizeof(kDigitsFrame); ++index) {
        tampered[index] = kDigitsFrame[index];
    }
    tampered[0] = static_cast<std::uint8_t>(tampered[0] ^ 0x01U);
    const Result<void> checked = verifyCrc16(tampered);
    const ErrorCode* error = checked.tryError();
    if (checked.ok() || error == nullptr || *error != ErrorCode::CrcMismatch) {
        return fail("tampered_body_rejected");
    }
    return true;
}

/// @brief 短于 3 字节时不把结果说成 CRC 不符。
/// @return 空、1 字节和 2 字节都得到 InvalidParameter 时为 true。
bool shorterThanThreeRejected() {
    const std::uint8_t one[] = {0x00};
    const std::uint8_t two[] = {0x00, 0x00};
    const Result<void> empty = verifyCrc16({});
    const Result<void> single = verifyCrc16(one);
    const Result<void> pair = verifyCrc16(two);
    const ErrorCode* emptyError = empty.tryError();
    const ErrorCode* singleError = single.tryError();
    const ErrorCode* pairError = pair.tryError();
    if (empty.ok() || emptyError == nullptr || *emptyError != ErrorCode::InvalidParameter) {
        return fail("shorter_than_three_rejected");
    }
    if (single.ok() || singleError == nullptr || *singleError != ErrorCode::InvalidParameter) {
        return fail("shorter_than_three_rejected");
    }
    if (pair.ok() || pairError == nullptr || *pairError != ErrorCode::InvalidParameter) {
        return fail("shorter_than_three_rejected");
    }
    return true;
}

/// @brief 空输入的附加结果是 FF FF，这两字节仍然不能通过验证。
/// @return 计算值为 0xFFFF、附加字节为 FF FF、验证错误为 InvalidParameter 时为 true。
bool emptyAppendIsNotAValidFrame() {
    if (calculateCrc16({}) != 0xFFFF) {
        return fail("empty_append_is_not_a_valid_frame");
    }
    const std::uint8_t expected[] = {0xFF, 0xFF};
    const std::vector<std::uint8_t> framed = appendCrc16({});
    if (!sameBytes(framed, expected)) {
        return fail("empty_append_is_not_a_valid_frame");
    }
    const Result<void> checked = verifyCrc16(framed);
    const ErrorCode* error = checked.tryError();
    if (checked.ok() || error == nullptr || *error != ErrorCode::InvalidParameter) {
        return fail("empty_append_is_not_a_valid_frame");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "catalogue_123456789") {
        return catalogue123456789();
    }
    if (name == "append_lays_low_byte_first") {
        return appendLaysLowByteFirst();
    }
    if (name == "read_body_crc") {
        return readBodyCrc();
    }
    if (name == "literal_frame_verifies") {
        return literalFrameVerifies();
    }
    if (name == "tampered_crc_rejected") {
        return tamperedCrcRejected();
    }
    if (name == "tampered_body_rejected") {
        return tamperedBodyRejected();
    }
    if (name == "shorter_than_three_rejected") {
        return shorterThanThreeRejected();
    }
    if (name == "empty_append_is_not_a_valid_frame") {
        return emptyAppendIsNotAValidFrame();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个 CRC16 单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_crc16_tests <case>\n";
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
