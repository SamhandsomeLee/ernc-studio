#include "ernc/protocol/crc16.hpp"
#include "ernc/protocol/wire_encoder.hpp"
#include "ernc/protocol/wire_parser.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::protocol::ParseStep;
using ernc::protocol::WireParser;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 两个缓冲区的长度和每个字节都相同。
/// @param actual 被测输出，只在本次调用期间借用。
/// @param expected 期望字节，只在本次调用期间借用。
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

/// @brief 给 Modbus 本体加上 CRC 和长度前缀。
/// @param pdu 不含 CRC 的本体，只在本次调用期间借用。
/// @return 编码器产生的整帧。编码失败时为空。
std::vector<std::uint8_t> wireFor(std::span<const std::uint8_t> pdu) {
    const std::vector<std::uint8_t> modbus = ernc::protocol::appendCrc16(pdu);
    const auto encoded = ernc::protocol::encodeLengthPrefixed(modbus);
    const std::vector<std::uint8_t>* wire = encoded.tryValue();
    if (!encoded.ok() || wire == nullptr) {
        return {};
    }
    return *wire;
}

/// @brief 一直拉取，直到需要更多数据。
/// @param parser 解析器，只在本次调用期间借用。会从中取出当前能决定的步骤。
/// @return 除 NeedMore 以外的步骤。最后一次 NeedMore 不放进结果。
std::vector<ParseStep> drain(WireParser& parser) {
    std::vector<ParseStep> steps;
    while (true) {
        ParseStep step = parser.pull();
        if (step.kind == ParseStep::Kind::NeedMore) {
            return steps;
        }
        steps.push_back(std::move(step));
    }
}

/// @brief 逐字节送入后得到的有效步骤。
/// @param bytes 整段输入，只在本次调用期间借用。
/// @return 除 NeedMore 以外的步骤。
std::vector<ParseStep> feedOneByte(std::span<const std::uint8_t> bytes) {
    WireParser parser;
    std::vector<ParseStep> steps;
    for (const std::uint8_t byte : bytes) {
        const std::uint8_t one[] = {byte};
        parser.append(one);
        std::vector<ParseStep> ready = drain(parser);
        steps.insert(steps.end(), std::make_move_iterator(ready.begin()), std::make_move_iterator(ready.end()));
    }
    return steps;
}

/// @brief 两个步骤序列的类别和帧字节都相同。
/// @param actual 实际步骤，只在本次调用期间借用。
/// @param expected 期望步骤，只在本次调用期间借用。
/// @return 长度、类别和帧都一致时为 true。
bool sameSteps(std::span<const ParseStep> actual, std::span<const ParseStep> expected) {
    if (actual.size() != expected.size()) {
        return false;
    }
    for (std::size_t index = 0; index < actual.size(); ++index) {
        if (actual[index].kind != expected[index].kind || !sameBytes(actual[index].frame, expected[index].frame)) {
            return false;
        }
    }
    return true;
}

/// @brief 一次送入和逐字节送入得到相同的帧与丢弃序列。
/// @return 两帧之间夹着非法长度时，两种送法的有效步骤一致。
bool chunkMatchesByteFeed() {
    const std::uint8_t firstPdu[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    const std::uint8_t secondPdu[] = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02};
    const std::vector<std::uint8_t> first = wireFor(firstPdu);
    const std::vector<std::uint8_t> second = wireFor(secondPdu);
    if (first.empty() || second.empty()) {
        return fail("chunk_matches_byte_feed");
    }
    std::vector<std::uint8_t> bytes{0x00, 0x04};
    bytes.insert(bytes.end(), first.begin(), first.end());
    bytes.insert(bytes.end(), second.begin(), second.end());
    WireParser chunkParser;
    chunkParser.append(bytes);
    const std::vector<ParseStep> chunkSteps = drain(chunkParser);
    const std::vector<ParseStep> byteSteps = feedOneByte(bytes);
    if (!sameSteps(chunkSteps, byteSteps) || chunkSteps.size() != 4 || chunkSteps[0].kind != ParseStep::Kind::LengthDropped
        || chunkSteps[1].kind != ParseStep::Kind::LengthDropped || chunkSteps[2].kind != ParseStep::Kind::Frame
        || chunkSteps[3].kind != ParseStep::Kind::Frame || !sameBytes(chunkSteps[2].frame, first)
        || !sameBytes(chunkSteps[3].frame, second)) {
        return fail("chunk_matches_byte_feed");
    }
    return true;
}

/// @brief 两帧粘在一起时按到达顺序交出。
/// @return 先是第一帧，再是第二帧，并且没有多余的成功帧。
bool stickyFramesInOrder() {
    const std::uint8_t firstPdu[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    const std::uint8_t secondPdu[] = {0x01, 0x10, 0x00, 0x02, 0x00, 0x02};
    const std::vector<std::uint8_t> first = wireFor(firstPdu);
    const std::vector<std::uint8_t> second = wireFor(secondPdu);
    std::vector<std::uint8_t> bytes = first;
    bytes.insert(bytes.end(), second.begin(), second.end());
    WireParser parser;
    parser.append(bytes);
    const std::vector<ParseStep> steps = drain(parser);
    if (steps.size() != 2 || steps[0].kind != ParseStep::Kind::Frame || steps[1].kind != ParseStep::Kind::Frame
        || !sameBytes(steps[0].frame, first) || !sameBytes(steps[1].frame, second)) {
        return fail("sticky_frames_in_order");
    }
    return true;
}

/// @brief 帧前面的 00 和 04 被丢掉，后面的好帧仍然交出。
/// @return 两次 LengthDropped 之后得到原来的整帧。
bool garbageBytesDropped() {
    const std::uint8_t pdu[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    const std::vector<std::uint8_t> frame = wireFor(pdu);
    std::vector<std::uint8_t> bytes{0x00, 0x04};
    bytes.insert(bytes.end(), frame.begin(), frame.end());
    WireParser parser;
    parser.append(bytes);
    const std::vector<ParseStep> steps = drain(parser);
    if (steps.size() != 3 || steps[0].kind != ParseStep::Kind::LengthDropped || steps[1].kind != ParseStep::Kind::LengthDropped
        || steps[2].kind != ParseStep::Kind::Frame || !steps[0].frame.empty() || !sameBytes(steps[2].frame, frame)) {
        return fail("garbage_bytes_dropped");
    }
    return true;
}

/// @brief 字节不够时不交出帧，补齐后交出整帧。
/// @return 半帧的拉取是 NeedMore，补齐后的帧与一次送入相同。
bool halfFrameWaits() {
    const std::uint8_t pdu[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    const std::vector<std::uint8_t> frame = wireFor(pdu);
    if (frame.size() < 4) {
        return fail("half_frame_waits");
    }
    WireParser parser;
    parser.append(std::span<const std::uint8_t>{frame.data(), 3});
    const ParseStep waiting = parser.pull();
    if (waiting.kind != ParseStep::Kind::NeedMore || !waiting.frame.empty()) {
        return fail("half_frame_waits");
    }
    parser.append(std::span<const std::uint8_t>{frame.data() + 3, frame.size() - 3});
    const std::vector<ParseStep> steps = drain(parser);
    if (steps.size() != 1 || steps[0].kind != ParseStep::Kind::Frame || !sameBytes(steps[0].frame, frame)) {
        return fail("half_frame_waits");
    }
    return true;
}

/// @brief 声明长度已经到齐但 CRC 被改过时，不作为成功帧。
/// @return 结果是 CrcRejected，帧字节为空，缓冲里也不再留下这帧。
bool badCrcIsNotAFrame() {
    const std::uint8_t pdu[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    std::vector<std::uint8_t> frame = wireFor(pdu);
    if (frame.empty()) {
        return fail("bad_crc_is_not_a_frame");
    }
    frame.back() = static_cast<std::uint8_t>(frame.back() ^ 0xFFU);
    WireParser parser;
    parser.append(frame);
    const ParseStep rejected = parser.pull();
    const ParseStep after = parser.pull();
    if (rejected.kind != ParseStep::Kind::CrcRejected || !rejected.frame.empty() || after.kind != ParseStep::Kind::NeedMore) {
        return fail("bad_crc_is_not_a_frame");
    }
    return true;
}

/// @brief 长度字节为 255 但后续还没到齐时继续等待。
/// @return 两次拉取都是 NeedMore，不把这个长度字节丢掉。
bool declaredLengthWaits() {
    const std::uint8_t partial[] = {0xFF, 0x01, 0x02};
    WireParser parser;
    parser.append(partial);
    const ParseStep first = parser.pull();
    const ParseStep second = parser.pull();
    if (first.kind != ParseStep::Kind::NeedMore || second.kind != ParseStep::Kind::NeedMore) {
        return fail("declared_length_waits");
    }
    return true;
}

/// @brief 清空后，后半段不会和清空前的半帧拼起来。
/// @return 清空并送入后半段后没有成功帧。
bool clearDropsPartialFrame() {
    const std::uint8_t pdu[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    const std::vector<std::uint8_t> frame = wireFor(pdu);
    if (frame.size() < 4) {
        return fail("clear_drops_partial_frame");
    }
    WireParser parser;
    parser.append(std::span<const std::uint8_t>{frame.data(), 4});
    parser.clear();
    parser.append(std::span<const std::uint8_t>{frame.data() + 4, frame.size() - 4});
    const std::vector<ParseStep> steps = drain(parser);
    for (const ParseStep& step : steps) {
        if (step.kind == ParseStep::Kind::Frame) {
            return fail("clear_drops_partial_frame");
        }
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "chunk_matches_byte_feed") {
        return chunkMatchesByteFeed();
    }
    if (name == "sticky_frames_in_order") {
        return stickyFramesInOrder();
    }
    if (name == "garbage_bytes_dropped") {
        return garbageBytesDropped();
    }
    if (name == "half_frame_waits") {
        return halfFrameWaits();
    }
    if (name == "bad_crc_is_not_a_frame") {
        return badCrcIsNotAFrame();
    }
    if (name == "declared_length_waits") {
        return declaredLengthWaits();
    }
    if (name == "clear_drops_partial_frame") {
        return clearDropsPartialFrame();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个流式拆帧单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_wire_parser_tests <case>\n";
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
