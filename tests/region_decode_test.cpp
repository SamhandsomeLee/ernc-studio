#include "ernc/domain/ernc_tf_codec.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"

#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::DataType;
using ernc::domain::DecodedField;
using ernc::domain::DecodedRegion;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::RegionCatalog;
using ernc::domain::RegionDefinition;
using ernc::domain::Result;
using ernc::domain::SubRegion;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 在字段字节的某个 float 位置写入独立的小端位模式。
/// @param bytes 字段字节。长度必须盖住这个 float。
/// @param index 从 0 开始的 float 下标。
/// @param littleEndian 恰好 4 字节，下标 0 是最低字节。不调用解码器。
/// @return 范围落在缓冲内时为 true。
bool putBytes(std::vector<std::uint8_t>& bytes, std::size_t index, const std::uint8_t littleEndian[4]) {
    const std::size_t at = index * 4U;
    if (at + 4U > bytes.size()) {
        return false;
    }
    bytes[at] = littleEndian[0];
    bytes[at + 1U] = littleEndian[1];
    bytes[at + 2U] = littleEndian[2];
    bytes[at + 3U] = littleEndian[3];
    return true;
}

/// @brief 从正式布局取出三块区域。
/// @param manifestPath 正式 manifest 路径。
/// @param name 失败时打印的用例名。
/// @return 布局和目录都成功时为成功结果。失败时已经打印用例名，错误码只表示没有目录。
Result<RegionCatalog> openCatalog(const char* manifestPath, const char* name) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        fail(name);
        return Result<RegionCatalog>::failure(ErrorCode::InvalidParameter);
    }
    Result<RegionCatalog> built = RegionCatalog::load(loaded.layout());
    if (!built.ok()) {
        fail(name);
    }
    return built;
}

/// @brief 按路径找子字段。
/// @param region 父区域，只在本次调用期间借用。
/// @param path 完整路径。
/// @return 找到时指向内部子字段。没有时为空。
const SubRegion* findChild(const RegionDefinition& region, std::string_view path) {
    for (const SubRegion& child : region.subregions) {
        if (child.path == path) {
            return &child;
        }
    }
    return nullptr;
}

/// @brief SpecRadius 的首尾两个 float 来自手工小端字节。
/// @param manifestPath 正式 manifest。
/// @return 120 点、首值 1、尾值 2 时为 true。
bool specRadiusEndpoints(const char* manifestPath) {
    constexpr const char* name = "ernc_tf.spec_radius_endpoints";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    const RegionDefinition* transfer = catalog.value().find("ERNC_TF");
    if (transfer == nullptr) {
        return fail(name);
    }
    const SubRegion* radius = findChild(*transfer, "ERNC_TF.SpecRadius");
    if (radius == nullptr || radius->shape != std::vector<std::uint32_t>{120} || radius->size != 480U) {
        return fail(name);
    }
    std::vector<std::uint8_t> bytes(radius->size, 0);
    const std::uint8_t one[] = {0x00, 0x00, 0x80, 0x3F};
    const std::uint8_t two[] = {0x00, 0x00, 0x00, 0x40};
    if (!putBytes(bytes, 0, one) || !putBytes(bytes, 119, two)) {
        return fail(name);
    }
    const Result<DecodedField> decoded = ernc::domain::decodeErncTfField(*radius, bytes);
    if (!decoded.ok() || decoded.tryValue() == nullptr) {
        return fail(name);
    }
    const DecodedField& field = decoded.value();
    if (field.path != radius->path || field.shape != radius->shape || field.values.size() != 120U || field.values.front() != 1.0F
        || field.values.back() != 2.0F || field.values[1] != 0.0F) {
        return fail(name);
    }
    return true;
}

/// @brief 整段 ERNC_TF 切成四段，shape 与目录相同。
/// @param manifestPath 正式 manifest。
/// @return 首字段第一个值是 1、末字段最后一个值是 2 时为 true。
bool parentKeepsFourShapes(const char* manifestPath) {
    constexpr const char* name = "ernc_tf.parent_keeps_four_shapes";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    const RegionDefinition* transfer = catalog.value().find("ERNC_TF");
    if (transfer == nullptr || transfer->subregions.size() != 4U) {
        return fail(name);
    }
    std::vector<std::uint8_t> bytes(transfer->size, 0);
    const std::uint8_t one[] = {0x00, 0x00, 0x80, 0x3F};
    const std::uint8_t two[] = {0x00, 0x00, 0x00, 0x40};
    if (!putBytes(bytes, 0, one) || !putBytes(bytes, transfer->slotCount - 1U, two)) {
        return fail(name);
    }
    const Result<DecodedRegion> decoded = ernc::domain::decodeErncTf(*transfer, bytes);
    if (!decoded.ok() || decoded.tryValue() == nullptr) {
        return fail(name);
    }
    const DecodedRegion& region = decoded.value();
    if (region.path != "ERNC_TF" || region.fields.size() != 4U || region.fields.front().values.empty()
        || region.fields.front().values.front() != 1.0F || region.fields.back().values.empty()
        || region.fields.back().values.back() != 2.0F) {
        return fail(name);
    }
    for (std::size_t index = 0; index < region.fields.size(); ++index) {
        const DecodedField& field = region.fields[index];
        const SubRegion& child = transfer->subregions[index];
        if (field.path != child.path || field.shape != child.shape || field.values.size() != child.slotCount) {
            return fail(name);
        }
    }
    if (region.fields[0].shape != std::vector<std::uint32_t>{6, 6, 120} || region.fields[3].shape != std::vector<std::uint32_t>{6, 6, 200}) {
        return fail(name);
    }
    return true;
}

/// @brief 少一个字节时不给出任何 float。
/// @param manifestPath 正式 manifest。
/// @return 错误码为 PayloadSizeMismatch 时为 true。
bool shortFieldRejects(const char* manifestPath) {
    constexpr const char* name = "ernc_tf.short_field_rejects";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    const RegionDefinition* transfer = catalog.value().find("ERNC_TF");
    const SubRegion* radius = transfer == nullptr ? nullptr : findChild(*transfer, "ERNC_TF.SpecRadius");
    if (radius == nullptr || radius->size == 0U) {
        return fail(name);
    }
    const std::vector<std::uint8_t> bytes(radius->size - 1U, 0);
    const Result<DecodedField> decoded = ernc::domain::decodeErncTfField(*radius, bytes);
    if (decoded.ok() || decoded.tryValue() != nullptr || decoded.tryError() == nullptr || *decoded.tryError() != ErrorCode::PayloadSizeMismatch) {
        return fail(name);
    }
    return true;
}

/// @brief shape 乘积和 slot 数不一致时拒绝。
/// @return 错误码为 InvalidParameter 时为 true。
bool wrongShapeRejects() {
    constexpr const char* name = "ernc_tf.wrong_shape_rejects";
    SubRegion field;
    field.path = "ERNC_TF.SpecRadius";
    field.slotCount = 120;
    field.size = 480;
    field.shape = {6, 6, 120};
    field.scalarType = DataType::Float32;
    const std::vector<std::uint8_t> bytes(field.size, 0);
    const Result<DecodedField> decoded = ernc::domain::decodeErncTfField(field, bytes);
    if (decoded.ok() || decoded.tryError() == nullptr || *decoded.tryError() != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    return true;
}

/// @brief check_data 不能交给 ERNC_TF 解码。
/// @param manifestPath 正式 manifest。
/// @return 错误码为 InvalidParameter 时为 true。
bool checkDataRegionRejects(const char* manifestPath) {
    constexpr const char* name = "ernc_tf.check_data_region_rejects";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    const RegionDefinition* check = catalog.value().find("check_data");
    if (check == nullptr) {
        return fail(name);
    }
    const Result<DecodedRegion> decoded = ernc::domain::decodeErncTf(*check, {});
    if (decoded.ok() || decoded.tryError() == nullptr || *decoded.tryError() != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。
/// @param manifestPath 正式 manifest。wrong_shape 不读取它。
/// @return 用例通过时为 true。
bool runCase(std::string_view name, const char* manifestPath) {
    if (name == "ernc_tf.spec_radius_endpoints") {
        return specRadiusEndpoints(manifestPath);
    }
    if (name == "ernc_tf.parent_keeps_four_shapes") {
        return parentKeepsFourShapes(manifestPath);
    }
    if (name == "ernc_tf.short_field_rejects") {
        return shortFieldRejects(manifestPath);
    }
    if (name == "ernc_tf.wrong_shape_rejects") {
        return wrongShapeRejects();
    }
    if (name == "ernc_tf.check_data_region_rejects") {
        return checkDataRegionRejects(manifestPath);
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_region_decode_tests <case> <layout_manifest.json>\n";
        return 2;
    }
    return runCase(argv[1], argv[2]) ? 0 : 1;
}
