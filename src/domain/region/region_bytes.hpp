#pragma once

#include "ernc/domain/region_decode.hpp"
#include "ernc/domain/region_catalog.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ernc::domain::detail {

/// @brief 判断 shape 是否和 slot 数一致。
/// @param shape 目录给出的各级长度。空只允许一个 slot 的标量。
/// @param slotCount 字段的 slot 数。0 不是合法字段。
/// @return 空 shape 且只有 1 个 slot，或各级乘积等于 slotCount 时为 true。
inline bool shapeMatches(const std::vector<std::uint32_t>& shape, std::uint32_t slotCount) noexcept {
    if (slotCount == 0 || slotCount > (std::numeric_limits<std::uint32_t>::max() / 4U)) {
        return false;
    }
    if (shape.empty()) {
        return slotCount == 1U;
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

/// @brief 字段几何能否按 float 解码。
/// @param scalarType 目录上的标量类型。空或不是 float32 时拒绝。
/// @param shape 各级长度。
/// @param slotCount slot 数。
/// @param size 字节长度。必须等于 slotCount 乘 4。
/// @return 可以解码时为 true。
inline bool floatGeometryOk(const std::optional<DataType>& scalarType, const std::vector<std::uint32_t>& shape,
                            std::uint32_t slotCount, std::uint32_t size) noexcept {
    return scalarType && *scalarType == DataType::Float32 && shapeMatches(shape, slotCount) && size == slotCount * 4U;
}

/// @brief 把低字节在前的 4 字节还原成 float。
/// @param bytes 恰好 4 字节，只在本次调用期间借用。下标 0 是最低字节。
/// @return IEEE-754 binary32。NaN 和无穷大保持原位模式。
inline float loadFloat(std::span<const std::uint8_t, 4> bytes) noexcept {
    const auto bits = static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                      (static_cast<std::uint32_t>(bytes[2]) << 16U) | (static_cast<std::uint32_t>(bytes[3]) << 24U);
    return std::bit_cast<float>(bits);
}

/// @brief 在允许名单里查找路径。
/// @param path 字段路径。
/// @param allowed 这一片接受的路径。
/// @return 找到时为下标。没有时为 allowed.size()。
template <std::size_t Count>
inline std::size_t indexOf(std::string_view path, const std::array<std::string_view, Count>& allowed) noexcept {
    for (std::size_t index = 0; index < allowed.size(); ++index) {
        if (allowed[index] == path) {
            return index;
        }
    }
    return allowed.size();
}

/// @brief 解码一个已经确认属于本片的 float 字段。
/// @param field 子字段，只在本次调用期间借用。调用前路径已经核对过。
/// @param bytes 原始字节，只在本次调用期间借用。
/// @return 几何不符为 InvalidParameter。长度不符为 PayloadSizeMismatch。成功时调用方拥有数值。
inline Result<DecodedField> decodeCheckedField(const SubRegion& field, std::span<const std::uint8_t> bytes) {
    if (!floatGeometryOk(field.scalarType, field.shape, field.slotCount, field.size)) {
        return Result<DecodedField>::failure(ErrorCode::InvalidParameter);
    }
    if (bytes.size() != field.size) {
        return Result<DecodedField>::failure(ErrorCode::PayloadSizeMismatch);
    }
    DecodedField decoded;
    decoded.path = field.path;
    decoded.shape = field.shape;
    decoded.values.reserve(field.slotCount);
    for (std::uint32_t index = 0; index < field.slotCount; ++index) {
        const std::size_t offset = static_cast<std::size_t>(index) * 4U;
        decoded.values.push_back(loadFloat(std::span<const std::uint8_t, 4>{bytes.data() + offset, 4}));
    }
    return Result<DecodedField>::success(std::move(decoded));
}

/// @brief 按子字段顺序解开一整块区域。路径名单不齐时不看字节。
/// @param region 父区域，只在本次调用期间借用。
/// @param bytes 父区域原始字节，只在本次调用期间借用。
/// @param parent 这一片唯一接受的父路径。
/// @param allowed 必须各出现一次的子路径。
/// @return 父路径或子字段集合不对为 InvalidParameter。父字节长度不对为 PayloadSizeMismatch。
template <std::size_t Count>
Result<DecodedRegion> decodeCheckedRegion(const RegionDefinition& region, std::span<const std::uint8_t> bytes,
                                          std::string_view parent, const std::array<std::string_view, Count>& allowed) {
    if (region.path != parent || region.subregions.size() != allowed.size()) {
        return Result<DecodedRegion>::failure(ErrorCode::InvalidParameter);
    }
    std::array<bool, Count> seen{};
    std::uint64_t total = 0;
    for (const SubRegion& child : region.subregions) {
        const std::size_t index = indexOf(child.path, allowed);
        if (index == allowed.size() || seen[index] || !floatGeometryOk(child.scalarType, child.shape, child.slotCount, child.size)) {
            return Result<DecodedRegion>::failure(ErrorCode::InvalidParameter);
        }
        seen[index] = true;
        total += child.size;
    }
    if (total != region.size) {
        return Result<DecodedRegion>::failure(ErrorCode::InvalidParameter);
    }
    if (bytes.size() != region.size) {
        return Result<DecodedRegion>::failure(ErrorCode::PayloadSizeMismatch);
    }
    DecodedRegion decoded;
    decoded.path = region.path;
    decoded.fields.reserve(region.subregions.size());
    std::size_t offset = 0;
    for (const SubRegion& child : region.subregions) {
        const std::span<const std::uint8_t> slice{bytes.data() + offset, child.size};
        Result<DecodedField> field = decodeCheckedField(child, slice);
        if (!field.ok()) {
            return Result<DecodedRegion>::failure(field.error());
        }
        decoded.fields.push_back(std::move(*field.tryValue()));
        offset += child.size;
    }
    return Result<DecodedRegion>::success(std::move(decoded));
}

}  // namespace ernc::domain::detail
