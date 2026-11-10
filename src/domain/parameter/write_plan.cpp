#include "ernc/domain/write_plan.hpp"

#include <utility>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

/// @brief 判断这条定义是不是整段区域。
/// @param definition 参数定义，只在本次调用期间借用。
/// @return 没有标量类型、shape 为 region，或 editor 为 region_editor 时为 true。
bool isBlob(const ParameterDefinition& definition) {
    if (!definition.dataType) {
        return true;
    }
    if (definition.shape && *definition.shape == ValueShape::Region) {
        return true;
    }
    return definition.editor && *definition.editor == EditorKind::RegionEditor;
}

/// @brief 判断这条路径是否不能作为 param.md 的四字节布局地址。
/// @param layout 固件布局，只在本次调用期间借用。
/// @param firmwarePath 目录中的固件路径，只在本次调用期间借用。
/// @return 路径不在布局里或几何越界时为 true。pack4-sequential 表示路径存在，从地址 0 按四字节对齐排列，这里不阻止。
bool pathMissingOrOutOfRange(const FirmwareLayout& layout, std::string_view firmwarePath) {
    const LayoutResolve resolved = layout.resolve(firmwarePath);
    if (resolved.ok()) {
        return false;
    }
    return *resolved.tryError() != LayoutError::Unverified;
}

/// @brief 为一条脏参数决定排除原因。
/// @param definition 参数定义。空表示目录里没有这个 id。
/// @param layout 固件布局，只在本次调用期间借用。
/// @return 可以进入常规计划时为空。否则是排除原因。
std::optional<WriteExclusion> exclusionFor(
    const ParameterDefinition* definition,
    const FirmwareLayout& layout) {
    if (definition == nullptr || !definition->firmwarePath || definition->firmwarePath->empty()) {
        return WriteExclusion::Unverified;
    }
    const ActuationPolicy policy = classifyActuation(*definition);
    if (policy.kind == ActuationKind::OneShot) {
        return WriteExclusion::Command;
    }
    if (policy.kind == ActuationKind::NotActuated) {
        if (definition->access && *definition->access == AccessMode::ReadOnly) {
            return WriteExclusion::ReadOnly;
        }
        return WriteExclusion::Unsupported;
    }
    if (policy.kind == ActuationKind::ModeChange || isBlob(*definition)) {
        return policy.kind == ActuationKind::ModeChange ? WriteExclusion::Unsupported : WriteExclusion::Blob;
    }
    if (pathMissingOrOutOfRange(layout, *definition->firmwarePath)) {
        return WriteExclusion::Unverified;
    }
    return std::nullopt;
}

/// @brief 把一条脏参数放进预览。
/// @param plan 正在组装的计划。
/// @param id 参数标识，按值复制。
/// @param state 运行状态，只在本次调用期间借用。调用前已经确认 dirty 和 stagedValue。
/// @param definition 目录定义。空表示目录中没有该 id。
/// @param layout 固件布局，只在本次调用期间借用。
void appendCandidate(
    WritePlan& plan,
    const ParameterId& id,
    const ParameterRuntimeState& state,
    const ParameterDefinition* definition,
    const FirmwareLayout& layout) {
    if (const std::optional<WriteExclusion> reason = exclusionFor(definition, layout)) {
        plan.excluded.push_back(RejectedWrite{id, *reason, state.stale});
        return;
    }
    plan.included.push_back(PlannedWrite{
        id,
        *definition->firmwarePath,
        state.deviceValue,
        *state.stagedValue,
        definition->dataType,
        state.stale});
}

/// @brief 范围内的状态是否构成待写候选。
/// @param state 运行状态。空表示 Store 里没有记录。
/// @return dirty 且 stagedValue 有值时为 true。
bool isPending(const ParameterRuntimeState* state) {
    return state != nullptr && state->dirty && state->stagedValue.has_value();
}

}  // namespace

WritePlan buildWritePlan(
    const ParameterCatalog& catalog,
    const ParameterRuntimeStore& store,
    const FirmwareLayout& layout,
    const WriteScope& scope) {
    WritePlan plan;
    if (scope.kind == WriteScopeKind::Category) {
        if (scope.category.empty()) {
            return plan;
        }
        for (const ParameterDefinition& definition : catalog.entries()) {
            if (!definition.category || *definition.category != scope.category) {
                continue;
            }
            const ParameterRuntimeState* state = store.find(definition.id);
            if (!isPending(state)) {
                continue;
            }
            appendCandidate(plan, definition.id, *state, &definition, layout);
        }
        return plan;
    }

    std::vector<std::string> seen;
    for (const ParameterId& id : scope.selected) {
        bool duplicated = false;
        for (const std::string& previous : seen) {
            if (previous == id.text()) {
                duplicated = true;
                break;
            }
        }
        if (duplicated) {
            continue;
        }
        seen.push_back(id.text());
        const ParameterRuntimeState* state = store.find(id);
        if (!isPending(state)) {
            continue;
        }
        appendCandidate(plan, id, *state, catalog.find(id.text()), layout);
    }
    return plan;
}

}  // namespace ernc::domain
