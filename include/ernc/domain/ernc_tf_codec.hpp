#pragma once

#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/region_decode.hpp"

#include <cstdint>
#include <span>

namespace ernc::domain {

/// @brief 把 ERNC_TF 的一个子字段从原始字节解成 float。
///
/// 只接受实部、虚部、SpecRadius 和 ENCTF。字节顺序是低字节在前的 IEEE-754 binary32，
/// 与 typed codec 的 float32 观察一致。证据等级是旧版源码观察，没有已批准黄金 BIN。
/// 不访问设备，也不给维度轴命名。
/// @param field 目录中的子字段，只在本次调用期间借用。shape 和长度以它为准。
/// @param bytes 该字段的原始字节，只在本次调用期间借用。长度必须等于 field.size。
/// @return 成功时调用方拥有路径、shape 和存储顺序的 float。路径不属于这四个字段，
/// 或维度和 slot 数对不上时为 InvalidParameter。字节数不对时为 PayloadSizeMismatch，没有半段数值。
Result<DecodedField> decodeErncTfField(const SubRegion& field, std::span<const std::uint8_t> bytes);

/// @brief 把一整段 ERNC_TF 字节按四个子字段切开并解码。
///
/// 子字段顺序和长度来自 region。任何一段失败时，不返回已经解开的其它段。
/// @param region 路径必须是 ERNC_TF，并且正好带有那四个子字段。只在本次调用期间借用。
/// @param bytes 父区域的原始字节，只在本次调用期间借用。长度必须等于 region.size。
/// @return 成功时调用方拥有四段字段。区域不对为 InvalidParameter。长度不对为 PayloadSizeMismatch。
Result<DecodedRegion> decodeErncTf(const RegionDefinition& region, std::span<const std::uint8_t> bytes);

}  // namespace ernc::domain
