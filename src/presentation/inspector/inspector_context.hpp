#pragma once

#include "ernc/domain/catalog.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <string>
#include <string_view>

namespace ernc::presentation {

/// 检查器三个标签的一段显示文字。文字由当次查询生成，不是另一份参数值。
struct InspectorText {
    /// Properties 标签的全文。字段之间用换行分隔。
    std::string properties;

    /// Info 标签的全文。说明与 Properties 读取同一条目录字段。
    std::string info;

    /// Diagnostics 标签的全文。本切片没有命令或报文记录。
    std::string diagnostics;
};

/// @brief 从目录和实时工作区生成三个标签的文字。
/// @param parameterId 当前参数 id。空文本表示还没有选择。只在本次调用期间借用。
/// @param definition 目录定义。空指针表示目录里没有这一条，或目录不可用。调用方保留所有权。
/// @param state 本次调用借用的运行态。空指针表示没有观察或暂存记录。
/// @return 三个标签的文本。调用方拥有返回值。本函数不保存 Read 或 Set。
InspectorText describeInspector(
    std::string_view parameterId,
    const ernc::domain::ParameterDefinition* definition,
    const ernc::domain::ParameterRuntimeState* state);

} // namespace ernc::presentation
