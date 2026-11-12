#include "ernc/infrastructure/legacy_param_json.hpp"

#include "ernc/domain/catalog.hpp"

#include "json_minimal.cpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <system_error>

namespace ernc::infrastructure {
namespace {

using ernc::domain::ErrorCode;
using ernc::domain::ParameterCatalog;
using ernc::domain::Result;
using ernc::domain::ScalarValue;
using json_minimal::Json;
using json_minimal::Parser;

/// @brief 保留超过 float32 精度的整数，同时兼容历史解析器的 float32 小数和小整数。
/// @param number JSON 中以 double 解析的无单位数值，按原单位保存。
/// @return 超出有限 float32 范围时为空，由调用方报告该行。
std::optional<ScalarValue> numberToScalar(double number) {
    if (!std::isfinite(number) || std::abs(number) > std::numeric_limits<float>::max()) {
        return std::nullopt;
    }
    const float rounded = static_cast<float>(number);
    if (static_cast<double>(rounded) != number && std::trunc(number) == number) {
        if (number >= std::numeric_limits<std::int32_t>::min() && number <= std::numeric_limits<std::int32_t>::max()) {
            return ScalarValue::fromInt32(static_cast<std::int32_t>(number));
        }
        if (number >= 0 && number <= std::numeric_limits<std::uint32_t>::max()) {
            return ScalarValue::fromUInt32(static_cast<std::uint32_t>(number));
        }
    }
    return ScalarValue::fromFloat(rounded);
}

LegacyParamJsonLoad failure(ErrorCode code) {
    LegacyParamJsonLoad load;
    load.success = false;
    load.error = code;
    return load;
}

/// @brief 从已解析 JSON 提取 v1 参数，保留可继续处理的逐行问题。
/// @param root 本次调用借用的 JSON 根对象。
/// @param catalog 可选目录，仅在本次调用借用，用于报告未知参数。
LegacyParamJsonLoad extractDocument(const Json& root, const ParameterCatalog* catalog) {
    if (root.kind != Json::Kind::Object) {
        return failure(ErrorCode::InvalidParameter);
    }
    LegacyParamJsonLoad load;
    load.success = true;
    load.error = ErrorCode::Unknown;
    for (const auto& field : root.fields) {
        if (field.first == "version" || field.first == "params") {
            continue;
        }
        load.report.unrecognizedTopLevelFields.push_back(field.first);
    }
    const Json* version = json_minimal::findField(root, "version");
    const Json* params = json_minimal::findField(root, "params");
    if (version == nullptr || version->kind != Json::Kind::String || params == nullptr || params->kind != Json::Kind::Array) {
        return failure(ErrorCode::InvalidParameter);
    }
    load.document.version = version->text;
    if (load.document.version != kLegacyParamJsonVersion) {
        return failure(ErrorCode::InvalidParameter);
    }
    for (const Json& item : params->elements) {
        if (item.kind != Json::Kind::Object) {
            load.report.malformedEntries.push_back("(not an object)");
            continue;
        }
        const Json* nameField = json_minimal::findField(item, "name");
        const Json* valueField = json_minimal::findField(item, "value");
        if (nameField == nullptr || nameField->kind != Json::Kind::String) {
            load.report.malformedEntries.push_back("(missing name)");
            continue;
        }
        if (valueField == nullptr) {
            load.report.malformedEntries.push_back(nameField->text);
            continue;
        }
        if (valueField->kind == Json::Kind::Array) {
            load.report.skippedNames.push_back(nameField->text);
            continue;
        }
        if (valueField->kind != Json::Kind::Number) {
            load.report.malformedEntries.push_back(nameField->text);
            continue;
        }
        if (catalog != nullptr && catalog->find(nameField->text) == nullptr) {
            load.report.unknownNames.push_back(nameField->text);
            continue;
        }
        const auto value = numberToScalar(valueField->number);
        if (!value) {
            load.report.malformedEntries.push_back(nameField->text);
            continue;
        }
        load.document.params.push_back(LegacyParamJsonEntry{nameField->text, *value});
    }
    return load;
}

std::string escapeJsonString(std::string_view text) {
    std::string out;
    out.push_back('"');
    for (char character : text) {
        if (character == '"' || character == '\\') {
            out.push_back('\\');
        }
        out.push_back(character);
    }
    out.push_back('"');
    return out;
}

}  // namespace

LegacyParamJsonLoad parseLegacyParamJson(std::string_view utf8, const ParameterCatalog* catalog) {
    if (utf8.starts_with("\xEF\xBB\xBF")) {
        utf8.remove_prefix(3);
    }
    Parser parser(utf8);
    const std::optional<Json> root = parser.parseDocument();
    if (!root) {
        return failure(ErrorCode::InvalidParameter);
    }
    return extractDocument(*root, catalog);
}

LegacyParamJsonLoad loadLegacyParamJsonFile(const std::filesystem::path& source) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return failure(ErrorCode::FileIoFailed);
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.empty()) {
        return failure(ErrorCode::InvalidParameter);
    }
    return parseLegacyParamJson(text);
}

Result<std::string> serializeLegacyParamJson(const LegacyParamJsonDocument& document) {
    if (document.version != kLegacyParamJsonVersion) {
        return Result<std::string>::failure(ErrorCode::InvalidParameter);
    }
    std::string out = "{\"version\":\"";
    out.append(document.version);
    out.append("\",\"params\":[");
    for (std::size_t index = 0; index < document.params.size(); ++index) {
        if (index > 0) {
            out.push_back(',');
        }
        const LegacyParamJsonEntry& entry = document.params[index];
        out.push_back('{');
        out.append("\"name\":");
        out.append(escapeJsonString(entry.name));
        out.push_back(',');
        out.append("\"value\":");
        if (const auto number = entry.value.asFloat(); number.ok()) {
            if (!std::isfinite(number.value())) {
                return Result<std::string>::failure(ErrorCode::InvalidParameter);
            }
            char buffer[32];
            const auto encoded = std::to_chars(buffer, buffer + sizeof(buffer), static_cast<double>(number.value()));
            if (encoded.ec != std::errc{}) {
                return Result<std::string>::failure(ErrorCode::InvalidParameter);
            }
            out.append(buffer, encoded.ptr);
        } else if (const auto integer = entry.value.asInt32(); integer.ok()) {
            out.append(std::to_string(integer.value()));
        } else {
            out.append(std::to_string(entry.value.asUInt32().value()));
        }
        out.push_back('}');
    }
    out.append("]}");
    return Result<std::string>::success(std::move(out));
}

Result<void> writeLegacyParamJsonFile(const std::filesystem::path& destination,
                                      const LegacyParamJsonDocument& document) {
    const Result<std::string> text = serializeLegacyParamJson(document);
    if (!text.ok()) {
        return Result<void>::failure(text.error());
    }
    // 保留 Windows 原生宽路径，避免中文路径经过系统代码页。
    std::filesystem::path staging = destination;
    staging += ".writing";
    {
        std::ofstream output(staging, std::ios::binary | std::ios::trunc);
        if (!output) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
        output.write(text.value().data(), static_cast<std::streamsize>(text.value().size()));
        output.close();
        if (!output) {
            std::error_code ignored;
            std::filesystem::remove(staging, ignored);
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
    }
    std::error_code error;
    std::filesystem::rename(staging, destination, error);
    if (error) {
        std::filesystem::remove(staging, error);
        return Result<void>::failure(ErrorCode::FileIoFailed);
    }
    return Result<void>::success();
}

}  // namespace ernc::infrastructure
