#include "ernc/protocol/typed_codec.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_protocol must not include Qt"
#endif

namespace ernc::protocol {
namespace {

static_assert(sizeof(float) == 4);
static_assert(sizeof(std::int32_t) == 4);
static_assert(sizeof(std::uint32_t) == 4);

/// @brief 把 32 位模式拆成低字节在前的 4 字节。
/// @param bits 与种类对应的位模式。float 的位模式是 IEEE-754 binary32。
/// @return 下标 0 是最低字节。
std::array<std::uint8_t, 4> littleEndianBytes(std::uint32_t bits) noexcept {
    return {
        static_cast<std::uint8_t>(bits & 0xFFU),
        static_cast<std::uint8_t>((bits >> 8U) & 0xFFU),
        static_cast<std::uint8_t>((bits >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((bits >> 24U) & 0xFFU),
    };
}

/// @brief 把 4 个低字节在前的字节合成 32 位模式。
/// @param bytes 恰好 4 字节，只在本次调用期间借用。下标 0 是最低字节。
/// @return 合成后的位模式。
std::uint32_t loadLittleEndian(std::span<const std::uint8_t, 4> bytes) noexcept {
    const auto b0 = static_cast<std::uint32_t>(bytes[0]);
    const auto b1 = static_cast<std::uint32_t>(bytes[1]);
    const auto b2 = static_cast<std::uint32_t>(bytes[2]);
    const auto b3 = static_cast<std::uint32_t>(bytes[3]);
    return b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
}

/// @brief 取出标量的 32 位模式。
/// @param value 标量，只在本次调用期间借用。种类决定取哪一种表示。
/// @return 不改变数值含义的位模式。
std::uint32_t bitPattern(const ernc::domain::ScalarValue& value) noexcept {
    switch (value.kind()) {
    case ernc::domain::ScalarValue::Kind::Float32: {
        const float number = value.asFloat().value();
        std::uint32_t bits = 0;
        std::memcpy(&bits, &number, sizeof(bits));
        return bits;
    }
    case ernc::domain::ScalarValue::Kind::Int32: {
        const std::int32_t number = value.asInt32().value();
        std::uint32_t bits = 0;
        std::memcpy(&bits, &number, sizeof(bits));
        return bits;
    }
    case ernc::domain::ScalarValue::Kind::UInt32:
        return value.asUInt32().value();
    }
    return 0;
}

/// @brief 按种类把位模式放进 ScalarValue。
/// @param bits 小端字节合成的 32 位模式。
/// @param kind 调用方指定的种类。
/// @return 该种类的标量。kind 不是三种之一时失败，错误码为 InvalidParameter。
ernc::domain::Result<ernc::domain::ScalarValue> valueFromBits(std::uint32_t bits, ernc::domain::ScalarValue::Kind kind) {
    switch (kind) {
    case ernc::domain::ScalarValue::Kind::Float32: {
        float number = 0.0f;
        std::memcpy(&number, &bits, sizeof(number));
        return ernc::domain::Result<ernc::domain::ScalarValue>::success(ernc::domain::ScalarValue::fromFloat(number));
    }
    case ernc::domain::ScalarValue::Kind::Int32: {
        std::int32_t number = 0;
        std::memcpy(&number, &bits, sizeof(number));
        return ernc::domain::Result<ernc::domain::ScalarValue>::success(ernc::domain::ScalarValue::fromInt32(number));
    }
    case ernc::domain::ScalarValue::Kind::UInt32:
        return ernc::domain::Result<ernc::domain::ScalarValue>::success(ernc::domain::ScalarValue::fromUInt32(bits));
    }
    return ernc::domain::Result<ernc::domain::ScalarValue>::failure(ernc::domain::ErrorCode::InvalidParameter);
}

}  // namespace

std::array<std::uint8_t, 4> encodeScalar(const ernc::domain::ScalarValue& value) noexcept {
    return littleEndianBytes(bitPattern(value));
}

ernc::domain::Result<ernc::domain::ScalarValue> decodeScalar(std::span<const std::uint8_t> bytes,
                                                             ernc::domain::ScalarValue::Kind kind) {
    if (bytes.size() != 4) {
        return ernc::domain::Result<ernc::domain::ScalarValue>::failure(ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    const std::uint32_t bits = loadLittleEndian(bytes.first<4>());
    return valueFromBits(bits, kind);
}

ernc::domain::Result<std::vector<std::uint8_t>> encodeScalars(std::span<const ernc::domain::ScalarValue> values) {
    if (values.empty()) {
        return ernc::domain::Result<std::vector<std::uint8_t>>::success({});
    }
    const ernc::domain::ScalarValue::Kind kind = values.front().kind();
    for (const ernc::domain::ScalarValue& value : values) {
        if (value.kind() != kind) {
            return ernc::domain::Result<std::vector<std::uint8_t>>::failure(ernc::domain::ErrorCode::InvalidParameter);
        }
    }
    std::vector<std::uint8_t> bytes;
    bytes.reserve(values.size() * 4);
    for (const ernc::domain::ScalarValue& value : values) {
        const std::array<std::uint8_t, 4> encoded = encodeScalar(value);
        bytes.insert(bytes.end(), encoded.begin(), encoded.end());
    }
    return ernc::domain::Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

ernc::domain::Result<std::vector<ernc::domain::ScalarValue>> decodeScalars(std::span<const std::uint8_t> bytes,
                                                                           ernc::domain::ScalarValue::Kind kind) {
    if (bytes.size() % 4 != 0) {
        return ernc::domain::Result<std::vector<ernc::domain::ScalarValue>>::failure(
            ernc::domain::ErrorCode::PayloadSizeMismatch);
    }
    std::vector<ernc::domain::ScalarValue> values;
    values.reserve(bytes.size() / 4);
    for (std::size_t offset = 0; offset < bytes.size(); offset += 4) {
        const ernc::domain::Result<ernc::domain::ScalarValue> one = decodeScalar(bytes.subspan(offset, 4), kind);
        if (!one.ok()) {
            return ernc::domain::Result<std::vector<ernc::domain::ScalarValue>>::failure(one.error());
        }
        values.push_back(one.value());
    }
    return ernc::domain::Result<std::vector<ernc::domain::ScalarValue>>::success(std::move(values));
}

}  // namespace ernc::protocol
