#pragma once

#include "ernc/domain/catalog.hpp"

namespace ernc::domain {

/// @brief 判断一个标量能不能作为普通暂存值。
///
/// 只读取调用方给出的目录定义和标量，不修改 RuntimeStore，不创建写请求，也不访问设备。
/// 上下限本阶段冻结：函数不读取、不比较任何范围。
/// 没有后台线程。definition 和 value 只在本次调用期间借用。
/// @param definition 目录中的一条参数。access 缺省表示权限未声明。
/// @param value 用户想暂存的标量。Float32、Int32、UInt32 不互相转换，没有物理单位。
/// @return 可以进入后续暂存时成功，且没有载荷。只读返回 ReadOnlyViolation。
///         命令、未声明权限、已声明类型不匹配或浮点不是有限数时返回 InvalidParameter。
Result<void> validateStagedValue(const ParameterDefinition& definition, const ScalarValue& value);

}  // namespace ernc::domain
