#include "ernc/domain/types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

// 这三个宏分别来自 QtCore、QtWidgets 和 QObject 相关头。
// Domain 目标一旦包含它们，翻译单元就失败。没走到这些头时检查不会触发。
#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::ParameterId;
using ernc::domain::Result;
using ernc::domain::ScalarValue;
using ernc::domain::SessionId;

/// @brief 探测 fromFloat 是否接受 T。
/// @tparam T 用来尝试调用工厂的类型。错误类型的探测式必须为 false。
template <typename T>
constexpr bool acceptsFloatFactory = requires {
    ScalarValue::fromFloat(std::declval<T>());
};

/// @brief 探测 fromInt32 是否接受 T。
/// @tparam T 用来尝试调用工厂的类型。错误类型的探测式必须为 false。
template <typename T>
constexpr bool acceptsInt32Factory = requires {
    ScalarValue::fromInt32(std::declval<T>());
};

/// @brief 探测 fromUInt32 是否接受 T。
/// @tparam T 用来尝试调用工厂的类型。错误类型的探测式必须为 false。
template <typename T>
constexpr bool acceptsUInt32Factory = requires {
    ScalarValue::fromUInt32(std::declval<T>());
};

/// @brief 确认空标识被拒绝，两段不同的非空文本会原样保留且互不相等。
/// @return 契约样例成立时为 true。
/// @note ANC_OFF 与 ENC_OFF 在这里只是两段目录标识文本，不定义开关线值。样例不是协议 Golden。
constexpr bool parameterIdsBehave() {
    const auto empty = ParameterId::parse("");
    const auto anc = ParameterId::parse("ANC_OFF");
    const auto enc = ParameterId::parse("ENC_OFF");
    if (empty.ok() || empty.tryValue() != nullptr || empty.tryError() == nullptr) {
        return false;
    }
    if (*empty.tryError() != ErrorCode::InvalidParameter) {
        return false;
    }
    if (!anc.ok() || anc.tryError() != nullptr || anc.tryValue() == nullptr) {
        return false;
    }
    if (anc.tryValue()->text() != std::string_view{"ANC_OFF"}) {
        return false;
    }
    if (enc.tryValue() == nullptr || *anc.tryValue() == *enc.tryValue()) {
        return false;
    }
    return true;
}

/// @brief 确认三种标量保持各自的种类，数值相同也不能跨种类相等。
/// @return 契约样例成立时为 true。样例不生成线上字节，也不是协议 Golden。
constexpr bool scalarValuesStayDistinct() {
    const auto number = ScalarValue::fromFloat(1.25f);
    const auto negative = ScalarValue::fromInt32(std::int32_t{-1});
    // 选这个比特是为了占住无符号高位。把它读成 int32 会变成负数。它不是固件常量。
    const auto highBit = ScalarValue::fromUInt32(std::uint32_t{0x80000000u});
    if (number.kind() != ScalarValue::Kind::Float32 || !number.asFloat().ok()) {
        return false;
    }
    if (number.asFloat().tryValue() == nullptr || *number.asFloat().tryValue() != 1.25f) {
        return false;
    }
    if (number.asInt32().ok() || number.asInt32().tryValue() != nullptr) {
        return false;
    }
    if (number.asInt32().tryError() == nullptr
        || *number.asInt32().tryError() != ErrorCode::InvalidParameter) {
        return false;
    }
    if (number.asUInt32().ok()) {
        return false;
    }
    if (negative.kind() != ScalarValue::Kind::Int32
        || negative.asInt32().tryValue() == nullptr
        || *negative.asInt32().tryValue() != std::int32_t{-1}) {
        return false;
    }
    if (negative.asUInt32().ok() || negative.asUInt32().tryValue() != nullptr
        || negative.asFloat().ok()) {
        return false;
    }
    if (highBit.kind() != ScalarValue::Kind::UInt32
        || highBit.asUInt32().tryValue() == nullptr
        || *highBit.asUInt32().tryValue() != std::uint32_t{0x80000000u}) {
        return false;
    }
    if (highBit.asInt32().ok() || highBit.asInt32().tryValue() != nullptr) {
        return false;
    }
    if (ScalarValue::fromInt32(std::int32_t{1}) == ScalarValue::fromUInt32(std::uint32_t{1})) {
        return false;
    }
    if (ScalarValue::fromFloat(1.0f) == ScalarValue::fromInt32(std::int32_t{1})) {
        return false;
    }
    return true;
}

/// @brief 确认 0 表示没有会话，并且 Result 的成功侧和失败侧不能互相取值。
/// @return 契约样例成立时为 true。样例不创建会话，也不是协议 Golden。
constexpr bool sessionsAndResultsBehave() {
    const auto none = SessionId::none();
    const auto session = SessionId::fromRaw(4);
    const auto failed = Result<float>::failure(ErrorCode::Unknown);
    const auto held = Result<float>::success(2.5f);
    const auto ok = Result<void>::success();
    const auto cancelled = Result<void>::failure(ErrorCode::OperationCancelled);
    if (none.valid() || none.raw() != 0 || SessionId::fromRaw(0) != none) {
        return false;
    }
    if (!session.valid() || session.raw() != 4 || session == none) {
        return false;
    }
    if (failed.ok() || failed.tryValue() != nullptr
        || failed.tryError() == nullptr || *failed.tryError() != ErrorCode::Unknown) {
        return false;
    }
    if (!held.ok() || held.tryError() != nullptr || held.tryValue() == nullptr
        || *held.tryValue() != 2.5f) {
        return false;
    }
    if (!ok.ok() || ok.tryError() != nullptr || cancelled.ok()) {
        return false;
    }
    if (cancelled.tryError() == nullptr
        || *cancelled.tryError() != ErrorCode::OperationCancelled
        || ok == cancelled) {
        return false;
    }
    return true;
}

}  // namespace

// 标量不能从三种数值类型隐式构造，也不能隐式转换回这三种类型。
static_assert(!std::is_convertible_v<float, ScalarValue>);
static_assert(!std::is_convertible_v<std::int32_t, ScalarValue>);
static_assert(!std::is_convertible_v<std::uint32_t, ScalarValue>);
static_assert(!std::is_constructible_v<ScalarValue, float>);
static_assert(!std::is_constructible_v<ScalarValue, std::int32_t>);
static_assert(!std::is_constructible_v<ScalarValue, std::uint32_t>);
static_assert(!std::is_convertible_v<ScalarValue, float>);
static_assert(!std::is_convertible_v<ScalarValue, std::int32_t>);
static_assert(!std::is_convertible_v<ScalarValue, std::uint32_t>);

// 工厂只接受完全相同的类型。double 也不能进入 Float32。
static_assert(acceptsFloatFactory<float>);
static_assert(!acceptsFloatFactory<std::int32_t>);
static_assert(!acceptsFloatFactory<std::uint32_t>);
static_assert(!acceptsFloatFactory<double>);
static_assert(acceptsInt32Factory<std::int32_t>);
static_assert(!acceptsInt32Factory<std::uint32_t>);
static_assert(!acceptsInt32Factory<float>);
static_assert(acceptsUInt32Factory<std::uint32_t>);
static_assert(!acceptsUInt32Factory<std::int32_t>);
static_assert(!acceptsUInt32Factory<float>);

// 身份类型之间，以及它们和裸整数、字符串之间，都不能隐式转换。
// 没有默认构造，避免未经解析的参数标识、未写出的会话代号和没有结果的 Result。
static_assert(!std::is_convertible_v<ParameterId, SessionId>);
static_assert(!std::is_convertible_v<SessionId, ParameterId>);
static_assert(!std::is_convertible_v<SessionId, ScalarValue>);
static_assert(!std::is_convertible_v<ScalarValue, SessionId>);
static_assert(!std::is_convertible_v<ParameterId, ScalarValue>);
static_assert(!std::is_convertible_v<ScalarValue, ParameterId>);
static_assert(!std::is_convertible_v<std::uint64_t, SessionId>);
static_assert(!std::is_convertible_v<SessionId, std::uint64_t>);
static_assert(!std::is_convertible_v<SessionId, std::uint32_t>);
static_assert(!std::is_convertible_v<std::string, ParameterId>);
static_assert(!std::is_convertible_v<float, Result<float>>);
static_assert(!std::is_default_constructible_v<ParameterId>);
static_assert(!std::is_default_constructible_v<ScalarValue>);
static_assert(!std::is_default_constructible_v<SessionId>);
static_assert(!std::is_default_constructible_v<Result<float>>);
static_assert(!std::is_default_constructible_v<Result<void>>);

// 上面三个函数是契约样例，不是线上 Golden。
static_assert(parameterIdsBehave());
static_assert(scalarValuesStayDistinct());
static_assert(sessionsAndResultsBehave());
