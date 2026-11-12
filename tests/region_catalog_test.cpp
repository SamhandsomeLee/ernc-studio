#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ernc::domain::DataType;
using ernc::domain::ErrorCode;
using ernc::domain::FieldLayout;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutEvidence;
using ernc::domain::LayoutLoad;
using ernc::domain::LayoutResolve;
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

/// @brief 包一份最小 manifest。total_size 必须等于 total_slots 乘 4。
/// @param totalSlots 镜像 slot 数。
/// @param fields fields 数组内部的 JSON，不含外层方括号。只在本次调用期间借用。
/// @return 调用方拥有的完整 JSON 文本。
std::string manifest(unsigned totalSlots, std::string_view fields) {
    std::string text = R"({"alignment":4,"base":0,"total_size":)";
    text.append(std::to_string(totalSlots * 4U));
    text.append(R"(,"total_slots":)");
    text.append(std::to_string(totalSlots));
    text.append(R"(,"fields":[)");
    text.append(fields);
    text.append("]}");
    return text;
}

/// @brief 子字段的几何是否和 locate() 相同，并且基线 pack4 允许区域读写。
/// @param layout 正式布局。
/// @param child 目录中的子字段，只在本次调用期间借用。
/// @param name 用例名。
/// @return 路径、slot、字节、shape 和证据都一致，且真实读写关闭时为 true。
bool childMatchesLayout(const FirmwareLayout& layout, const SubRegion& child, const char* name) {
    const LayoutResolve located = layout.locate(child.path);
    if (!located.ok()) {
        return fail(name);
    }
    const FieldLayout& field = located.field();
    if (field.slot != child.slot || field.slotCount != child.slotCount || field.size != child.size || field.offset != child.offset
        || field.shape != child.shape || field.typeName != child.typeName || field.evidence != child.evidence
        || field.writable != child.writable || field.evidence != LayoutEvidence::Pack4Sequential || child.writable
        || !ernc::domain::realReadPermitted(child.evidence) || !ernc::domain::realWritePermitted(child.evidence, child.writable)) {
        return fail(name);
    }
    return true;
}

/// @brief 正式布局里的三块区域与 locate() 一致，基线几何允许区域读写。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return 子字段数量、顺序、维度和覆盖关系都符合这份布局时为 true。
bool manifestRegionsMatchLayout(const char* manifestPath) {
    constexpr const char* name = "manifest_regions_match_layout";
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail(name);
    }
    const FirmwareLayout& layout = loaded.layout();
    const Result<RegionCatalog> catalog = RegionCatalog::load(layout);
    if (!catalog.ok() || catalog.tryValue() == nullptr || catalog.value().size() != 3U || catalog.value().find("Can_Msg") != nullptr) {
        return fail(name);
    }
    const RegionDefinition* transfer = catalog.value().find("ERNC_TF");
    const RegionDefinition* check = catalog.value().find("check_data");
    const RegionDefinition* radius = catalog.value().find("ERNC_TF.SpecRadius");
    if (transfer == nullptr || check == nullptr || radius == nullptr || transfer->subregions.size() != 4U || check->subregions.size() != 20U
        || !radius->subregions.empty() || transfer->shape.empty() == false || radius->shape != std::vector<std::uint32_t>{120}
        || radius->slotCount != 120U || radius->size != 480U || !radius->scalarType || *radius->scalarType != DataType::Float32) {
        return fail(name);
    }
    if (transfer->subregions[0].path != "ERNC_TF.fSecPathTransR" || transfer->subregions[0].shape != std::vector<std::uint32_t>{6, 6, 120}
        || transfer->subregions[2].path != "ERNC_TF.SpecRadius" || transfer->subregions[3].path != "ERNC_TF.ENCTF"
        || check->subregions.front().path != "check_data.OutputCheck" || check->subregions.front().shape != std::vector<std::uint32_t>{6, 200}
        || check->subregions.back().path != "check_data.RNCAdapCheck") {
        return fail(name);
    }
    const LayoutResolve parent = layout.locate(transfer->path);
    if (!parent.ok() || parent.field().slot != transfer->slot || parent.field().slotCount != transfer->slotCount
        || parent.field().size != transfer->size || !ernc::domain::realReadPermitted(transfer->evidence)
        || !ernc::domain::realWritePermitted(transfer->evidence, transfer->writable)) {
        return fail(name);
    }
    const SubRegion& first = transfer->subregions.front();
    const SubRegion& last = transfer->subregions.back();
    if (first.slot != transfer->slot || first.offset != transfer->offset
        || last.slot + last.slotCount != transfer->slot + transfer->slotCount
        || last.offset + last.size != transfer->offset + transfer->size) {
        return fail(name);
    }
    for (const SubRegion& child : transfer->subregions) {
        if (!childMatchesLayout(layout, child, name)) {
            return false;
        }
    }
    for (const SubRegion& child : check->subregions) {
        if (!childMatchesLayout(layout, child, name)) {
            return false;
        }
    }
    const LayoutResolve radiusField = layout.locate(radius->path);
    if (!radiusField.ok() || radiusField.field().shape != radius->shape || radiusField.field().slot != radius->slot) {
        return fail(name);
    }
    return true;
}

/// @brief 子字段中间有空洞时，整份目录失败。
/// @return 错误码为 InvalidParameter，且没有目录对象时为 true。
bool gapRejectsTheCatalog() {
    constexpr const char* name = "gap_rejects_the_catalog";
    const std::string text = manifest(4, R"(
        {"path":"ERNC_TF","offset":0,"size":12,"slot":0,"slot_count":3,"type":"ANC_TransFunc_f","shape":[],"status":"pack4-sequential","writable":false,"children":[
            {"path":"ERNC_TF.SpecRadius","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[1],"status":"pack4-sequential","writable":false},
            {"path":"ERNC_TF.ENCTF","offset":8,"size":4,"slot":2,"slot_count":1,"type":"float","shape":[1],"status":"pack4-sequential","writable":false}
        ]},
        {"path":"check_data","offset":12,"size":4,"slot":3,"slot_count":1,"type":"Check_data","shape":[],"status":"pack4-sequential","writable":false,"children":[
            {"path":"check_data.OutputCheck","offset":12,"size":4,"slot":3,"slot_count":1,"type":"float","shape":[1],"status":"pack4-sequential","writable":false}
        ]}
    )");
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(text);
    if (!loaded.ok()) {
        return fail(name);
    }
    const Result<RegionCatalog> catalog = RegionCatalog::load(loaded.layout());
    if (catalog.ok() || catalog.tryValue() != nullptr || catalog.tryError() == nullptr || *catalog.tryError() != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    return true;
}

/// @brief 已核验区域可以真实读取；真实写入还要 writable 为 true。
/// @return ERNC_TF 可写、check_data 只允许真实读时为 true。
bool verifiedWriteFollowsWritable() {
    constexpr const char* name = "verified_write_follows_writable";
    const std::string text = manifest(4, R"(
        {"path":"ERNC_TF","offset":0,"size":8,"slot":0,"slot_count":2,"type":"ANC_TransFunc_f","shape":[],"status":"verified","writable":true,"children":[
            {"path":"ERNC_TF.SpecRadius","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[1],"status":"verified","writable":true},
            {"path":"ERNC_TF.ENCTF","offset":4,"size":4,"slot":1,"slot_count":1,"type":"float","shape":[1],"status":"verified","writable":true}
        ]},
        {"path":"check_data","offset":8,"size":8,"slot":2,"slot_count":2,"type":"Check_data","shape":[],"status":"verified","writable":false,"children":[
            {"path":"check_data.OutputCheck","offset":8,"size":8,"slot":2,"slot_count":2,"type":"float","shape":[2],"status":"verified","writable":false}
        ]}
    )");
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(text);
    if (!loaded.ok()) {
        return fail(name);
    }
    const Result<RegionCatalog> catalog = RegionCatalog::load(loaded.layout());
    if (!catalog.ok() || catalog.tryValue() == nullptr) {
        return fail(name);
    }
    const RegionDefinition* transfer = catalog.value().find("ERNC_TF");
    const RegionDefinition* check = catalog.value().find("check_data");
    const RegionDefinition* radius = catalog.value().find("ERNC_TF.SpecRadius");
    if (transfer == nullptr || check == nullptr || radius == nullptr || transfer->subregions.size() != 2U || check->subregions.size() != 1U
        || !radius->subregions.empty()) {
        return fail(name);
    }
    if (!ernc::domain::realReadPermitted(transfer->evidence) || !ernc::domain::realWritePermitted(transfer->evidence, transfer->writable)
        || !ernc::domain::realReadPermitted(check->evidence) || !ernc::domain::realWritePermitted(check->evidence, check->writable)
        || !ernc::domain::realReadPermitted(radius->evidence) || !ernc::domain::realWritePermitted(radius->evidence, radius->writable)
        || check->subregions[0].shape != std::vector<std::uint32_t>{2}) {
        return fail(name);
    }
    return true;
}

/// @brief 缺少规定区域时不建立目录。
/// @return 只有一个无关字段的布局被拒绝时为 true。
bool missingRegionRejectsTheCatalog() {
    constexpr const char* name = "missing_region_rejects_the_catalog";
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(manifest(
        1, R"({"path":"ANC_OFF","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false})"));
    if (!loaded.ok()) {
        return fail(name);
    }
    const Result<RegionCatalog> catalog = RegionCatalog::load(loaded.layout());
    if (catalog.ok() || catalog.tryValue() != nullptr || catalog.tryError() == nullptr || *catalog.tryError() != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。
/// @param manifestPath 正式 manifest。合成布局的用例不读取它。
/// @return 用例通过时为 true。
bool runCase(std::string_view name, const char* manifestPath) {
    if (name == "manifest_regions_match_layout") {
        return manifestRegionsMatchLayout(manifestPath);
    }
    if (name == "gap_rejects_the_catalog") {
        return gapRejectsTheCatalog();
    }
    if (name == "verified_write_follows_writable") {
        return verifiedWriteFollowsWritable();
    }
    if (name == "missing_region_rejects_the_catalog") {
        return missingRegionRejectsTheCatalog();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_region_catalog_tests <case> <layout_manifest.json>\n";
        return 2;
    }
    return runCase(argv[1], argv[2]) ? 0 : 1;
}
