#include "ernc/application/transfer_cancellation.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"
#include "ernc/infrastructure/legacy_region_codec.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::RegionCatalog;
using ernc::domain::RegionDefinition;
using ernc::domain::Result;
using ernc::infrastructure::readRegionBin;
using ernc::infrastructure::readRegionTxt;
using ernc::infrastructure::writeRegionBin;
using ernc::infrastructure::writeRegionTxt;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 用完就删掉的临时目录。
struct TempDir {
    /// 本次用例的目录。
    std::filesystem::path dir;

    /// @brief 在系统临时目录下建一个空目录。
    TempDir() {
        std::random_device device;
        dir = std::filesystem::temp_directory_path() / ("ernc-es075-" + std::to_string(device()));
        std::filesystem::create_directories(dir);
    }

    /// @brief 删除目录和里面的文件。
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
    }
};

/// @brief 做成一块测试区域。
/// @param path 区域路径。
/// @param slots slot 数。字节数是它乘 4。
/// @return 调用方拥有的定义。
RegionDefinition regionOf(std::string path, std::uint32_t slots) {
    RegionDefinition region;
    region.path = std::move(path);
    region.slotCount = slots;
    region.size = slots * 4U;
    region.scalarType = ernc::domain::DataType::Float32;
    region.shape = {slots};
    return region;
}

/// @brief 用测试自己的字节写一个文件。不调用被测编码器。
/// @param path 目标路径。
/// @param bytes 文件内容。
/// @return 写完时为 true。
bool writeRaw(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(output);
}

/// @brief 读出文件的全部字节。不调用被测编码器。
/// @param path 文件路径。
/// @return 文件内容。打不开时为空向量。
std::vector<std::uint8_t> readRaw(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff end = input.tellg();
    if (end < 0) {
        return {};
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    return bytes;
}

/// @brief 手工小端 BIN 写出再读回，字节相同。
/// @return 8 字节与 1.0、2.0 的位模式一致时为 true。
bool binRoundTripMatchesBytes() {
    constexpr const char* name = "bin_round_trip_matches_bytes";
    TempDir temp;
    const RegionDefinition region = regionOf("lab.two", 2);
    const std::vector<std::uint8_t> bytes{0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40};
    const std::filesystem::path destination = temp.dir / "sample.bin";
    if (!writeRegionBin(region, bytes, destination).ok()) {
        return fail(name);
    }
    const Result<std::vector<std::uint8_t>> loaded = readRegionBin(region, destination);
    if (!loaded.ok() || loaded.tryValue() == nullptr || loaded.value() != bytes || readRaw(destination) != bytes) {
        return fail(name);
    }
    return true;
}

/// @brief 本编解码器写出的 TXT 再读回，float 序列相同。
/// @return 1 和 0.1 都按原值回来时为 true。
bool txtRoundTripMatchesFloats() {
    constexpr const char* name = "txt_round_trip_matches_floats";
    TempDir temp;
    const RegionDefinition region = regionOf("lab.two", 2);
    const std::vector<float> values{1.0F, 0.1F};
    const std::filesystem::path destination = temp.dir / "sample.txt";
    if (!writeRegionTxt(region, values, destination).ok()) {
        return fail(name);
    }
    const Result<std::vector<float>> loaded = readRegionTxt(region, destination);
    if (!loaded.ok() || loaded.tryValue() == nullptr || loaded.value() != values) {
        return fail(name);
    }
    const std::filesystem::path handwritten = temp.dir / "hand.txt";
    if (!writeRaw(handwritten, std::vector<std::uint8_t>{
            '#', ' ', 'r', 'e', 'g', 'i', 'o', 'n', '=', 'l', 'a', 'b', '.', 't', 'w', 'o', '\n',
            '#', ' ', 'f', 'o', 'r', 'm', 'a', 't', '=', 's', 'l', 'o', 't', '_', 'f', 'l', 'o', 'a', 't', 's', '\n',
            '#', ' ', 's', 'l', 'o', 't', 's', '=', '2', '\n',
            '\n',
            '#', ' ', 'c', 'o', 'm', 'm', 'e', 'n', 't', '\n',
            '1', ',', '\n',
            '2', ',', '\n'})) {
        return fail(name);
    }
    const Result<std::vector<float>> parsed = readRegionTxt(region, handwritten);
    if (!parsed.ok() || parsed.tryValue() == nullptr || parsed.value().size() != 2U || parsed.value()[0] != 1.0F || parsed.value()[1] != 2.0F) {
        return fail(name);
    }
    return true;
}

/// @brief 旧版单字段 TXT 不带元数据头，且读取时兼容有无尾逗号。
/// @return 导出只有 value, 行，手工无逗号文件也能按相同 float 读回时为 true。
bool fieldTxtMatchesLegacy() {
    constexpr const char* name = "field_txt_matches_legacy";
    TempDir temp;
    const RegionDefinition region = regionOf("check_data.HornRefCheck_F", 2);
    const std::filesystem::path exported = temp.dir / "HornRefCheck_F.txt";
    const std::vector<float> values{1.0F, 0.5F};
    if (!writeRegionTxt(region, values, exported, ernc::infrastructure::RegionTxtLayout::Field).ok()) {
        return fail(name);
    }
    const std::vector<std::uint8_t> raw = readRaw(exported);
    const std::string text(raw.begin(), raw.end());
    if (text != "1,\n0.5,\n" || text.find('#') != std::string::npos) {
        return fail(name);
    }
    const std::filesystem::path handwritten = temp.dir / "without_commas.txt";
    const std::string oldText = "1\n0.5\n";
    if (!writeRaw(handwritten, std::vector<std::uint8_t>(oldText.begin(), oldText.end()))) {
        return fail(name);
    }
    const Result<std::vector<float>> loaded = readRegionTxt(region, handwritten);
    if (!loaded.ok() || loaded.tryValue() == nullptr || loaded.value() != values) {
        return fail(name);
    }
    return true;
}

/// @brief 短于区域长度的 BIN 被拒绝。
/// @param manifestPath 正式 manifest。
/// @return 4 字节文件不能当成 120 点谱半径时为 true。
bool shortBinRejected(const char* manifestPath) {
    constexpr const char* name = "short_bin_rejected";
    TempDir temp;
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail(name);
    }
    const Result<RegionCatalog> catalog = RegionCatalog::load(loaded.layout());
    if (!catalog.ok() || catalog.tryValue() == nullptr) {
        return fail(name);
    }
    const RegionDefinition* radius = catalog.value().find("ERNC_TF.SpecRadius");
    if (radius == nullptr || radius->size != 480U) {
        return fail(name);
    }
    const std::filesystem::path source = temp.dir / "short.bin";
    if (!writeRaw(source, std::vector<std::uint8_t>{0x00, 0x00, 0x80, 0x3F})) {
        return fail(name);
    }
    const Result<std::vector<std::uint8_t>> parsed = readRegionBin(*radius, source);
    if (parsed.ok() || parsed.tryValue() != nullptr || parsed.tryError() == nullptr || *parsed.tryError() != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    return true;
}

/// @brief 非数字 TXT 被拒绝。
/// @return 没有数值结果时为 true。
bool txtBadTokenRejected() {
    constexpr const char* name = "txt_bad_token_rejected";
    TempDir temp;
    const RegionDefinition region = regionOf("lab.two", 2);
    const std::filesystem::path source = temp.dir / "bad.txt";
    const std::string text = "# region=lab.two\n# format=slot_floats\n# slots=2\nnope,\n2,\n";
    if (!writeRaw(source, std::vector<std::uint8_t>(text.begin(), text.end()))) {
        return fail(name);
    }
    const Result<std::vector<float>> parsed = readRegionTxt(region, source);
    if (parsed.ok() || parsed.tryValue() != nullptr || parsed.tryError() == nullptr || *parsed.tryError() != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    return true;
}

/// @brief slots 注释和区域不一致时拒绝。
/// @return 没有数值结果时为 true。
bool txtSlotsHeaderMismatchRejected() {
    constexpr const char* name = "txt_slots_header_mismatch_rejected";
    TempDir temp;
    const RegionDefinition region = regionOf("lab.two", 2);
    const std::filesystem::path source = temp.dir / "slots.txt";
    const std::string text = "# region=lab.two\n# format=slot_floats\n# slots=3\n1,\n2,\n";
    if (!writeRaw(source, std::vector<std::uint8_t>(text.begin(), text.end()))) {
        return fail(name);
    }
    const Result<std::vector<float>> parsed = readRegionTxt(region, source);
    if (parsed.ok() || parsed.tryError() == nullptr || *parsed.tryError() != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    return true;
}

/// @brief 取消记录的前缀不能导出成区域 BIN。
/// @return 不创建目标文件时为 true。
bool cancellationPrefixIsNotExported() {
    constexpr const char* name = "cancellation_prefix_is_not_exported";
    TempDir temp;
    const RegionDefinition region = regionOf("lab.two", 2);
    ernc::application::RegionTransferRecord record;
    record.path = region.path;
    record.completedSlots = 1;
    record.prefix = {0x00, 0x00, 0x80, 0x3F};
    const std::filesystem::path destination = temp.dir / "partial.bin";
    const Result<void> written = writeRegionBin(region, record.prefix, destination);
    if (record.canSaveAsCompleteBin() || written.ok() || written.tryError() == nullptr || *written.tryError() != ErrorCode::InvalidParameter
        || std::filesystem::exists(destination)) {
        return fail(name);
    }
    return true;
}

/// @brief 拒绝导出时，用户原来的 BIN 字节不变。
/// @return 源文件仍是手工写入的 8 字节，目标文件不存在时为 true。
bool sourceFileUnchangedAfterRejectedExport() {
    constexpr const char* name = "source_file_unchanged_after_rejected_export";
    TempDir temp;
    const RegionDefinition region = regionOf("lab.two", 2);
    const std::vector<std::uint8_t> original{0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40};
    const std::filesystem::path source = temp.dir / "source.bin";
    const std::filesystem::path destination = temp.dir / "dest.bin";
    if (!writeRaw(source, original)) {
        return fail(name);
    }
    const std::vector<std::uint8_t> shortBytes{0x00, 0x00, 0x80, 0x3F};
    const Result<void> written = writeRegionBin(region, shortBytes, destination);
    if (written.ok() || readRaw(source) != original || std::filesystem::exists(destination)) {
        return fail(name);
    }
    const Result<std::vector<std::uint8_t>> loaded = readRegionBin(region, source);
    if (!loaded.ok() || loaded.tryValue() == nullptr || loaded.value() != original || readRaw(source) != original) {
        return fail(name);
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。
/// @param manifestPath 正式 manifest。只有短 BIN 用例读取它。
/// @return 用例通过时为 true。
bool runCase(std::string_view name, const char* manifestPath) {
    if (name == "bin_round_trip_matches_bytes") {
        return binRoundTripMatchesBytes();
    }
    if (name == "txt_round_trip_matches_floats") {
        return txtRoundTripMatchesFloats();
    }
    if (name == "field_txt_matches_legacy") {
        return fieldTxtMatchesLegacy();
    }
    if (name == "short_bin_rejected") {
        return shortBinRejected(manifestPath);
    }
    if (name == "txt_bad_token_rejected") {
        return txtBadTokenRejected();
    }
    if (name == "txt_slots_header_mismatch_rejected") {
        return txtSlotsHeaderMismatchRejected();
    }
    if (name == "cancellation_prefix_is_not_exported") {
        return cancellationPrefixIsNotExported();
    }
    if (name == "source_file_unchanged_after_rejected_export") {
        return sourceFileUnchangedAfterRejectedExport();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_legacy_region_codec_tests <case> <layout_manifest.json>\n";
        return 2;
    }
    return runCase(argv[1], argv[2]) ? 0 : 1;
}
