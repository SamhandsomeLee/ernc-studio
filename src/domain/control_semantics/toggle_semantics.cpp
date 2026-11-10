#include "ernc/domain/control_semantics.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

/// @brief 判断目录声明的数据类型和标量种类是否相同。
/// @param declared 目录中的标量类型。
/// @param kind 标量当前的种类。
/// @return 两者指向同一种表示时为 true。
bool kindMatches(DataType declared, ScalarValue::Kind kind) {
    switch (declared) {
    case DataType::Float32:
        return kind == ScalarValue::Kind::Float32;
    case DataType::Int32:
        return kind == ScalarValue::Kind::Int32;
    case DataType::UInt32:
        return kind == ScalarValue::Kind::UInt32;
    }
    return false;
}

/// @brief 判断线值能否精确放进 float，不做四舍五入。
/// @param level 目录里的十进制线值，没有物理单位。
/// @param out 成功时写入精确相等的 float。
/// @return 线值有限且转成 float 后再转回 double 仍相等时为 true。
bool exactFloat(double level, float& out) {
    if (!std::isfinite(level)) {
        return false;
    }
    const float narrowed = static_cast<float>(level);
    if (static_cast<double>(narrowed) != level) {
        return false;
    }
    out = narrowed;
    return true;
}

/// @brief 判断线值能否精确放进 int32_t。
/// @param level 目录里的十进制线值，没有物理单位。
/// @param out 成功时写入精确相等的 int32_t。
/// @return 线值是 int32 范围内的整数时为 true。
bool exactInt32(double level, std::int32_t& out) {
    if (!std::isfinite(level) || std::floor(level) != level) {
        return false;
    }
    if (level < static_cast<double>(std::numeric_limits<std::int32_t>::min())
        || level > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    out = static_cast<std::int32_t>(level);
    return true;
}

/// @brief 判断线值能否精确放进 uint32_t。
/// @param level 目录里的十进制线值，没有物理单位。
/// @param out 成功时写入精确相等的 uint32_t。
/// @return 线值是 uint32 范围内的非负整数时为 true。
bool exactUInt32(double level, std::uint32_t& out) {
    if (!std::isfinite(level) || level < 0 || std::floor(level) != level) {
        return false;
    }
    if (level > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    out = static_cast<std::uint32_t>(level);
    return true;
}

/// @brief 判断标量是否精确等于一条线值。
/// @param value 设备或用户标量，只在本次调用期间借用。
/// @param level 目录里的 on 或 off。没有物理单位。
/// @return 标量种类能精确表示该线值且数值相等时为 true。
bool matchesLevel(const ScalarValue& value, double level) {
    switch (value.kind()) {
    case ScalarValue::Kind::Float32: {
        float expected = 0;
        const Result<float> held = value.asFloat();
        return exactFloat(level, expected) && held.ok() && held.value() == expected;
    }
    case ScalarValue::Kind::Int32: {
        std::int32_t expected = 0;
        const Result<std::int32_t> held = value.asInt32();
        return exactInt32(level, expected) && held.ok() && held.value() == expected;
    }
    case ScalarValue::Kind::UInt32: {
        std::uint32_t expected = 0;
        const Result<std::uint32_t> held = value.asUInt32();
        return exactUInt32(level, expected) && held.ok() && held.value() == expected;
    }
    }
    return false;
}

/// @brief 目录是否给出了可用于解释的 toggle 线值。
/// @param definition 参数定义，只在本次调用期间借用。
/// @return editor 为 toggle 且 on/off 都存在时为 true。
bool hasConfirmedLevels(const ParameterDefinition& definition) {
    return definition.editor && *definition.editor == EditorKind::Toggle && definition.wireLevels.has_value();
}

/// @brief 按声明的数据类型生成一条线值。
/// @param declared 目录中的标量类型。
/// @param level 要写入的 on 或 off。没有物理单位。
/// @return 线值能精确放进该种类时返回标量。否则返回 InvalidParameter。
Result<ScalarValue> scalarFor(DataType declared, double level) {
    switch (declared) {
    case DataType::Float32: {
        float number = 0;
        if (!exactFloat(level, number)) {
            return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
        }
        return Result<ScalarValue>::success(ScalarValue::fromFloat(number));
    }
    case DataType::Int32: {
        std::int32_t number = 0;
        if (!exactInt32(level, number)) {
            return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
        }
        return Result<ScalarValue>::success(ScalarValue::fromInt32(number));
    }
    case DataType::UInt32: {
        std::uint32_t number = 0;
        if (!exactUInt32(level, number)) {
            return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
        }
        return Result<ScalarValue>::success(ScalarValue::fromUInt32(number));
    }
    }
    return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
}

}  // namespace

TogglePosition decodeToggle(const ParameterDefinition& definition, const ScalarValue& value) {
    if (!hasConfirmedLevels(definition)) {
        return TogglePosition::Unknown;
    }
    if (definition.dataType && !kindMatches(*definition.dataType, value.kind())) {
        return TogglePosition::Unknown;
    }
    const WireLevels& levels = *definition.wireLevels;
    if (matchesLevel(value, levels.on)) {
        return TogglePosition::On;
    }
    if (matchesLevel(value, levels.off)) {
        return TogglePosition::Off;
    }
    return TogglePosition::Unknown;
}

Result<ScalarValue> encodeToggle(const ParameterDefinition& definition, TogglePosition position) {
    if (position == TogglePosition::Unknown || !hasConfirmedLevels(definition) || !definition.dataType) {
        return Result<ScalarValue>::failure(ErrorCode::InvalidParameter);
    }
    const double level = position == TogglePosition::On ? definition.wireLevels->on : definition.wireLevels->off;
    return scalarFor(*definition.dataType, level);
}

}  // namespace ernc::domain
