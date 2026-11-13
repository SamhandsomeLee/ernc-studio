#include "inspector/inspector_context.hpp"

#include "parameters/runtime_time_display.hpp"
#include "parameters/scalar_display.hpp"
#include <format>
#include <optional>

namespace ernc::presentation {
namespace {

/// 缺省字段显示的占位。它不是数值 0，也不是设备关闭。
constexpr std::string_view kMissing = "—";

/// @brief 在已有正文后追加一行「标签 值」。
/// @param out 正在组装的标签正文。本函数在原字符串后追加，不更换其所有权。
/// @param label 行首名称，只借用。
/// @param value 这一行的值，只借用。空值也应由调用方先换成占位。
void appendLine(std::string& out, std::string_view label, std::string_view value) {
    if (!out.empty()) {
        out.push_back('\n');
    }
    out.append(label);
    out.push_back(' ');
    out.append(value);
}

/// @brief 取出可选文本，没有或空白时用占位。
/// @param value 目录里的可选字符串。空 optional 和空字符串都表示目录没有这项。
/// @return 目录原文或占位。返回的视图指向参数或静态占位，只在参数存活期间有效。
std::string_view textOrMissing(const std::optional<std::string>& value) {
    if (!value.has_value() || value->empty()) {
        return kMissing;
    }
    return *value;
}

/// @brief 目录数据类型的显示名。
/// @param dataType 目录声明的标量类型。没有声明时不调用本函数。
/// @return JSON 里的类型名，指向静态字面量。
std::string_view dataTypeName(ernc::domain::DataType dataType) {
    switch (dataType) {
    case ernc::domain::DataType::Float32:
        return "32 位浮点";
    case ernc::domain::DataType::Int32:
        return "32 位有符号整数";
    case ernc::domain::DataType::UInt32:
        return "32 位无符号整数";
    }
    return kMissing;
}

/// @brief 目录值形状的显示名。
/// @param shape 目录声明的形状。没有声明时不调用本函数。
/// @return JSON 里的形状名，指向静态字面量。
std::string_view shapeName(ernc::domain::ValueShape shape) {
    switch (shape) {
    case ernc::domain::ValueShape::Scalar:
        return "标量";
    case ernc::domain::ValueShape::Array:
        return "数组";
    case ernc::domain::ValueShape::Region:
        return "区域";
    }
    return kMissing;
}

/// @brief 目录访问声明的显示名。
/// @param access 目录声明的权限。没有声明时不调用本函数。
/// @return JSON 里的权限名，指向静态字面量。
std::string_view accessName(ernc::domain::AccessMode access) {
    switch (access) {
    case ernc::domain::AccessMode::ReadOnly:
        return "只读";
    case ernc::domain::AccessMode::ReadWrite:
        return "读写";
    case ernc::domain::AccessMode::WriteOnly:
        return "只写";
    case ernc::domain::AccessMode::Command:
        return "命令";
    }
    return kMissing;
}

/// @brief 把目录线值写成十进制整数文本。
/// @param value 目录里的 on 或 off 线值，没有物理单位。非整数时退回默认十进制。
/// @return 调用方拥有的短文本。0 和 1 写成 `0`、`1`，不写成带小数的设备读数。
std::string formatWireLevel(double value) {
    const long long whole = static_cast<long long>(value);
    if (static_cast<double>(whole) == value) {
        return std::to_string(whole);
    }
    return std::to_string(value);
}

} // namespace

InspectorText describeInspector(
    std::string_view parameterId,
    const ernc::domain::ParameterDefinition* definition,
    const ernc::domain::ParameterRuntimeState* state) {
    const std::string_view shownId = parameterId.empty() ? std::string_view{"未选择参数"} : parameterId;
    const std::string_view description = definition == nullptr ? kMissing : textOrMissing(definition->descriptionZh);
    const std::string_view path = definition == nullptr ? kMissing : textOrMissing(definition->firmwarePath);
    const std::string_view category = definition == nullptr ? kMissing : textOrMissing(definition->category);
    const std::string_view dataType = definition == nullptr || !definition->dataType.has_value()
        ? kMissing
        : dataTypeName(*definition->dataType);
    const std::string_view shape = definition == nullptr || !definition->shape.has_value()
        ? kMissing
        : shapeName(*definition->shape);
    const std::string_view access = definition == nullptr || !definition->access.has_value()
        ? kMissing
        : accessName(*definition->access);
    const auto* read = state == nullptr ? nullptr : state->displayedRead();
    const auto* set = state != nullptr && state->stagedValue ? &*state->stagedValue : nullptr;
    std::string_view status = parameterId.empty() ? kMissing : "未读取";
    std::string_view source = definition == nullptr ? kMissing : "参数目录";
    std::optional<ernc::domain::RuntimeTime> timestamp;
    if (state != nullptr) {
        if (state->projectReadValue) {
            status = "工程历史读数";
            source = "工程文件";
        } else if (read != nullptr) {
            status = state->stale ? "数据已过期" : "已读取";
            source = state->stale ? "设备缓存" : "设备读回";
            timestamp = state->readAt;
        } else if (set != nullptr) {
            source = "本地暂存";
        }
        if (state->dirty && !state->projectReadValue && (read == nullptr || !state->stale)) {
            status = "待写入";
        }
        if (!timestamp) {
            timestamp = state->stagedAt;
        }
    }
    const std::string updatedAt =
        timestamp ? formatRuntimeTimeBeijingUtf8(*timestamp) : std::string(kMissing);

    InspectorText text;
    appendLine(text.properties, "参数", shownId);
    appendLine(text.properties, "读", displayScalar(read));
    appendLine(text.properties, "写", displayScalar(set));
    appendLine(text.properties, "状态", status);
    appendLine(text.properties, "说明", description);
    appendLine(text.properties, "路径", path);
    appendLine(text.properties, "类型", dataType);
    appendLine(text.properties, "形状", shape);
    appendLine(text.properties, "权限", access);

    appendLine(text.info, "来源", source);
    appendLine(text.info, "分类", category);
    appendLine(text.info, "说明", description);
    appendLine(text.info, "更新时间", updatedAt);
    if (definition != nullptr && definition->wireLevels.has_value()) {
        const std::string on = formatWireLevel(definition->wireLevels->on);
        const std::string off = formatWireLevel(definition->wireLevels->off);
        text.info.append("\n目录线值 on ");
        text.info.append(on);
        text.info.append("，off ");
        text.info.append(off);
        text.info.append("。这不是设备当前值。");
    }

    appendLine(text.diagnostics, "当前参数", shownId);
    text.diagnostics.append(state != nullptr && state->error ? "\n上次读取失败，可在操作中心查看详情。"
                                                           : "\n在操作中心查看会话记录。");
    return text;
}

} // namespace ernc::presentation
