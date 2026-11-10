#include "ernc/protocol/crc16.hpp"
#include "ernc/protocol/modbus_fc16.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::protocol::Fc16Ack;
using ernc::protocol::Fc16Exception;
using ernc::protocol::Fc16Response;
using ernc::protocol::Fc16WriteRequest;
using ernc::protocol::decodeFc16Ack;
using ernc::protocol::encodeFc16Write;
using ernc::protocol::kConfirmedSlaveAddress;

/// 从站 0x01、slot 0、1 个 slot、数据 4 个 0。寄存器数量大端为 00 02，字节数为 04。
constexpr std::uint8_t kOneSlotZeros[] = {0x0D, 0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0xF3, 0xAF};

/// slot 1，数据区保持 78 56 34 12。起始寄存器大端为 00 02。
constexpr std::uint8_t kSlotOnePayload[] = {0x0D, 0x01, 0x10, 0x00, 0x02, 0x00, 0x02, 0x04, 0x78, 0x56, 0x34, 0x12, 0x1C, 0x0B};

/// slot 0、1 个 slot 的 ACK，不含长度字节。数量是 00 02。
constexpr std::uint8_t kAckOneSlot[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x41, 0xC8};

/// 从站 0x01、异常码 0x03 的 FC16 异常帧。
constexpr std::uint8_t kExceptionFrame[] = {0x01, 0x90, 0x03, 0x0C, 0x01};

constexpr std::uint8_t kFourZeros[] = {0x00, 0x00, 0x00, 0x00};
constexpr std::uint8_t kLePattern[] = {0x78, 0x56, 0x34, 0x12};

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

/// @brief 组帧被拒绝且没有线字节。
/// @param name 失败时打印的用例名。
/// @param request 写入范围，只在本次调用期间借用。
/// @param payload 数据区，只在本次调用期间借用。
/// @param expected 期望的错误码。
/// @return 错误码匹配且没有成功载荷时为 true。
bool expectEncodeError(const char* name, const Fc16WriteRequest& request, std::span<const std::uint8_t> payload, ErrorCode expected) {
    const Result<std::vector<std::uint8_t>> encoded = encodeFc16Write(request, payload);
    const ErrorCode* error = encoded.tryError();
    if (encoded.ok() || encoded.tryValue() != nullptr || error == nullptr || *error != expected) {
        return fail(name);
    }
    return true;
}

/// @brief 1 个 slot 的全零数据与字面量整帧一致。
/// @return 默认从站为 0x01 且输出匹配 kOneSlotZeros 时为 true。
bool oneSlotWrite() {
    const Result<std::vector<std::uint8_t>> encoded = encodeFc16Write(Fc16WriteRequest{0, 1}, kFourZeros);
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr || !sameBytes(*wire, kOneSlotZeros)) {
        return fail("one_slot_write");
    }
    return true;
}

/// @brief 数据区按给定顺序写入，起始寄存器用大端。
/// @return 整帧匹配 kSlotOnePayload 时为 true。
bool payloadBytesKeepOrder() {
    const Result<std::vector<std::uint8_t>> encoded = encodeFc16Write(Fc16WriteRequest{1, 1}, kLePattern);
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr || !sameBytes(*wire, kSlotOnePayload)) {
        return fail("payload_bytes_keep_order");
    }
    return true;
}

/// @brief 核对批量写的长度、大端数量、字节数和 CRC。
/// @param name 失败时打印的用例名。
/// @param slotCount 60 或 61。
/// @param lengthByte 期望的长度字节。
/// @param quantityHi 寄存器数量的高字节。0 表示数量小于 256。
/// @param quantityLo 寄存器数量的低字节。
/// @param byteCount 数据区字节数。
/// @return 帧头和 CRC 都符合时为 true。
bool expectBulk(const char* name, std::uint16_t slotCount, std::uint8_t lengthByte, std::uint8_t quantityHi, std::uint8_t quantityLo,
                std::uint8_t byteCount) {
    const std::vector<std::uint8_t> payload(byteCount, 0x00);
    const Result<std::vector<std::uint8_t>> encoded = encodeFc16Write(Fc16WriteRequest{0, slotCount}, payload);
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr || wire->size() != static_cast<std::size_t>(lengthByte) + 1) {
        return fail(name);
    }
    if ((*wire)[0] != lengthByte || (*wire)[1] != kConfirmedSlaveAddress || (*wire)[2] != 0x10 || (*wire)[3] != 0x00
        || (*wire)[4] != 0x00 || (*wire)[5] != quantityHi || (*wire)[6] != quantityLo || (*wire)[7] != byteCount) {
        return fail(name);
    }
    for (std::size_t index = 8; index + 2 < wire->size(); ++index) {
        if ((*wire)[index] != 0x00) {
            return fail(name);
        }
    }
    const Result<void> crc = ernc::protocol::verifyCrc16(std::span<const std::uint8_t>(*wire).subspan(1));
    if (!crc.ok()) {
        return fail(name);
    }
    return true;
}

/// @brief 60 个 slot 可以编码。寄存器数量是 0x0078，数据字节数是 240。
/// @return 帧头和 CRC 通过时为 true。
bool sixtySlotWrite() {
    return expectBulk("sixty_slot_write", 60, 0xF9, 0x00, 0x78, 0xF0);
}

/// @brief 61 个 slot 可以编码。寄存器数量是 0x007A，数据字节数是 244。
/// @return 帧头和 CRC 通过时为 true。
bool sixtyOneSlotWrite() {
    return expectBulk("sixty_one_slot_write", 61, 0xFD, 0x00, 0x7A, 0xF4);
}

/// @brief 62 个 slot 在组帧前被拒绝。
/// @return 错误码为 InvalidParameter 时为 true。
bool sixtyTwoSlotsRejected() {
    const std::vector<std::uint8_t> payload(62 * 4, 0x00);
    return expectEncodeError("sixty_two_slots_rejected", Fc16WriteRequest{0, 62}, payload, ErrorCode::InvalidParameter);
}

/// @brief 0 个 slot 不组帧。
/// @return 错误码为 InvalidParameter 时为 true。
bool zeroSlotsRejected() {
    return expectEncodeError("zero_slots_rejected", Fc16WriteRequest{0, 0}, {}, ErrorCode::InvalidParameter);
}

/// @brief 数据长度不是 slot 数乘 4。
/// @return 错误码为 PayloadSizeMismatch 时为 true。
bool payloadSizeRejected() {
    const std::uint8_t shortPayload[] = {0x00, 0x00, 0x00};
    return expectEncodeError("payload_size_rejected", Fc16WriteRequest{0, 1}, shortPayload, ErrorCode::PayloadSizeMismatch);
}

/// @brief 起始寄存器超出 16 位时拒绝。
/// @return slot 32768 的错误码为 InvalidParameter 时为 true。
bool registerSpanRejected() {
    return expectEncodeError("register_span_rejected", Fc16WriteRequest{32768, 1}, kFourZeros, ErrorCode::InvalidParameter);
}

/// @brief 显式传入的从站地址写到帧里。0x02 不是第二个已确认设备地址。
/// @return 从站字节为 0x02 且 CRC 通过时为 true。
bool slaveArgumentIsWritten() {
    const Result<std::vector<std::uint8_t>> encoded = encodeFc16Write(Fc16WriteRequest{0, 1, 0x02}, kFourZeros);
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr || wire->size() != 14 || (*wire)[1] != 0x02 || (*wire)[2] != 0x10) {
        return fail("slave_argument_is_written");
    }
    const Result<void> crc = ernc::protocol::verifyCrc16(std::span<const std::uint8_t>(*wire).subspan(1));
    if (!crc.ok()) {
        return fail("slave_argument_is_written");
    }
    return true;
}

/// @brief 地址和数量一致的 ACK 只返回回显字段。
/// @return 从站 0x01、起始寄存器 0、寄存器数量 2 时为 true。
bool ackMatchesAddressAndCount() {
    const Result<Fc16Response> decoded = decodeFc16Ack(Fc16WriteRequest{0, 1}, kAckOneSlot);
    const Fc16Response* response = decoded.tryValue();
    const Fc16Ack* ack = response == nullptr ? nullptr : std::get_if<Fc16Ack>(response);
    if (!decoded.ok() || ack == nullptr || ack->slaveAddress != 0x01 || ack->startRegister != 0 || ack->registerCount != 2) {
        return fail("ack_matches_address_and_count");
    }
    return true;
}

/// @brief ACK 的寄存器数量和请求不一致。
/// @return 错误码为 ResponseMismatch 时为 true。
bool ackCountMismatch() {
    const Result<Fc16Response> decoded = decodeFc16Ack(Fc16WriteRequest{0, 2}, kAckOneSlot);
    const ErrorCode* error = decoded.tryError();
    if (decoded.ok() || error == nullptr || *error != ErrorCode::ResponseMismatch) {
        return fail("ack_count_mismatch");
    }
    return true;
}

/// @brief ACK 的起始寄存器和请求不一致。
/// @return 错误码为 ResponseMismatch 时为 true。
bool ackAddressMismatch() {
    const Result<Fc16Response> decoded = decodeFc16Ack(Fc16WriteRequest{1, 1}, kAckOneSlot);
    const ErrorCode* error = decoded.tryError();
    if (decoded.ok() || error == nullptr || *error != ErrorCode::ResponseMismatch) {
        return fail("ack_address_mismatch");
    }
    return true;
}

/// @brief 异常码留在结果里。
/// @return 异常码为 0x03 时为 true。
bool exceptionCodeIsKept() {
    const Result<Fc16Response> decoded = decodeFc16Ack(Fc16WriteRequest{0, 1}, kExceptionFrame);
    const Fc16Response* response = decoded.tryValue();
    const Fc16Exception* exception = response == nullptr ? nullptr : std::get_if<Fc16Exception>(response);
    if (!decoded.ok() || exception == nullptr || exception->slaveAddress != 0x01 || exception->code != 0x03) {
        return fail("exception_code_is_kept");
    }
    return true;
}

/// @brief 篡改 ACK 的 CRC 后不返回回显。
/// @return 错误码为 CrcMismatch 时为 true。
bool tamperedCrcRejected() {
    std::uint8_t tampered[sizeof(kAckOneSlot)];
    for (std::size_t index = 0; index < sizeof(kAckOneSlot); ++index) {
        tampered[index] = kAckOneSlot[index];
    }
    tampered[sizeof(tampered) - 1] = static_cast<std::uint8_t>(tampered[sizeof(tampered) - 1] ^ 0xFFU);
    const Result<Fc16Response> decoded = decodeFc16Ack(Fc16WriteRequest{0, 1}, tampered);
    const ErrorCode* error = decoded.tryError();
    if (decoded.ok() || decoded.tryValue() != nullptr || error == nullptr || *error != ErrorCode::CrcMismatch) {
        return fail("tampered_crc_rejected");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "one_slot_write") {
        return oneSlotWrite();
    }
    if (name == "payload_bytes_keep_order") {
        return payloadBytesKeepOrder();
    }
    if (name == "sixty_slot_write") {
        return sixtySlotWrite();
    }
    if (name == "sixty_one_slot_write") {
        return sixtyOneSlotWrite();
    }
    if (name == "sixty_two_slots_rejected") {
        return sixtyTwoSlotsRejected();
    }
    if (name == "zero_slots_rejected") {
        return zeroSlotsRejected();
    }
    if (name == "payload_size_rejected") {
        return payloadSizeRejected();
    }
    if (name == "register_span_rejected") {
        return registerSpanRejected();
    }
    if (name == "slave_argument_is_written") {
        return slaveArgumentIsWritten();
    }
    if (name == "ack_matches_address_and_count") {
        return ackMatchesAddressAndCount();
    }
    if (name == "ack_count_mismatch") {
        return ackCountMismatch();
    }
    if (name == "ack_address_mismatch") {
        return ackAddressMismatch();
    }
    if (name == "exception_code_is_kept") {
        return exceptionCodeIsKept();
    }
    if (name == "tampered_crc_rejected") {
        return tamperedCrcRejected();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个 FC16 单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_modbus_fc16_tests <case>\n";
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
