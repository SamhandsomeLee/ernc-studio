#include "ernc/domain/catalog.hpp"
#include "inspector/inspector_context.hpp"
#include <chrono>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using ernc::domain::AccessMode;
using ernc::domain::DataType;
using ernc::domain::ParameterDefinition;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::domain::ValueShape;
using ernc::domain::WireLevels;
using ernc::presentation::describeInspector;
using ernc::presentation::InspectorText;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 做一条只含 id 的目录定义，其余字段由调用方填上。
/// @param id 非空目录主键，只借用。空文本会让 `ParameterId::parse` 失败并终止进程。
/// @return 调用方拥有的定义。它还没有说明、路径或权限。
ParameterDefinition definitionFor(std::string_view id) {
    return ParameterDefinition{ParameterId::parse(id).value()};
}

/// @brief 判断三段文字是否与期望完全一致。
/// @param name 失败时打印的用例名，只借用。
/// @param actual 本次格式化结果。调用方拥有。
/// @param properties Properties 的期望全文，只借用。
/// @param info Info 的期望全文，只借用。
/// @param diagnostics Diagnostics 的期望全文，只借用。
/// @return 三段都一致时为 true。
bool sameText(
    const char* name,
    const InspectorText& actual,
    std::string_view properties,
    std::string_view info,
    std::string_view diagnostics) {
    if (actual.properties != properties || actual.info != info || actual.diagnostics != diagnostics) {
        std::cerr << actual.properties << "\n---\n" << actual.info << "\n---\n" << actual.diagnostics << '\n';
        return fail(name);
    }
    return true;
}

/// @brief 还没有参数 id 时，三个标签都不显示假读数。
/// @return 全文都是未选择和占位时为 true。
bool emptySelection() {
    const InspectorText text = describeInspector({}, nullptr, nullptr);
    return sameText(
        "empty_selection",
        text,
        "参数 未选择参数\n"
        "读 —\n"
        "写 —\n"
        "状态 —\n"
        "说明 —\n"
        "路径 —\n"
        "类型 —\n"
        "形状 —\n"
        "权限 —",
        "来源 —\n"
        "分类 —\n"
        "说明 —\n"
        "更新时间 —",
        "当前参数 未选择参数\n"
        "在操作中心查看会话记录。");
}

/// @brief 未观察 ANC_OFF 时显示目录说明和空读数。
/// @return 说明、状态和目录线值都匹配时为 true。
bool ancOff() {
    ParameterDefinition definition = definitionFor("ANC_OFF");
    definition.firmwarePath = "ANC_OFF";
    definition.category = "ERNC_Flag";
    definition.descriptionZh = "ERNC总开关，0开启，1关闭";
    definition.dataType = DataType::Float32;
    definition.shape = ValueShape::Scalar;
    definition.access = AccessMode::ReadWrite;
    definition.wireLevels = WireLevels{0, 1};
    const InspectorText text = describeInspector("ANC_OFF", &definition, nullptr);
    return sameText(
        "anc_off",
        text,
        "参数 ANC_OFF\n"
        "读 —\n"
        "写 —\n"
        "状态 未读取\n"
        "说明 ERNC总开关，0开启，1关闭\n"
        "路径 ANC_OFF\n"
        "类型 32 位浮点\n"
        "形状 标量\n"
        "权限 读写",
        "来源 参数目录\n"
        "分类 ERNC_Flag\n"
        "说明 ERNC总开关，0开启，1关闭\n"
        "更新时间 —\n"
        "目录线值 on 0，off 1。这不是设备当前值。",
        "当前参数 ANC_OFF\n"
        "在操作中心查看会话记录。");
}

/// @brief 在 Store 分别放入读数与用户目标，检查两者都出现在属性中。
/// @return 两列保持 1 和 2 且更新时间来自设备读回时为 true。
bool alpha1Values() {
    ParameterDefinition definition = definitionFor("RNC_param.alpha1");
    definition.firmwarePath = "RNC_param.alpha1";
    definition.category = "RNC_Param";
    definition.descriptionZh = "第一段步长(10分钟后)，步长越大系统收敛速度越快，但稳定性越差";
    definition.dataType = DataType::Float32;
    definition.access = AccessMode::ReadWrite;
    ParameterRuntimeStore store;
    store.observe(definition.id, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{10}});
    store.stage(definition.id, ScalarValue::fromFloat(2.0F), RuntimeTime{std::chrono::seconds{11}});
    const InspectorText text = describeInspector("RNC_param.alpha1", &definition, store.find(definition.id));
    return sameText(
        "alpha1_values",
        text,
        "参数 RNC_param.alpha1\n"
        "读 1\n"
        "写 2\n"
        "状态 待写入\n"
        "说明 第一段步长(10分钟后)，步长越大系统收敛速度越快，但稳定性越差\n"
        "路径 RNC_param.alpha1\n"
        "类型 32 位浮点\n"
        "形状 —\n"
        "权限 读写",
        "来源 设备读回\n"
        "分类 RNC_Param\n"
        "说明 第一段步长(10分钟后)，步长越大系统收敛速度越快，但稳定性越差\n"
        "更新时间 1970-01-01T00:00:10Z",
        "当前参数 RNC_param.alpha1\n"
        "在操作中心查看会话记录。");
}

/// @brief Step 的目录权限是只读，Set 保持占位。
/// @return 权限、Read 和 Set 都匹配时为 true。
bool stepReadOnly() {
    ParameterDefinition definition = definitionFor("Step");
    definition.firmwarePath = "Step";
    definition.category = "RNC_Param";
    definition.descriptionZh = "实时步长显示";
    definition.dataType = DataType::Float32;
    definition.shape = ValueShape::Scalar;
    definition.access = AccessMode::ReadOnly;
    ParameterRuntimeStore store;
    store.observe(definition.id, ScalarValue::fromFloat(3.0F), RuntimeTime{std::chrono::seconds{9}});
    store.markOffline();
    const InspectorText text = describeInspector("Step", &definition, store.find(definition.id));
    return sameText(
        "step_read_only",
        text,
        "参数 Step\n"
        "读 3\n"
        "写 —\n"
        "状态 数据已过期\n"
        "说明 实时步长显示\n"
        "路径 Step\n"
        "类型 32 位浮点\n"
        "形状 标量\n"
        "权限 只读",
        "来源 设备缓存\n"
        "分类 RNC_Param\n"
        "说明 实时步长显示\n"
        "更新时间 1970-01-01T00:00:09Z",
        "当前参数 Step\n"
        "在操作中心查看会话记录。");
}

/// @brief check_data 只显示目录字段，不编造假读数。
/// @return 路径和权限来自目录、Read/Set 为占位时为 true。
bool checkDataCatalogOnly() {
    ParameterDefinition definition = definitionFor("check_data");
    definition.firmwarePath = "check_data";
    definition.category = "other";
    definition.access = AccessMode::ReadWrite;
    const InspectorText text = describeInspector("check_data", &definition, nullptr);
    return sameText(
        "check_data_catalog_only",
        text,
        "参数 check_data\n"
        "读 —\n"
        "写 —\n"
        "状态 未读取\n"
        "说明 —\n"
        "路径 check_data\n"
        "类型 —\n"
        "形状 —\n"
        "权限 读写",
        "来源 参数目录\n"
        "分类 other\n"
        "说明 —\n"
        "更新时间 —",
        "当前参数 check_data\n"
        "在操作中心查看会话记录。");
}

/// @brief 设计示意里的小数不能出现在检查器文字里。
/// @return 五段快照都不含 `0.05`、`0.06`、`0.002` 时为 true。
bool noSketchValues() {
    ParameterDefinition anc = definitionFor("ANC_OFF");
    anc.descriptionZh = "ERNC总开关，0开启，1关闭";
    anc.wireLevels = WireLevels{0, 1};
    ParameterDefinition alpha = definitionFor("RNC_param.alpha1");
    ParameterDefinition step = definitionFor("Step");
    step.access = AccessMode::ReadOnly;
    ParameterDefinition check = definitionFor("check_data");
    const InspectorText samples[] = {
        describeInspector({}, nullptr, nullptr),
        describeInspector("ANC_OFF", &anc, nullptr),
        describeInspector("RNC_param.alpha1", &alpha, nullptr),
        describeInspector("Step", &step, nullptr),
        describeInspector("check_data", &check, nullptr),
    };
    std::string joined;
    for (const InspectorText& sample : samples) {
        joined.append(sample.properties);
        joined.append(sample.info);
        joined.append(sample.diagnostics);
    }
    if (joined.find("0.05") != std::string::npos || joined.find("0.06") != std::string::npos
        || joined.find("0.002") != std::string::npos) {
        return fail("no_sketch_values");
    }
    return true;
}

} // namespace

/// @brief 按用例名运行检查器文字检查。
/// @param argc 参数个数，含程序名。
/// @param argv 程序名和用例名。用例名由 CTest 传入，本函数只借用。
/// @return 通过时为 0，未知用例或失败时为 1。
int main(int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    const std::string_view name = argv[1];
    if (name == "empty_selection") {
        return emptySelection() ? 0 : 1;
    }
    if (name == "anc_off") {
        return ancOff() ? 0 : 1;
    }
    if (name == "alpha1_values") {
        return alpha1Values() ? 0 : 1;
    }
    if (name == "step_read_only") {
        return stepReadOnly() ? 0 : 1;
    }
    if (name == "check_data_catalog_only") {
        return checkDataCatalogOnly() ? 0 : 1;
    }
    if (name == "no_sketch_values") {
        return noSketchValues() ? 0 : 1;
    }
    return 1;
}
