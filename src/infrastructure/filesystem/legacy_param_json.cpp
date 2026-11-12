#include "ernc/infrastructure/legacy_param_json.hpp"

#include "ernc/domain/catalog.hpp"

#include "json_minimal.cpp"

#include <cstdio>
#include <fstream>
#include <system_error>

namespace ernc::infrastructure {
namespace {

using ernc::domain::ErrorCode;
using ernc::domain::ParameterCatalog;
using ernc::domain::Result;
using ernc::domain::ScalarValue;
using json_minimal::Json;
using json_minimal::Parser;

ScalarValue numberToScalar(double number) {
    return ScalarValue::fromFloat(static_cast<float>(number));
}

LegacyParamJsonLoad failure(ErrorCode code) {
    LegacyParamJsonLoad load;
    load.success = false;
    load.error = code;
    return load;
}

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
        load.document.params.push_back(
            LegacyParamJsonEntry{nameField->text, numberToScalar(valueField->number)});
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
        const Result<float> number = entry.value.asFloat();
        if (!number.ok()) {
            const Result<std::int32_t> integer = entry.value.asInt32();
            if (!integer.ok()) {
                return Result<std::string>::failure(ErrorCode::InvalidParameter);
            }
            out.append(std::to_string(integer.value()));
        } else {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(number.value()));
            out.append(buffer);
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
    const std::filesystem::path staging = destination.string() + ".writing";
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
