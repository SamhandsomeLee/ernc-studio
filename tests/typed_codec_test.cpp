#include "ernc/protocol/typed_codec.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::domain::ScalarValue;
using ernc::protocol::decodeScalar;
using ernc::protocol::decodeScalars;
using ernc::protocol::encodeScalar;
using ernc::protocol::encodeScalars;

constexpr std::uint8_t kFloatTwoPointFive[] = {0x00, 0x00, 0x20, 0x40};
constexpr std::uint8_t kFloatNegative[] = {0x00, 0x00, 0xA0, 0xBF};
constexpr std::uint8_t kPositiveZero[] = {0x00, 0x00, 0x00, 0x00};
constexpr std::uint8_t kNegativeZero[] = {0x00, 0x00, 0x00, 0x80};
constexpr std::uint8_t kPositiveInf[] = {0x00, 0x00, 0x80, 0x7F};
constexpr std::uint8_t kNegativeInf[] = {0x00, 0x00, 0x80, 0xFF};
constexpr std::uint8_t kQuietNan[] = {0x00, 0x00, 0xC0, 0x7F};
constexpr std::uint8_t kPayloadNan[] = {0x01, 0x00, 0x80, 0x7F};
constexpr std::uint8_t kInt32Min[] = {0x00, 0x00, 0x00, 0x80};
constexpr std::uint8_t kInt32Max[] = {0xFF, 0xFF, 0xFF, 0x7F};
constexpr std::uint8_t kAllOnes[] = {0xFF, 0xFF, 0xFF, 0xFF};

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

/// @brief 编码结果与字面量一致，并且按同一字节再解回原值。
/// @param name 失败时打印的用例名。
/// @param value 要编码的标量。
/// @param expected 期望的 4 个小端字节。
/// @return 字节一致且解码后与 value 相等时为 true。
bool roundTrip(const char* name, const ScalarValue& value, std::span<const std::uint8_t> expected) {
    const std::array<std::uint8_t, 4> encoded = encodeScalar(value);
    const Result<ScalarValue> decoded = decodeScalar(expected, value.kind());
    const ScalarValue* restored = decoded.tryValue();
    if (!sameBytes(encoded, expected) || !decoded.ok() || restored == nullptr || *restored != value) {
        return fail(name);
    }
    return true;
}

/// @brief 2.5 的小端字节是 00 00 20 40。
/// @return 往返一致时为 true。
bool floatTwoPointFive() {
    return roundTrip("float_two_point_five", ScalarValue::fromFloat(2.5f), kFloatTwoPointFive);
}

/// @brief -1.25 的小端字节是 00 00 A0 BF。
/// @return 往返一致时为 true。
bool floatNegative() {
    return roundTrip("float_negative", ScalarValue::fromFloat(-1.25f), kFloatNegative);
}

/// @brief 正零和负零的字节不同。
/// @return 正零是 00 00 00 00、负零是 00 00 00 80 时为 true。
bool signedZeroBytesDiffer() {
    const std::array<std::uint8_t, 4> positive = encodeScalar(ScalarValue::fromFloat(0.0f));
    const std::array<std::uint8_t, 4> negative = encodeScalar(ScalarValue::fromFloat(-0.0f));
    if (!sameBytes(positive, kPositiveZero) || !sameBytes(negative, kNegativeZero)) {
        return fail("signed_zero_bytes_differ");
    }
    return true;
}

/// @brief int32 最小值和最大值使用小端。
/// @return 两端字节和数值都匹配时为 true。
bool int32Limits() {
    if (!roundTrip("int32_limits", ScalarValue::fromInt32(INT32_MIN), kInt32Min)) {
        return false;
    }
    return roundTrip("int32_limits", ScalarValue::fromInt32(INT32_MAX), kInt32Max);
}

/// @brief uint32 的高位保持无符号位模式。
/// @return 0、0x80000000 和 0xFFFFFFFF 的字节匹配时为 true。
bool uint32Limits() {
    if (!roundTrip("uint32_limits", ScalarValue::fromUInt32(std::uint32_t{0}), kPositiveZero)) {
        return false;
    }
    if (!roundTrip("uint32_limits", ScalarValue::fromUInt32(0x80000000u), kInt32Min)) {
        return false;
    }
    return roundTrip("uint32_limits", ScalarValue::fromUInt32(0xFFFFFFFFu), kAllOnes);
}

/// @brief 同一组字节按三种种类解码，结果互不相等。
/// @return 00 00 00 80 分别是负零、int32 最小值和 0x80000000 时为 true。
bool sameBytesKeepKind() {
    const Result<ScalarValue> asFloat = decodeScalar(kNegativeZero, ScalarValue::Kind::Float32);
    const Result<ScalarValue> asInt = decodeScalar(kNegativeZero, ScalarValue::Kind::Int32);
    const Result<ScalarValue> asUint = decodeScalar(kNegativeZero, ScalarValue::Kind::UInt32);
    const ScalarValue* floatValue = asFloat.tryValue();
    const ScalarValue* intValue = asInt.tryValue();
    const ScalarValue* uintValue = asUint.tryValue();
    if (floatValue == nullptr || intValue == nullptr || uintValue == nullptr) {
        return fail("same_bytes_keep_kind");
    }
    const Result<float> number = floatValue->asFloat();
    const Result<std::int32_t> signedNumber = intValue->asInt32();
    const Result<std::uint32_t> unsignedNumber = uintValue->asUInt32();
    if (!number.ok() || !signedNumber.ok() || !unsignedNumber.ok() || !std::signbit(number.value()) || number.value() != 0.0f
        || signedNumber.value() != INT32_MIN || unsignedNumber.value() != 0x80000000u) {
        return fail("same_bytes_keep_kind");
    }
    if (*floatValue == *intValue || *intValue == *uintValue || *floatValue == *uintValue) {
        return fail("same_bytes_keep_kind");
    }
    const Result<ScalarValue> negativeOne = decodeScalar(kAllOnes, ScalarValue::Kind::Int32);
    const Result<ScalarValue> allBits = decodeScalar(kAllOnes, ScalarValue::Kind::UInt32);
    const ScalarValue* signedAll = negativeOne.tryValue();
    const ScalarValue* unsignedAll = allBits.tryValue();
    if (signedAll == nullptr || unsignedAll == nullptr || signedAll->asInt32().value() != -1
        || unsignedAll->asUInt32().value() != 0xFFFFFFFFu || *signedAll == *unsignedAll) {
        return fail("same_bytes_keep_kind");
    }
    return true;
}

/// @brief NaN 再编码后字节不变。比较的是字节，不是浮点相等。
/// @param name 失败时打印的用例名。
/// @param bytes 4 个 NaN 位模式，只在本次调用期间借用。
/// @return 解码结果是 NaN 且再编码字节相同时为 true。
bool nanBitsRoundTrip(const char* name, std::span<const std::uint8_t> bytes) {
    const Result<ScalarValue> decoded = decodeScalar(bytes, ScalarValue::Kind::Float32);
    const ScalarValue* value = decoded.tryValue();
    if (!decoded.ok() || value == nullptr || !value->asFloat().ok() || !std::isnan(value->asFloat().value())) {
        return fail(name);
    }
    if (!sameBytes(encodeScalar(*value), bytes)) {
        return fail(name);
    }
    return true;
}

/// @brief 普通 quiet NaN 的字节是 00 00 C0 7F。
/// @return 位模式往返一致时为 true。
bool quietNanBitsRoundTrip() {
    return nanBitsRoundTrip("quiet_nan_bits_round_trip", kQuietNan);
}

/// @brief 带载荷的 NaN 不被改成普通 quiet NaN。
/// @return 01 00 80 7F 保持不变时为 true。
bool nanPayloadBitsPreserved() {
    return nanBitsRoundTrip("nan_payload_bits_preserved", kPayloadNan);
}

/// @brief 正无穷和负无穷按原字节往返。
/// @return 两种无穷的符号和字节都匹配时为 true。
bool infinitiesRoundTrip() {
    const Result<ScalarValue> positive = decodeScalar(kPositiveInf, ScalarValue::Kind::Float32);
    const Result<ScalarValue> negative = decodeScalar(kNegativeInf, ScalarValue::Kind::Float32);
    const ScalarValue* positiveValue = positive.tryValue();
    const ScalarValue* negativeValue = negative.tryValue();
    if (positiveValue == nullptr || negativeValue == nullptr) {
        return fail("infinities_round_trip");
    }
    const Result<float> positiveNumber = positiveValue->asFloat();
    const Result<float> negativeNumber = negativeValue->asFloat();
    if (!positiveNumber.ok() || !negativeNumber.ok() || !std::isinf(positiveNumber.value()) || std::signbit(positiveNumber.value())
        || !std::isinf(negativeNumber.value()) || !std::signbit(negativeNumber.value())) {
        return fail("infinities_round_trip");
    }
    if (!sameBytes(encodeScalar(*positiveValue), kPositiveInf) || !sameBytes(encodeScalar(*negativeValue), kNegativeInf)) {
        return fail("infinities_round_trip");
    }
    return true;
}

/// @brief 不是 4 字节的单个标量被拒绝。
/// @return 0、3 和 8 字节都得到 PayloadSizeMismatch 时为 true。
bool shortBufferRejected() {
    const std::uint8_t three[] = {0x00, 0x00, 0x20};
    const std::uint8_t eight[] = {0x00, 0x00, 0x20, 0x40, 0x00, 0x00, 0xA0, 0xBF};
    const Result<ScalarValue> empty = decodeScalar({}, ScalarValue::Kind::Float32);
    const Result<ScalarValue> shortBytes = decodeScalar(three, ScalarValue::Kind::Float32);
    const Result<ScalarValue> longBytes = decodeScalar(eight, ScalarValue::Kind::Int32);
    const ErrorCode* emptyError = empty.tryError();
    const ErrorCode* shortError = shortBytes.tryError();
    const ErrorCode* longError = longBytes.tryError();
    if (empty.ok() || shortBytes.ok() || longBytes.ok() || emptyError == nullptr || shortError == nullptr || longError == nullptr) {
        return fail("short_buffer_rejected");
    }
    if (*emptyError != ErrorCode::PayloadSizeMismatch || *shortError != ErrorCode::PayloadSizeMismatch
        || *longError != ErrorCode::PayloadSizeMismatch) {
        return fail("short_buffer_rejected");
    }
    return true;
}

/// @brief 两个相同种类的标量接成 8 字节，再按同样种类拆开。
/// @return 字节是 2.5 紧接 -1.25，且拆开后数值相同。
bool scalarBlockRoundTrip() {
    const ScalarValue values[] = {ScalarValue::fromFloat(2.5f), ScalarValue::fromFloat(-1.25f)};
    const Result<std::vector<std::uint8_t>> encoded = encodeScalars(values);
    const std::uint8_t expected[] = {0x00, 0x00, 0x20, 0x40, 0x00, 0x00, 0xA0, 0xBF};
    const std::vector<std::uint8_t>* bytes = encoded.tryValue();
    if (!encoded.ok() || bytes == nullptr || !sameBytes(*bytes, expected)) {
        return fail("scalar_block_round_trip");
    }
    const Result<std::vector<ScalarValue>> decoded = decodeScalars(*bytes, ScalarValue::Kind::Float32);
    const std::vector<ScalarValue>* restored = decoded.tryValue();
    if (!decoded.ok() || restored == nullptr || restored->size() != 2 || (*restored)[0] != values[0]
        || (*restored)[1] != values[1]) {
        return fail("scalar_block_round_trip");
    }
    return true;
}

/// @brief 一组里出现两种种类时不输出字节。
/// @return 错误码为 InvalidParameter 时为 true。
bool mixedKindsRejected() {
    const ScalarValue values[] = {ScalarValue::fromFloat(1.0f), ScalarValue::fromInt32(std::int32_t{1})};
    const Result<std::vector<std::uint8_t>> encoded = encodeScalars(values);
    const ErrorCode* error = encoded.tryError();
    if (encoded.ok() || encoded.tryValue() != nullptr || error == nullptr || *error != ErrorCode::InvalidParameter) {
        return fail("mixed_kinds_rejected");
    }
    return true;
}

/// @brief 空序列得到空数据区，空数据区得到空序列。
/// @return 两侧都成功且长度为 0 时为 true。
bool emptyBlockIsEmpty() {
    const Result<std::vector<std::uint8_t>> encoded = encodeScalars({});
    const Result<std::vector<ScalarValue>> decoded = decodeScalars({}, ScalarValue::Kind::UInt32);
    const std::vector<std::uint8_t>* bytes = encoded.tryValue();
    const std::vector<ScalarValue>* values = decoded.tryValue();
    if (!encoded.ok() || !decoded.ok() || bytes == nullptr || values == nullptr || !bytes->empty() || !values->empty()) {
        return fail("empty_block_is_empty");
    }
    return true;
}

/// @brief 数据区长度不是 4 的倍数。
/// @return 6 字节得到 PayloadSizeMismatch 时为 true。
bool blockLengthRejected() {
    const std::uint8_t six[] = {0x00, 0x00, 0x20, 0x40, 0x00, 0x00};
    const Result<std::vector<ScalarValue>> decoded = decodeScalars(six, ScalarValue::Kind::Float32);
    const ErrorCode* error = decoded.tryError();
    if (decoded.ok() || error == nullptr || *error != ErrorCode::PayloadSizeMismatch) {
        return fail("block_length_rejected");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "float_two_point_five") {
        return floatTwoPointFive();
    }
    if (name == "float_negative") {
        return floatNegative();
    }
    if (name == "signed_zero_bytes_differ") {
        return signedZeroBytesDiffer();
    }
    if (name == "int32_limits") {
        return int32Limits();
    }
    if (name == "uint32_limits") {
        return uint32Limits();
    }
    if (name == "same_bytes_keep_kind") {
        return sameBytesKeepKind();
    }
    if (name == "quiet_nan_bits_round_trip") {
        return quietNanBitsRoundTrip();
    }
    if (name == "nan_payload_bits_preserved") {
        return nanPayloadBitsPreserved();
    }
    if (name == "infinities_round_trip") {
        return infinitiesRoundTrip();
    }
    if (name == "short_buffer_rejected") {
        return shortBufferRejected();
    }
    if (name == "scalar_block_round_trip") {
        return scalarBlockRoundTrip();
    }
    if (name == "mixed_kinds_rejected") {
        return mixedKindsRejected();
    }
    if (name == "empty_block_is_empty") {
        return emptyBlockIsEmpty();
    }
    if (name == "block_length_rejected") {
        return blockLengthRejected();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个标量字节序单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_typed_codec_tests <case>\n";
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
