#pragma once

#include "ernc/domain/types.hpp"

#include <charconv>
#include <string>

namespace ernc::presentation {

/// @brief 显示固件原值。float32 用能还原原值的最短十进制，整数保留精确值。
/// @param value 当次显示借用的标量。空指针表示没有读数或目标，不显示为 0。
/// @return UTF-8 文本，仅用于界面；工程序列化和设备编解码各用原有格式。
inline std::string displayScalar(const ernc::domain::ScalarValue* value) {
    if (value == nullptr) {
        return "—";
    }
    if (const auto number = value->asFloat(); number.ok()) {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), number.value(), std::chars_format::general);
        return result.ec == std::errc{} ? std::string(buffer, result.ptr) : std::string("—");
    }
    if (const auto number = value->asInt32(); number.ok()) {
        return std::to_string(number.value());
    }
    return std::to_string(value->asUInt32().value());
}

}  // namespace ernc::presentation
