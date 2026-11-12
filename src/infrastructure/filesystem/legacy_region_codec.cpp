#include "ernc/infrastructure/legacy_region_codec.hpp"

#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_filesystem must not include Qt"
#endif

namespace ernc::infrastructure {
namespace {

using ernc::domain::ErrorCode;
using ernc::domain::RegionDefinition;
using ernc::domain::Result;

/// @brief 区域字节数是否等于 slot 数乘 4。
/// @param region 区域定义，只在本次调用期间借用。
/// @return 长度一致且路径非空时为 true。
bool geometryOk(const RegionDefinition& region) noexcept {
    if (region.path.empty() || region.slotCount > (std::numeric_limits<std::uint32_t>::max() / 4U)) {
        return false;
    }
    return region.size == region.slotCount * 4U;
}

/// @brief 扩展名是否匹配。比较不区分大小写。
/// @param path 文件路径。
/// @param expected 带点的扩展名，例如 .bin。必须已经是小写。
/// @return 匹配时为 true。
bool extensionIs(const std::filesystem::path& path, std::string_view expected) {
    std::string extension = path.extension().string();
    for (char& character : extension) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return extension == expected;
}

/// @brief 去掉首尾空白。
/// @param text 一行文本。函数就地修改。
void trim(std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\r')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\r')) {
        --end;
    }
    text = text.substr(begin, end - begin);
}

/// @brief 只读读出整个文件。
/// @param source 文件路径。只读打开。
/// @return 成功时调用方拥有全部字节。打不开或读不完时为 FileIoFailed。
Result<std::vector<std::uint8_t>> readFile(const std::filesystem::path& source) {
    std::ifstream input(source, std::ios::binary);
    if (!input) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::FileIoFailed);
    }
    input.seekg(0, std::ios::end);
    const std::streamoff end = input.tellg();
    if (end < 0) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::FileIoFailed);
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!input) {
            return Result<std::vector<std::uint8_t>>::failure(ErrorCode::FileIoFailed);
        }
    }
    return Result<std::vector<std::uint8_t>>::success(std::move(bytes));
}

/// @brief 把字节写到临时文件，完成后再换成目标路径。
/// @param destination 最终路径。
/// @param bytes 要写入的全部字节。
/// @return 替换完成时成功。失败时删除临时文件，不把半截内容留成目标文件。
Result<void> writeAtomically(const std::filesystem::path& destination, std::span<const std::uint8_t> bytes) {
    const std::filesystem::path staging(destination.string() + ".writing");
    {
        std::ofstream output(staging, std::ios::binary | std::ios::trunc);
        if (!output) {
            return Result<void>::failure(ErrorCode::FileIoFailed);
        }
        if (!bytes.empty()) {
            output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
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
        std::filesystem::remove(destination, error);
        std::filesystem::rename(staging, destination, error);
    }
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(staging, ignored);
        return Result<void>::failure(ErrorCode::FileIoFailed);
    }
    return Result<void>::success();
}

/// @brief 把一个 float 写成能往返的十进制。
/// @param value 要写出的 float。
/// @return 不含逗号的文本。转换失败时为空。
std::string formatFloat(float value) {
    char buffer[64];
    const std::to_chars_result converted = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                                                         std::numeric_limits<float>::max_digits10);
    if (converted.ec != std::errc{}) {
        return {};
    }
    return std::string(buffer, converted.ptr);
}

/// @brief 解析注释行。没有约束的注释被跳过。
/// @param line 已经去掉空白、并且以 # 开头的行。
/// @param region 期望的区域。
/// @return 约束相符或这行没有约束时成功。路径、格式或 slot 数不符时为 InvalidParameter。
Result<void> acceptComment(const std::string& line, const RegionDefinition& region) {
    constexpr std::string_view kRegion = "# region=";
    constexpr std::string_view kFormat = "# format=";
    constexpr std::string_view kSlots = "# slots=";
    if (line.rfind(kRegion, 0) == 0) {
        if (line.substr(kRegion.size()) != region.path) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        return Result<void>::success();
    }
    if (line.rfind(kFormat, 0) == 0) {
        if (line.substr(kFormat.size()) != "slot_floats") {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        return Result<void>::success();
    }
    if (line.rfind(kSlots, 0) == 0) {
        const std::string number = line.substr(kSlots.size());
        std::uint32_t slots = 0;
        const char* begin = number.data();
        const char* end = begin + number.size();
        const std::from_chars_result parsed = std::from_chars(begin, end, slots);
        if (parsed.ec != std::errc{} || parsed.ptr != end || slots != region.slotCount) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        return Result<void>::success();
    }
    return Result<void>::success();
}

/// @brief 解析一行 value,。
/// @param line 已经去掉空白、并且不是注释的行。
/// @param value 成功时写入 float。
/// @return 有一个 float 时成功；末尾的一个逗号可有可无，以兼容旧版人工文件和导出文件。
Result<void> acceptValue(const std::string& line, float& value) {
    if (line.empty()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const std::string number = line.back() == ',' ? line.substr(0, line.size() - 1) : line;
    if (number.empty()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const char* begin = number.data();
    const char* end = begin + number.size();
    const std::from_chars_result parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    return Result<void>::success();
}

}  // namespace

Result<std::vector<std::uint8_t>> readRegionBin(const RegionDefinition& region, const std::filesystem::path& source) {
    if (!geometryOk(region) || !extensionIs(source, ".bin")) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::InvalidParameter);
    }
    Result<std::vector<std::uint8_t>> file = readFile(source);
    if (!file.ok() || file.tryValue() == nullptr) {
        return Result<std::vector<std::uint8_t>>::failure(file.ok() ? ErrorCode::Unknown : file.error());
    }
    if (file.value().size() != region.size) {
        return Result<std::vector<std::uint8_t>>::failure(ErrorCode::InvalidParameter);
    }
    return file;
}

Result<void> writeRegionBin(const RegionDefinition& region, std::span<const std::uint8_t> bytes,
                            const std::filesystem::path& destination) {
    if (!geometryOk(region) || !extensionIs(destination, ".bin") || bytes.size() != region.size) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    return writeAtomically(destination, bytes);
}

Result<std::vector<float>> readRegionTxt(const RegionDefinition& region, const std::filesystem::path& source) {
    if (!geometryOk(region) || !extensionIs(source, ".txt")) {
        return Result<std::vector<float>>::failure(ErrorCode::InvalidParameter);
    }
    std::ifstream input(source);
    if (!input) {
        return Result<std::vector<float>>::failure(ErrorCode::FileIoFailed);
    }
    std::vector<float> values;
    values.reserve(region.slotCount);
    std::string line;
    while (std::getline(input, line)) {
        trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.front() == '#') {
            const Result<void> comment = acceptComment(line, region);
            if (!comment.ok()) {
                return Result<std::vector<float>>::failure(comment.error());
            }
            continue;
        }
        float value = 0.0F;
        const Result<void> parsed = acceptValue(line, value);
        if (!parsed.ok()) {
            return Result<std::vector<float>>::failure(parsed.error());
        }
        values.push_back(value);
    }
    if (input.bad() || values.size() != region.slotCount) {
        return Result<std::vector<float>>::failure(input.bad() ? ErrorCode::FileIoFailed : ErrorCode::InvalidParameter);
    }
    return Result<std::vector<float>>::success(std::move(values));
}

Result<void> writeRegionTxt(const RegionDefinition& region, std::span<const float> values,
                            const std::filesystem::path& destination, const RegionTxtLayout layout) {
    if (!geometryOk(region) || !extensionIs(destination, ".txt") || values.size() != region.slotCount) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    std::string text;
    if (layout == RegionTxtLayout::WholeRegion) {
        text += "# region=" + region.path + "\n";
        text += "# format=slot_floats\n";
        text += "# slots=" + std::to_string(region.slotCount) + "\n";
    }
    for (const float value : values) {
        const std::string formatted = formatFloat(value);
        if (formatted.empty()) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        text += formatted;
        text += ",\n";
    }
    return writeAtomically(destination, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

}  // namespace ernc::infrastructure
