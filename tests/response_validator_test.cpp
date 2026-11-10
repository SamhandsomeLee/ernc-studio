#include "ernc/protocol/crc16.hpp"
#include "ernc/protocol/response_validator.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::protocol::AcceptedRead;
using ernc::protocol::AcceptedResponse;
using ernc::protocol::AcceptedWriteAck;
using ernc::protocol::CommandExpectation;
using ernc::protocol::CommandKind;
using ernc::protocol::ResponseVerdict;
using ernc::protocol::kConfirmedSlaveAddress;
using ernc::protocol::validateResponse;

/// 1 个 slot 的 FC03 响应，数据为 78 56 34 12。不含长度字节。
constexpr std::uint8_t kReadInner[] = {0x01, 0x03, 0x04, 0x78, 0x56, 0x34, 0x12, 0x94, 0x4E};

/// slot 0、1 个 slot 的 FC16 ACK。不含长度字节。
constexpr std::uint8_t kWriteAckInner[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02, 0x41, 0xC8};

/// 从站 0x01、异常码 0x02 的 FC03 异常帧。
constexpr std::uint8_t kReadExceptionInner[] = {0x01, 0x83, 0x02, 0xC0, 0xF1};

/// 从站 0x01、异常码 0x03 的 FC16 异常帧。
constexpr std::uint8_t kWriteExceptionInner[] = {0x01, 0x90, 0x03, 0x0C, 0x01};

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 在内层帧前加上等于其长度的前缀。
/// @param inner Modbus RTU 本体，只在本次调用期间借用。长度必须不超过 255。
/// @return 调用方拥有的整帧。
std::vector<std::uint8_t> withLength(std::span<const std::uint8_t> inner) {
    std::vector<std::uint8_t> wire;
    wire.reserve(inner.size() + 1);
    wire.push_back(static_cast<std::uint8_t>(inner.size()));
    wire.insert(wire.end(), inner.begin(), inner.end());
    return wire;
}

/// @brief 做成一条使用已确认从站地址的命令。
/// @param kind 读或写。
/// @param firstSlot 起始逻辑 slot。
/// @param slotCount slot 数。
/// @return 从站地址为 0x01 的期望。
CommandExpectation command(CommandKind kind, std::uint16_t firstSlot, std::uint16_t slotCount) {
    return CommandExpectation{kind, firstSlot, slotCount, kConfirmedSlaveAddress};
}

/// @brief 拒绝结论的错误码是否符合期望，并且没有被接受。
/// @param name 失败时打印的用例名。
/// @param verdict 校验结论，只在本次调用期间借用。
/// @param expected 期望的错误类别。
/// @return 未接受且错误码匹配时为 true。
bool rejectedAs(const char* name, const ResponseVerdict& verdict, ErrorCode expected) {
    const ErrorCode* error = verdict.error();
    if (verdict.accepted() || verdict.response() != nullptr || error == nullptr || *error != expected) {
        return fail(name);
    }
    return true;
}

/// @brief 1 个 slot 的读响应被接受，数据顺序不变。
/// @return 从站、slot 数和 78 56 34 12 都匹配时为 true。
bool readPayloadAccepted() {
    const ResponseVerdict verdict = validateResponse(command(CommandKind::ReadHolding, 0, 1), withLength(kReadInner));
    const AcceptedResponse* response = verdict.response();
    const AcceptedRead* read = response == nullptr ? nullptr : std::get_if<AcceptedRead>(response);
    const std::uint8_t payload[] = {0x78, 0x56, 0x34, 0x12};
    if (!verdict.accepted() || verdict.error() != nullptr || read == nullptr || read->slaveAddress != 0x01
        || read->slotCount != 1 || read->payload.size() != 4) {
        return fail("read_payload_accepted");
    }
    for (std::size_t index = 0; index < 4; ++index) {
        if (read->payload[index] != payload[index]) {
            return fail("read_payload_accepted");
        }
    }
    return true;
}

/// @brief 从站不是命令期望的地址。
/// @return 错误码为 ResponseMismatch 时为 true。
bool readSlaveMismatch() {
    const std::uint8_t pdu[] = {0x02, 0x03, 0x04, 0x78, 0x56, 0x34, 0x12};
    const std::vector<std::uint8_t> inner = ernc::protocol::appendCrc16(pdu);
    const ResponseVerdict verdict = validateResponse(command(CommandKind::ReadHolding, 0, 1), withLength(inner));
    return rejectedAs("read_slave_mismatch", verdict, ErrorCode::ResponseMismatch);
}

/// @brief 读响应只有 1 个 slot，命令却期望 2 个。
/// @return 错误码为 PayloadSizeMismatch 时为 true。
bool readLengthMismatch() {
    const ResponseVerdict verdict = validateResponse(command(CommandKind::ReadHolding, 0, 2), withLength(kReadInner));
    return rejectedAs("read_length_mismatch", verdict, ErrorCode::PayloadSizeMismatch);
}

/// @brief 读命令收到 FC16 ACK。
/// @return 错误码为 ResponseMismatch 时为 true。
bool wrongFunctionRejected() {
    const ResponseVerdict verdict = validateResponse(command(CommandKind::ReadHolding, 0, 1), withLength(kWriteAckInner));
    return rejectedAs("wrong_function_rejected", verdict, ErrorCode::ResponseMismatch);
}

/// @brief 改动 CRC 后不接受。
/// @return 错误码为 CrcMismatch 时为 true。
bool crcRejected() {
    std::vector<std::uint8_t> wire = withLength(kReadInner);
    wire.back() = static_cast<std::uint8_t>(wire.back() ^ 0xFFU);
    const ResponseVerdict verdict = validateResponse(command(CommandKind::ReadHolding, 0, 1), wire);
    return rejectedAs("crc_rejected", verdict, ErrorCode::CrcMismatch);
}

/// @brief 空帧和长度字节与实际字节数不符都被拒绝。
/// @return 两种情况都是 FrameLengthInvalid 时为 true。
bool frameLengthRejected() {
    const ResponseVerdict empty = validateResponse(command(CommandKind::ReadHolding, 0, 1), {});
    std::vector<std::uint8_t> wire = withLength(kReadInner);
    wire[0] = static_cast<std::uint8_t>(wire[0] + 1);
    const ResponseVerdict lied = validateResponse(command(CommandKind::ReadHolding, 0, 1), wire);
    if (!rejectedAs("frame_length_rejected", empty, ErrorCode::FrameLengthInvalid)) {
        return false;
    }
    return rejectedAs("frame_length_rejected", lied, ErrorCode::FrameLengthInvalid);
}

/// @brief 地址和数量都匹配的写 ACK 被接受。
/// @return 回显为从站 0x01、寄存器 0、数量 2 时为 true。
bool writeAckAccepted() {
    const ResponseVerdict verdict = validateResponse(command(CommandKind::WriteMultiple, 0, 1), withLength(kWriteAckInner));
    const AcceptedResponse* response = verdict.response();
    const AcceptedWriteAck* ack = response == nullptr ? nullptr : std::get_if<AcceptedWriteAck>(response);
    if (!verdict.accepted() || ack == nullptr || ack->slaveAddress != 0x01 || ack->startRegister != 0 || ack->registerCount != 2) {
        return fail("write_ack_accepted");
    }
    return true;
}

/// @brief 写 ACK 的起始寄存器不是命令的 slot 乘 2。
/// @return 错误码为 ResponseMismatch 时为 true。
bool writeAddressMismatch() {
    const ResponseVerdict verdict = validateResponse(command(CommandKind::WriteMultiple, 1, 1), withLength(kWriteAckInner));
    return rejectedAs("write_address_mismatch", verdict, ErrorCode::ResponseMismatch);
}

/// @brief 写 ACK 的寄存器数量不是命令的 slot 数乘 2。
/// @return 错误码为 ResponseMismatch 时为 true。
bool writeCountMismatch() {
    const ResponseVerdict verdict = validateResponse(command(CommandKind::WriteMultiple, 0, 2), withLength(kWriteAckInner));
    return rejectedAs("write_count_mismatch", verdict, ErrorCode::ResponseMismatch);
}

/// @brief 读命令的异常帧不被接受，但保留异常码。
/// @return 错误码为 ModbusException 且异常码为 0x02 时为 true。
bool readExceptionKeepsCode() {
    const ResponseVerdict verdict = validateResponse(command(CommandKind::ReadHolding, 0, 1), withLength(kReadExceptionInner));
    const ErrorCode* error = verdict.error();
    const std::uint8_t* code = verdict.exceptionCode();
    if (verdict.accepted() || verdict.response() != nullptr || error == nullptr || *error != ErrorCode::ModbusException || code == nullptr
        || *code != 0x02) {
        return fail("read_exception_keeps_code");
    }
    return true;
}

/// @brief 写命令的异常帧不被接受，但保留异常码。
/// @return 错误码为 ModbusException 且异常码为 0x03 时为 true。
bool writeExceptionKeepsCode() {
    const ResponseVerdict verdict = validateResponse(command(CommandKind::WriteMultiple, 0, 1), withLength(kWriteExceptionInner));
    const ErrorCode* error = verdict.error();
    const std::uint8_t* code = verdict.exceptionCode();
    if (verdict.accepted() || error == nullptr || *error != ErrorCode::ModbusException || code == nullptr || *code != 0x03) {
        return fail("write_exception_keeps_code");
    }
    return true;
}

/// @brief 读命令收到写异常功能码。
/// @return 错误码为 ResponseMismatch 时为 true。
bool readRejectsWriteException() {
    const ResponseVerdict verdict = validateResponse(command(CommandKind::ReadHolding, 0, 1), withLength(kWriteExceptionInner));
    return rejectedAs("read_rejects_write_exception", verdict, ErrorCode::ResponseMismatch);
}

/// @brief 异常帧的从站与命令不一致。
/// @return 错误码为 ResponseMismatch，并且不把异常码当成已接受时为 true。
bool exceptionSlaveMismatch() {
    const std::uint8_t pdu[] = {0x02, 0x83, 0x02};
    const std::vector<std::uint8_t> inner = ernc::protocol::appendCrc16(pdu);
    const ResponseVerdict verdict = validateResponse(command(CommandKind::ReadHolding, 0, 1), withLength(inner));
    if (verdict.exceptionCode() != nullptr) {
        return fail("exception_slave_mismatch");
    }
    return rejectedAs("exception_slave_mismatch", verdict, ErrorCode::ResponseMismatch);
}

/// @brief 命令自己的数量非法时，不接受一帧本来完好的响应。
/// @return 0 个 slot 和 63 个读 slot 都是 InvalidParameter 时为 true。
bool commandRangeRejected() {
    const std::vector<std::uint8_t> wire = withLength(kReadInner);
    const ResponseVerdict zero = validateResponse(command(CommandKind::ReadHolding, 0, 0), wire);
    const ResponseVerdict tooMany = validateResponse(command(CommandKind::ReadHolding, 0, 63), wire);
    if (!rejectedAs("command_range_rejected", zero, ErrorCode::InvalidParameter)) {
        return false;
    }
    return rejectedAs("command_range_rejected", tooMany, ErrorCode::InvalidParameter);
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "read_payload_accepted") {
        return readPayloadAccepted();
    }
    if (name == "read_slave_mismatch") {
        return readSlaveMismatch();
    }
    if (name == "read_length_mismatch") {
        return readLengthMismatch();
    }
    if (name == "wrong_function_rejected") {
        return wrongFunctionRejected();
    }
    if (name == "crc_rejected") {
        return crcRejected();
    }
    if (name == "frame_length_rejected") {
        return frameLengthRejected();
    }
    if (name == "write_ack_accepted") {
        return writeAckAccepted();
    }
    if (name == "write_address_mismatch") {
        return writeAddressMismatch();
    }
    if (name == "write_count_mismatch") {
        return writeCountMismatch();
    }
    if (name == "read_exception_keeps_code") {
        return readExceptionKeepsCode();
    }
    if (name == "write_exception_keeps_code") {
        return writeExceptionKeepsCode();
    }
    if (name == "read_rejects_write_exception") {
        return readRejectsWriteException();
    }
    if (name == "exception_slave_mismatch") {
        return exceptionSlaveMismatch();
    }
    if (name == "command_range_rejected") {
        return commandRangeRejected();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个响应校验单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_response_validator_tests <case>\n";
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
