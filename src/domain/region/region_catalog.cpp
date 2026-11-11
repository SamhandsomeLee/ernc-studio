#include "ernc/domain/region_catalog.hpp"

#include <array>
#include <limits>
#include <string_view>
#include <utility>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

/// 本目录固定收录的三条固件路径。SpecRadius 没有单独的顶层路径。
constexpr std::array<std::string_view, 3> kRegionPaths{"ERNC_TF", "check_data", "ERNC_TF.SpecRadius"};

/// @brief 非空 shape 的乘积必须等于 slot 数。
/// @param shape 各级长度，只在本次调用期间借用。空表示没有维度要求。
/// @param slotCount 字段的 slot 数。
/// @return 空 shape，或乘积正好等于 slotCount 时为 true。某一维为 0 或乘积溢出时为 false。
bool shapeMatches(const std::vector<std::uint32_t>& shape, std::uint32_t slotCount) noexcept {
    if (shape.empty()) {
        return true;
    }
    std::uint32_t product = 1;
    for (const std::uint32_t dimension : shape) {
        if (dimension == 0 || product > (std::numeric_limits<std::uint32_t>::max() / dimension)) {
            return false;
        }
        product *= dimension;
    }
    return product == slotCount;
}

/// @brief 用一次 locate 结果填区域的公共几何。
/// @param field 已经成功的几何，只在本次调用期间借用。
/// @param path 固件路径。按值复制进结果。
/// @param region 要填写的区域。
void fillCommon(const FieldLayout& field, std::string path, RegionDefinition& region) {
    region.path = std::move(path);
    region.slot = field.slot;
    region.slotCount = field.slotCount;
    region.size = field.size;
    region.offset = field.offset;
    region.typeName = field.typeName;
    region.scalarType = field.scalarType;
    region.shape = field.shape;
    region.evidence = field.evidence;
    region.writable = field.writable;
}

/// @brief 用一次 locate 结果填子字段。
/// @param field 已经成功的几何，只在本次调用期间借用。
/// @param path 子字段完整路径。按值复制。
/// @return 调用方拥有的子字段。
SubRegion makeSubRegion(const FieldLayout& field, std::string path) {
    SubRegion child;
    child.path = std::move(path);
    child.slot = field.slot;
    child.slotCount = field.slotCount;
    child.size = field.size;
    child.offset = field.offset;
    child.typeName = field.typeName;
    child.scalarType = field.scalarType;
    child.shape = field.shape;
    child.evidence = field.evidence;
    child.writable = field.writable;
    return child;
}

/// @brief 子字段是否从父区域起点排到终点，中间没有空洞或重叠。
/// @param region 已经填好的区域，只在本次调用期间借用。
/// @return 没有子字段时只检查自己的 shape。有子字段时，第一段对齐父起点，相邻段首尾相接，最后一段对齐父终点。
bool tiles(const RegionDefinition& region) noexcept {
    if (!shapeMatches(region.shape, region.slotCount)) {
        return false;
    }
    if (region.subregions.empty()) {
        return true;
    }
    const SubRegion* previous = nullptr;
    for (const SubRegion& child : region.subregions) {
        if (!shapeMatches(child.shape, child.slotCount)) {
            return false;
        }
        if (previous == nullptr) {
            if (child.slot != region.slot || child.offset != region.offset) {
                return false;
            }
        } else if (child.slot != previous->slot + previous->slotCount || child.offset != previous->offset + previous->size) {
            return false;
        }
        previous = &child;
    }
    const SubRegion& last = region.subregions.back();
    return last.slot + last.slotCount == region.slot + region.slotCount && last.offset + last.size == region.offset + region.size;
}

/// @brief 读取一块区域和它的直接子字段。
/// @param layout 固件布局，只在本次调用期间借用。
/// @param path 区域路径。
/// @param region 成功时写入。失败时不作为目录的一部分发布。
/// @return 路径能 locate，每个直接子字段也能 locate，并且子字段盖满父区域时为 true。
bool readRegion(const FirmwareLayout& layout, std::string_view path, RegionDefinition& region) {
    const LayoutResolve located = layout.locate(path);
    if (!located.ok()) {
        return false;
    }
    fillCommon(located.field(), std::string{path}, region);
    for (const std::string& childPath : layout.directChildren(path)) {
        const LayoutResolve child = layout.locate(childPath);
        if (!child.ok()) {
            return false;
        }
        region.subregions.push_back(makeSubRegion(child.field(), childPath));
    }
    return tiles(region);
}

}  // namespace

Result<RegionCatalog> RegionCatalog::load(const FirmwareLayout& layout) {
    std::vector<RegionDefinition> regions;
    regions.reserve(kRegionPaths.size());
    for (const std::string_view path : kRegionPaths) {
        RegionDefinition region;
        if (!readRegion(layout, path, region)) {
            return Result<RegionCatalog>::failure(ErrorCode::InvalidParameter);
        }
        regions.push_back(std::move(region));
    }
    return Result<RegionCatalog>::success(RegionCatalog{std::move(regions)});
}

const RegionDefinition* RegionCatalog::find(std::string_view path) const noexcept {
    for (const RegionDefinition& region : regions_) {
        if (region.path == path) {
            return &region;
        }
    }
    return nullptr;
}

std::size_t RegionCatalog::size() const noexcept {
    return regions_.size();
}

RegionCatalog::RegionCatalog(std::vector<RegionDefinition> regions) : regions_{std::move(regions)} {}

}  // namespace ernc::domain
