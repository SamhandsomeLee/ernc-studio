#include "ernc/protocol/crc16.hpp"
#include "ernc/protocol/modbus_fc03.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::protocol::Fc03Exception;
using ernc::protocol::Fc03Payload;
using ernc::protocol::Fc03ReadRequest;
using ernc::protocol::Fc03Response;
using ernc::protocol::decodeFc03Response;
using ernc::protocol::encodeFc03Read;
using ernc::protocol::kConfirmedSlaveAddress;

/// 从站 0x01、slot 0、1 个 slot。寄存器数量是 2，大端写成 00 02。CRC 是字面量。
constexpr std::uint8_t kOneSlot[] = {0x08, 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

/// 60 个 slot。寄存器数量 120，大端写成 00 78。
constexpr std::uint8_t kSixtySlots[] = {0x08, 0x01, 0x03, 0x00, 0x00, 0x00, 0x78, 0x45, 0xE8};

/// 62 个 slot。寄存器数量 124，大端写成 00 7C。
constexpr std::uint8_t kSixtyTwoSlots[] = {0x08, 0x01, 0x03, 0x00, 0x00, 0x00, 0x7C, 0x44, 0x2B};

/// slot 1。起始寄存器是 2，大端写成 00 02。
constexpr std::uint8_t kSlotOne[] = {0x08, 0x01, 0x03, 0x00, 0x02, 0x00, 0x02, 0x65, 0xCB};

/// 数据区 78 56 34 12 的正常响应，不含长度字节。字节顺序保持这条线上的顺序。
constexpr std::uint8_t kPayloadFrame[] = {0x01, 0x03, 0x04, 0x78, 0x56, 0x34, 0x12, 0x94, 0x4E};

/// 声明数据有 4 字节，实际只有 3 字节。CRC 与这组非法字节匹配，用来走到长度检查。
constexpr std::uint8_t kShortPayload[] = {0x01, 0x03, 0x04, 0x11, 0x22, 0x33, 0x4C, 0x4A};

/// 功能码 0x10 的帧。FC03 解码器不接受它。
constexpr std::uint8_t kOtherFunction[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x41, 0xC8};

/// 从站 0x01、异常码 0x02。
constexpr std::uint8_t kExceptionFrame[] = {0x01, 0x83, 0x02, 0xC0, 0xF1};

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

/// @brief 编码成功且输出与字面量一致。
/// @param name 失败时打印的用例名。
/// @param request 读取参数，只在本次调用期间借用。
/// @param expected 期望的整帧线字节。
/// @return 成功且逐字节相同时为 true。
bool expectWire(const char* name, const Fc03ReadRequest& request, std::span<const std::uint8_t> expected) {
    const Result<std::vector<std::uint8_t>> encoded = encodeFc03Read(request);
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr || !sameBytes(*wire, expected)) {
        return fail(name);
    }
    return true;
}

/// @brief 组帧前拒绝，并且没有线字节。
/// @param name 失败时打印的用例名。
/// @param request 非法读取参数，只在本次调用期间借用。
/// @return 错误码为 InvalidParameter 时为 true。
bool expectRejected(const char* name, const Fc03ReadRequest& request) {
    const Result<std::vector<std::uint8_t>> encoded = encodeFc03Read(request);
    const ErrorCode* error = encoded.tryError();
    if (encoded.ok() || encoded.tryValue() != nullptr || error == nullptr || *error != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    return true;
}

/// @brief 默认从站 0x01、1 个 slot 的请求与字面量一致。
/// @return 整帧匹配 kOneSlot 时为 true。
bool oneSlotRequest() {
    return expectWire("one_slot_request", Fc03ReadRequest{0, 1}, kOneSlot);
}

/// @brief 60 个 slot 的寄存器数量按大端写成 0x0078。
/// @return 整帧匹配 kSixtySlots 时为 true。
bool sixtySlotRequest() {
    return expectWire("sixty_slot_request", Fc03ReadRequest{0, 60}, kSixtySlots);
}

/// @brief 62 个 slot 的寄存器数量按大端写成 0x007C。
/// @return 整帧匹配 kSixtyTwoSlots 时为 true。
bool sixtyTwoSlotRequest() {
    return expectWire("sixty_two_slot_request", Fc03ReadRequest{0, 62}, kSixtyTwoSlots);
}

/// @brief slot 1 的起始寄存器按大端写成 0x0002。
/// @return 整帧匹配 kSlotOne 时为 true。
bool slotOneRegisterIsBigEndian() {
    return expectWire("slot_one_register_is_big_endian", Fc03ReadRequest{1, 1}, kSlotOne);
}

/// @brief 最后一个仍能放进 16 位地址的 1 slot 读取使用大端寄存器地址。
/// @return 起始寄存器字节是 FF FE、数量是 00 02，且内层 CRC 通过时为 true。
bool lastRegisterIsBigEndian() {
    const Result<std::vector<std::uint8_t>> encoded = encodeFc03Read(Fc03ReadRequest{32767, 1});
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr || wire->size() != 9) {
        return fail("last_register_is_big_endian");
    }
    if ((*wire)[0] != 0x08 || (*wire)[1] != kConfirmedSlaveAddress || (*wire)[2] != 0x03 || (*wire)[3] != 0xFF
        || (*wire)[4] != 0xFE || (*wire)[5] != 0x00 || (*wire)[6] != 0x02) {
        return fail("last_register_is_big_endian");
    }
    const Result<void> crc = ernc::protocol::verifyCrc16(std::span<const std::uint8_t>(*wire).subspan(1));
    if (!crc.ok()) {
        return fail("last_register_is_big_endian");
    }
    return true;
}

/// @brief 0 个 slot 不组帧。
/// @return 错误码为 InvalidParameter 时为 true。
bool zeroSlotsRejected() {
    return expectRejected("zero_slots_rejected", Fc03ReadRequest{0, 0});
}

/// @brief 63 个 slot 在产生线字节前被拒绝。
/// @return 错误码为 InvalidParameter 时为 true。
bool sixtyThreeSlotsRejected() {
    return expectRejected("sixty_three_slots_rejected", Fc03ReadRequest{0, 63});
}

/// @brief 起始寄存器超出 16 位时拒绝。
/// @return slot 32768 的错误码为 InvalidParameter 时为 true。
bool registerSpanRejected() {
    return expectRejected("register_span_rejected", Fc03ReadRequest{32768, 1});
}

/// @brief 显式传入的从站地址写到帧里。0x02 不是第二个已确认设备地址。
/// @return 从站字节为 0x02、寄存器字段仍是大端，且 CRC 通过时为 true。
bool slaveArgumentIsWritten() {
    const Fc03ReadRequest request{0, 1, 0x02};
    const Result<std::vector<std::uint8_t>> encoded = encodeFc03Read(request);
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr || wire->size() != 9) {
        return fail("slave_argument_is_written");
    }
    if ((*wire)[0] != 0x08 || (*wire)[1] != 0x02 || (*wire)[2] != 0x03 || (*wire)[3] != 0x00 || (*wire)[4] != 0x00
        || (*wire)[5] != 0x00 || (*wire)[6] != 0x02) {
        return fail("slave_argument_is_written");
    }
    const Result<void> crc = ernc::protocol::verifyCrc16(std::span<const std::uint8_t>(*wire).subspan(1));
    if (!crc.ok()) {
        return fail("slave_argument_is_written");
    }
    return true;
}

/// @brief 响应数据区保持 78 56 34 12，不改成数值后的另一种字节序。
/// @return 从站、slot 数和原始字节都匹配时为 true。
bool payloadBytesKeepWireOrder() {
    const Result<Fc03Response> decoded = decodeFc03Response(kPayloadFrame);
    const Fc03Response* response = decoded.tryValue();
    const Fc03Payload* payload = response == nullptr ? nullptr : std::get_if<Fc03Payload>(response);
    const std::uint8_t expected[] = {0x78, 0x56, 0x34, 0x12};
    if (!decoded.ok() || payload == nullptr || payload->slaveAddress != 0x01 || payload->slotCount != 1
        || !sameBytes(payload->bytes, expected)) {
        return fail("payload_bytes_keep_wire_order");
    }
    return true;
}

/// @brief 异常码留在解码结果里，而不是收成没有代码的错误。
/// @return 从站为 0x01 且异常码为 0x02 时为 true。
bool exceptionCodeIsKept() {
    const Result<Fc03Response> decoded = decodeFc03Response(kExceptionFrame);
    const Fc03Response* response = decoded.tryValue();
    const Fc03Exception* exception = response == nullptr ? nullptr : std::get_if<Fc03Exception>(response);
    if (!decoded.ok() || exception == nullptr || exception->slaveAddress != 0x01 || exception->code != 0x02) {
        return fail("exception_code_is_kept");
    }
    return true;
}

/// @brief 篡改 CRC 后不返回载荷。
/// @return 错误码为 CrcMismatch 时为 true。
bool tamperedCrcRejected() {
    std::uint8_t tampered[sizeof(kPayloadFrame)];
    for (std::size_t index = 0; index < sizeof(kPayloadFrame); ++index) {
        tampered[index] = kPayloadFrame[index];
    }
    tampered[sizeof(tampered) - 1] = static_cast<std::uint8_t>(tampered[sizeof(tampered) - 1] ^ 0xFFU);
    const Result<Fc03Response> decoded = decodeFc03Response(tampered);
    const ErrorCode* error = decoded.tryError();
    if (decoded.ok() || decoded.tryValue() != nullptr || error == nullptr || *error != ErrorCode::CrcMismatch) {
        return fail("tampered_crc_rejected");
    }
    return true;
}

/// @brief 字节数和后面的数据长度不一致。
/// @return 错误码为 PayloadSizeMismatch 时为 true。
bool byteCountMismatchRejected() {
    const Result<Fc03Response> decoded = decodeFc03Response(kShortPayload);
    const ErrorCode* error = decoded.tryError();
    if (decoded.ok() || error == nullptr || *error != ErrorCode::PayloadSizeMismatch) {
        return fail("byte_count_mismatch_rejected");
    }
    return true;
}

/// @brief 63 个 slot 的响应数据区也被拒绝。
/// @return 错误码为 PayloadSizeMismatch 时为 true。
bool responseSixtyThreeRejected() {
    std::vector<std::uint8_t> pdu{0x01, 0x03, 252};
    pdu.insert(pdu.end(), 252, 0x00);
    const std::vector<std::uint8_t> frame = ernc::protocol::appendCrc16(pdu);
    const Result<Fc03Response> decoded = decodeFc03Response(frame);
    const ErrorCode* error = decoded.tryError();
    if (decoded.ok() || error == nullptr || *error != ErrorCode::PayloadSizeMismatch) {
        return fail("response_sixty_three_rejected");
    }
    return true;
}

/// @brief 其他功能码不被当成 FC03 响应。
/// @return 错误码为 InvalidParameter 时为 true。
bool otherFunctionRejected() {
    const Result<Fc03Response> decoded = decodeFc03Response(kOtherFunction);
    const ErrorCode* error = decoded.tryError();
    if (decoded.ok() || error == nullptr || *error != ErrorCode::InvalidParameter) {
        return fail("other_function_rejected");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "one_slot_request") {
        return oneSlotRequest();
    }
    if (name == "sixty_slot_request") {
        return sixtySlotRequest();
    }
    if (name == "sixty_two_slot_request") {
        return sixtyTwoSlotRequest();
    }
    if (name == "slot_one_register_is_big_endian") {
        return slotOneRegisterIsBigEndian();
    }
    if (name == "last_register_is_big_endian") {
        return lastRegisterIsBigEndian();
    }
    if (name == "zero_slots_rejected") {
        return zeroSlotsRejected();
    }
    if (name == "sixty_three_slots_rejected") {
        return sixtyThreeSlotsRejected();
    }
    if (name == "register_span_rejected") {
        return registerSpanRejected();
    }
    if (name == "slave_argument_is_written") {
        return slaveArgumentIsWritten();
    }
    if (name == "payload_bytes_keep_wire_order") {
        return payloadBytesKeepWireOrder();
    }
    if (name == "exception_code_is_kept") {
        return exceptionCodeIsKept();
    }
    if (name == "tampered_crc_rejected") {
        return tamperedCrcRejected();
    }
    if (name == "byte_count_mismatch_rejected") {
        return byteCountMismatchRejected();
    }
    if (name == "response_sixty_three_rejected") {
        return responseSixtyThreeRejected();
    }
    if (name == "other_function_rejected") {
        return otherFunctionRejected();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个 FC03 单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_modbus_fc03_tests <case>\n";
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
