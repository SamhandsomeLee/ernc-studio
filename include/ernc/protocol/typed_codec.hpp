#pragma once

#include "ernc/domain/types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ernc::protocol {

/// @brief 把一个标量写成 4 个小端字节。
///
/// 字节顺序来自旧版 struct.pack("<f")、"<i" 和 "<I"。低字节在前。
/// 按 value 自己的种类取 32 位模式，不在 float、int32、uint32 之间转换。
/// NaN、无穷大和负零保留原有位模式，不在这里拒绝。非有限浮点能否进入待写值仍由 validateStagedValue 决定。
/// 证据等级：旧版源码观察。没有已批准 Golden。不访问设备。
/// @param value 要编码的标量，只在本次调用期间借用。没有物理单位。
/// @return 调用方拥有的 4 字节。任何合法 ScalarValue 都能编码，包括 NaN。
std::array<std::uint8_t, 4> encodeScalar(const ernc::domain::ScalarValue& value) noexcept;

/// @brief 按指定种类把恰好 4 个小端字节还原成标量。
///
/// 同 4 个字节可以按不同种类解出不同的值。本函数每次只使用传入的 kind，不同时给出另外两种视图。
/// NaN 和无穷大解码成功，位模式保持不变。
/// @param bytes 小端字节，只在本次调用期间借用。长度必须是 4。
/// @param kind 要还原的种类。它不是从字节里推断出来的线上类型码。
/// @return 长度正确时成功，结果是该种类的 ScalarValue。长度不是 4 时错误码为 PayloadSizeMismatch。
ernc::domain::Result<ernc::domain::ScalarValue> decodeScalar(std::span<const std::uint8_t> bytes,
                                                             ernc::domain::ScalarValue::Kind kind);

/// @brief 把同一种类的一组标量接成小端数据区。
///
/// 每个值占 4 字节，顺序与 values 相同。空输入得到空缓冲区。
/// @param values 标量序列，只在本次调用期间借用。空序列没有种类要求。非空时每个值的种类必须和第一个相同。
/// @return 成功时调用方拥有拼接后的字节。种类不一致时错误码为 InvalidParameter，没有半截输出。
/// 分配失败时抛出 std::bad_alloc。
ernc::domain::Result<std::vector<std::uint8_t>> encodeScalars(std::span<const ernc::domain::ScalarValue> values);

/// @brief 按同一种类把数据区拆成若干标量。
///
/// 每 4 字节一个值。空输入得到空序列。
/// @param bytes 小端数据区，只在本次调用期间借用。长度必须是 4 的倍数。
/// @param kind 每个值使用的同一种类。
/// @return 长度合法时成功，调用方拥有标量序列。长度不是 4 的倍数时错误码为 PayloadSizeMismatch。
/// 分配失败时抛出 std::bad_alloc。
ernc::domain::Result<std::vector<ernc::domain::ScalarValue>> decodeScalars(std::span<const std::uint8_t> bytes,
                                                                           ernc::domain::ScalarValue::Kind kind);

}  // namespace ernc::protocol
