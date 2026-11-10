#pragma once

#include "ernc/domain/catalog.hpp"

namespace ernc::domain {

/// 已确认 toggle 的开关状态。Unknown 表示线值对不上，不能显示成开或关。
enum class TogglePosition {
    /// 线值等于目录里的 on。ANC_OFF、ENC_OFF、RNC_OFF 的 on 是 0。
    On,

    /// 线值等于目录里的 off。上述三个开关的 off 是 1。
    Off,

    /// 没有已确认的 on/off，或当前数值对不上。0 和 1 在这里也不表示开或关。
    Unknown
};

/// @brief 把设备标量解释成开关状态。
///
/// 只读取目录里的 editor、data_type 和 wire_semantics。不按参数名分支，不修改 Store，也不创建写请求。
/// Float32、Int32、UInt32 不互相转换。definition 和 value 只在本次调用期间借用。
/// @param definition 目录中的一条参数。没有 toggle 线值时，任何标量都解释为 Unknown。
/// @param value 设备读数或用户给出的标量。没有物理单位。
/// @return On、Off 或 Unknown。未知结果不是失败码。
TogglePosition decodeToggle(const ParameterDefinition& definition, const ScalarValue& value);

/// @brief 把用户选择的开或关编成目录声明的线值。
///
/// 只在 editor 为 toggle 且 wire_semantics 存在时编码。没有线值时不猜测 0 或 1。
/// definition 只在本次调用期间借用。函数不写 Store，也不创建写请求。
/// @param definition 目录中的一条参数。data_type 决定生成的标量种类。
/// @param position 用户选择。Unknown 不能编码。
/// @return 成功时持有对应种类的 ScalarValue。线值无法按声明种类精确表示，或映射未确认时返回 InvalidParameter。
Result<ScalarValue> encodeToggle(const ParameterDefinition& definition, TogglePosition position);

}  // namespace ernc::domain
