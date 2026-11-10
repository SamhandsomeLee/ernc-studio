#include "ernc/domain/command_policy.hpp"

#include <iostream>
#include <string_view>

namespace {

using ernc::domain::AccessMode;
using ernc::domain::ActuationKind;
using ernc::domain::ActuationPolicy;
using ernc::domain::CatalogLoad;
using ernc::domain::EditorKind;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterDefinition;
using ernc::domain::ParameterId;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 做成一个非空参数标识。
/// @param text 标识文本，只在本次调用期间借用。
/// @return parse 成功后的标识副本。
ParameterId identifier(const char* text) {
    return ParameterId::parse(text).value();
}

/// @brief 做成一条只含权限和控件的定义。
/// @param access 访问声明。空表示权限未声明。
/// @param editor 控件类型。空表示目录没有声明控件。
/// @return 调用方拥有的定义。没有路径、说明和线值。
ParameterDefinition definition(const std::optional<AccessMode>& access, const std::optional<EditorKind>& editor) {
    return ParameterDefinition{
        identifier("synthetic"),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt,
        access,
        editor,
        std::nullopt};
}

/// @brief 命令策略当前没有启用。
/// @return commandPolicyEnforced 为 false 时为 true。
bool policyIsReserved() {
    if (ernc::domain::commandPolicyEnforced()) {
        return fail("policy_is_reserved");
    }
    return true;
}

/// @brief 正式目录里的三类参数得到预留分类，但当前不拦截。
/// @param catalogPath 参数目录路径，只在本次调用期间借用。
/// @return RNC_refresh 是一次性触发，ANC_OFF 是普通写入，Step 不可作动，且都还没被排除时为 true。
bool catalogClassesStayReserved(const char* catalogPath) {
    const CatalogLoad loaded = ParameterCatalogLoader::loadFile(catalogPath);
    if (!loaded.ok()) {
        return fail("catalog_classes_stay_reserved");
    }
    const ParameterDefinition* refresh = loaded.catalog().find("RNC_refresh");
    const ParameterDefinition* anc = loaded.catalog().find("ANC_OFF");
    const ParameterDefinition* step = loaded.catalog().find("Step");
    if (refresh == nullptr || anc == nullptr || step == nullptr) {
        return fail("catalog_classes_stay_reserved");
    }
    const ActuationPolicy oneShot = ernc::domain::classifyActuation(*refresh);
    const ActuationPolicy normal = ernc::domain::classifyActuation(*anc);
    const ActuationPolicy blocked = ernc::domain::classifyActuation(*step);
    if (oneShot.kind != ActuationKind::OneShot || oneShot.includeInBulkWritePlan || oneShot.allowGenericRetry
        || oneShot.realWritePermitted) {
        return fail("catalog_classes_stay_reserved");
    }
    if (normal.kind != ActuationKind::NormalWrite || !normal.includeInBulkWritePlan || !normal.allowGenericRetry) {
        return fail("catalog_classes_stay_reserved");
    }
    if (blocked.kind != ActuationKind::NotActuated || blocked.includeInBulkWritePlan || blocked.realWritePermitted) {
        return fail("catalog_classes_stay_reserved");
    }
    if (ernc::domain::excludedFromBulkWrite(oneShot) || ernc::domain::genericRetryPermitted(normal)
        || ernc::domain::genericRetryPermitted(oneShot)) {
        return fail("catalog_classes_stay_reserved");
    }
    return true;
}

/// @brief 模式选择器的预留规则不进普通批量计划。
/// @return 类别是 ModeChange，且当前没有被排除函数拦截时为 true。
bool modeChangeIsReserved() {
    const ActuationPolicy policy = ernc::domain::classifyActuation(definition(AccessMode::ReadWrite, EditorKind::ModeSelector));
    if (policy.kind != ActuationKind::ModeChange || policy.includeInBulkWritePlan || policy.allowGenericRetry
        || !policy.realWritePermitted) {
        return fail("mode_change_is_reserved");
    }
    if (ernc::domain::excludedFromBulkWrite(policy)) {
        return fail("mode_change_is_reserved");
    }
    return true;
}

/// @brief 瞬时控件即使权限写成 read_write，预留类别仍是一次性触发。
/// @return 类别是 OneShot 且真实写入许可为 false 时为 true。
bool momentaryStaysOneShot() {
    const ActuationPolicy policy =
        ernc::domain::classifyActuation(definition(AccessMode::ReadWrite, EditorKind::MomentaryCommand));
    if (policy.kind != ActuationKind::OneShot || policy.includeInBulkWritePlan || policy.allowGenericRetry
        || policy.realWritePermitted) {
        return fail("momentary_stays_one_shot");
    }
    return true;
}

/// @brief 权限未声明时不可作动。
/// @return 类别是 NotActuated 时为 true。
bool missingAccessIsNotActuated() {
    const ActuationPolicy policy = ernc::domain::classifyActuation(definition(std::nullopt, std::nullopt));
    if (policy.kind != ActuationKind::NotActuated || policy.includeInBulkWritePlan || policy.realWritePermitted) {
        return fail("missing_access_is_not_actuated");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param catalogPath 正式参数目录路径。合成定义的用例不读取它。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const char* catalogPath, bool& recognized) {
    recognized = true;
    if (name == "policy_is_reserved") {
        return policyIsReserved();
    }
    if (name == "catalog_classes_stay_reserved") {
        return catalogClassesStayReserved(catalogPath);
    }
    if (name == "mode_change_is_reserved") {
        return modeChangeIsReserved();
    }
    if (name == "momentary_stays_one_shot") {
        return momentaryStaysOneShot();
    }
    if (name == "missing_access_is_not_actuated") {
        return missingAccessIsNotActuated();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个命令策略单元测试。
/// @param argc 参数个数，包含程序名。必须等于 3。
/// @param argv argv[1] 是用例名，argv[2] 是 parameter_catalog.json。两段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_command_policy_tests <case> <parameter_catalog.json>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], argv[2], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
