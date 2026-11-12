#include "ernc/infrastructure/legacy_param_template.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

namespace ernc::infrastructure {
namespace {

using ernc::domain::ErrorCode;
using ernc::domain::ParameterCatalog;
using ernc::domain::Result;

std::vector<std::string> tokenizeTemplateLines(std::string_view utf8) {
    std::vector<std::string> tokens;
    std::string line;
  std::istringstream stream{std::string(utf8)};
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        std::size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) {
            ++start;
        }
        std::size_t end = line.size();
        while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
            --end;
        }
        if (end <= start) {
            continue;
        }
        std::string token = line.substr(start, end - start);
        while (!token.empty() && token.back() == ',') {
            token.pop_back();
            while (!token.empty() && (token.back() == ' ' || token.back() == '\t')) {
                token.pop_back();
            }
        }
        if (!token.empty()) {
            tokens.push_back(std::move(token));
        }
    }
    return tokens;
}

LegacyTemplateLoad templateFailure(ErrorCode code) {
    LegacyTemplateLoad load;
    load.success = false;
    load.error = code;
    return load;
}

LegacyParamsLoad paramsFailure(ErrorCode code) {
    LegacyParamsLoad load;
    load.success = false;
    load.error = code;
    return load;
}

bool parseRowLimit(std::string_view text, std::size_t& rowLimit) {
    char* end = nullptr;
    const long parsed = std::strtol(text.data(), &end, 10);
    if (end == text.data() || parsed < 0) {
        return false;
    }
    while (end != nullptr && *end != '\0' && (*end == ' ' || *end == '\t')) {
        ++end;
    }
    if (end != nullptr && *end != '\0') {
        return false;
    }
    rowLimit = static_cast<std::size_t>(parsed);
    return true;
}

bool parseValueLine(std::string_view line, double& value) {
    std::string trimmed;
    trimmed.reserve(line.size());
    for (char character : line) {
        if (character == '\r') {
            continue;
        }
        trimmed.push_back(character);
    }
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t')) {
        trimmed.erase(trimmed.begin());
    }
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
        trimmed.pop_back();
    }
    if (trimmed.empty()) {
        return false;
    }
    while (!trimmed.empty() && trimmed.back() == ',') {
        trimmed.pop_back();
        while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t')) {
            trimmed.pop_back();
        }
    }
    if (trimmed.empty()) {
        return false;
    }
    char* end = nullptr;
    value = std::strtod(trimmed.c_str(), &end);
    if (end == trimmed.c_str()) {
        return false;
    }
    while (end != nullptr && *end != '\0' && (*end == ' ' || *end == '\t')) {
        ++end;
    }
    return end != nullptr && *end == '\0';
}

}  // namespace

LegacyTemplateLoad parseExportTemplate(std::string_view utf8, const ParameterCatalog& catalog) {
    const std::vector<std::string> tokens = tokenizeTemplateLines(utf8);
    if (tokens.empty()) {
        return templateFailure(ErrorCode::InvalidParameter);
    }
    LegacyTemplateLoad load;
    load.success = true;
    load.error = ErrorCode::Unknown;
    if (!parseRowLimit(tokens.front(), load.template_.rowLimit)) {
        return templateFailure(ErrorCode::InvalidParameter);
    }
    for (std::size_t index = 1; index < tokens.size(); ++index) {
        const std::string& name = tokens[index];
        if (catalog.find(name) == nullptr) {
            load.report.unknownTemplateLines.push_back(name);
        } else {
            load.template_.names.push_back(name);
        }
    }
    if (!load.report.unknownTemplateLines.empty()) {
        load.success = false;
        load.error = ErrorCode::InvalidParameter;
    }
    return load;
}

LegacyTemplateLoad loadExportTemplateFile(const std::filesystem::path& source, const ParameterCatalog& catalog) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return templateFailure(ErrorCode::FileIoFailed);
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.empty()) {
        return templateFailure(ErrorCode::InvalidParameter);
    }
    return parseExportTemplate(text, catalog);
}

LegacyParamsLoad parseLegacyParamsFile(std::string_view utf8, std::size_t expectedRows) {
    LegacyParamsLoad load;
    load.success = true;
    load.error = ErrorCode::Unknown;
    std::istringstream stream{std::string(utf8)};
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        double value = 0.0;
        if (!parseValueLine(line, value)) {
            return paramsFailure(ErrorCode::InvalidParameter);
        }
        load.file.values.push_back(value);
    }
    if (load.file.values.size() != expectedRows) {
        return paramsFailure(ErrorCode::InvalidParameter);
    }
    return load;
}

LegacyParamsLoad loadLegacyParamsFile(const std::filesystem::path& source, std::size_t expectedRows) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return paramsFailure(ErrorCode::FileIoFailed);
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (text.empty()) {
        return paramsFailure(ErrorCode::InvalidParameter);
    }
    return parseLegacyParamsFile(text, expectedRows);
}

Result<std::string> serializeLegacyParamsFile(const std::vector<double>& values) {
    std::string out;
    out.reserve(values.size() * 16);
    for (double value : values) {
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.9g,\n", value);
        out.append(buffer);
    }
    return Result<std::string>::success(std::move(out));
}

Result<void> writeLegacyParamsFile(const std::filesystem::path& destination, const std::vector<double>& values) {
    const Result<std::string> text = serializeLegacyParamsFile(values);
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
