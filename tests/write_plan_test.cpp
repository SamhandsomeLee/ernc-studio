#include "ernc/domain/write_plan.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using ernc::domain::CatalogLoad;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::domain::WriteExclusion;
using ernc::domain::WritePlan;
using ernc::domain::WriteScope;
using ernc::domain::WriteScopeKind;

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

/// @brief 做成一个固定时刻。
/// @param seconds 距系统钟纪元的秒数。
/// @return 调用方可复制的时间点。
RuntimeTime at(int seconds) {
    return RuntimeTime{std::chrono::seconds{seconds}};
}

/// @brief 包一份最小目录。
/// @param parameters parameters 数组内部的 JSON，不含外层方括号。只在本次调用期间借用。
/// @return 调用方拥有的目录 JSON。
std::string catalogText(std::string_view parameters) {
    std::string text = R"({"schema_version":1,"catalog_version":"0.2.0","parameters":[)";
    text.append(parameters);
    text.append("]}");
    return text;
}

/// @brief 包一份两个已核验字段的布局。路径是 ANC_OFF 和 Step。
/// @param status 两个字段共用的 status 原文。verified 可以通过 resolve，其他文本不行。
/// @return 调用方拥有的 manifest JSON。每个字段 4 字节。
std::string layoutText(std::string_view status) {
    std::string text = R"({"alignment":4,"base":0,"total_size":8,"total_slots":2,"fields":[)";
    text += R"({"path":"ANC_OFF","offset":0,"size":4,"slot":0,"slot_count":1,"type":"float","shape":[],"status":")";
    text.append(status);
    text += R"(","writable":false},{"path":"Step","offset":4,"size":4,"slot":1,"slot_count":1,"type":"float","shape":[],"status":")";
    text.append(status);
    text += R"(","writable":false}]})";
    return text;
}

/// @brief 本用例使用的目录文本。
/// @return 含普通开关、只读 Step、命令、区域和另一个分类参数的目录。
std::string sampleCatalog() {
    return catalogText(
        R"({"id":"ANC_OFF","firmware_path":"ANC_OFF","category":"ERNC_Flag","access":"read_write","data_type":"float32"},{"id":"Step","firmware_path":"Step","category":"RNC_Param","access":"read_only","data_type":"float32","editor":"read_only"},{"id":"RNC_refresh","firmware_path":"RNC_refresh","category":"ERNC_Flag","access":"command","data_type":"int32","editor":"momentary_command"},{"id":"check_data","firmware_path":"check_data","category":"other","access":"read_write"},{"id":"RNC_param.alpha1","firmware_path":"RNC_param.alpha1","category":"RNC_Param","access":"read_write","data_type":"float32"})");
}

/// @brief 载入样本目录和已核验布局。
/// @param catalog 成功时写入目录副本。
/// @param layout 成功时写入布局副本。
/// @param status 布局字段的 status 原文。
/// @return 两份文档都合法时为 true。
bool loadSample(ParameterCatalog& catalog, FirmwareLayout& layout, std::string_view status) {
    const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadText(sampleCatalog());
    const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadText(layoutText(status));
    if (!loadedCatalog.ok() || !loadedLayout.ok()) {
        return false;
    }
    catalog = loadedCatalog.catalog();
    layout = loadedLayout.layout();
    return true;
}

/// @brief 已核验的脏开关进入可写预览，未改的选择被略过。
/// @return 只含 ANC_OFF，且设备值和目标值分开时为 true。
bool selectedVerifiedIsIncluded() {
    const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadText(sampleCatalog());
    const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadText(layoutText("verified"));
    if (!loadedCatalog.ok() || !loadedLayout.ok()) {
        return fail("selected_verified_is_included");
    }
    ParameterRuntimeStore store;
    store.observe(identifier("ANC_OFF"), ScalarValue::fromFloat(1.f), at(1));
    store.stage(identifier("ANC_OFF"), ScalarValue::fromFloat(0.f), at(2));
    store.observe(identifier("RNC_param.alpha1"), ScalarValue::fromFloat(3.f), at(3));
    WriteScope scope;
    scope.kind = WriteScopeKind::Selected;
    scope.selected.push_back(identifier("ANC_OFF"));
    scope.selected.push_back(identifier("RNC_param.alpha1"));
    const WritePlan plan =
        ernc::domain::buildWritePlan(loadedCatalog.catalog(), store, loadedLayout.layout(), scope);
    if (plan.included.size() != 1 || plan.excluded.size() != 0 || plan.included[0].id.text() != "ANC_OFF"
        || plan.included[0].firmwarePath != "ANC_OFF" || !plan.included[0].deviceValue
        || *plan.included[0].deviceValue != ScalarValue::fromFloat(1.f)
        || plan.included[0].stagedValue != ScalarValue::fromFloat(0.f) || !plan.included[0].dataType
        || *plan.included[0].dataType != ernc::domain::DataType::Float32 || plan.included[0].stale) {
        return fail("selected_verified_is_included");
    }
    return true;
}

/// @brief 只读、命令和整段区域即使布局已核验也不进常规计划。
/// @return 三条排除原因分别是 ReadOnly、Command 和 Blob 时为 true。
bool blockedKindsStayOut() {
    const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadText(sampleCatalog());
    const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadText(layoutText("verified"));
    if (!loadedCatalog.ok() || !loadedLayout.ok()) {
        return fail("blocked_kinds_stay_out");
    }
    ParameterRuntimeStore store;
    store.stage(identifier("Step"), ScalarValue::fromFloat(1.f), at(1));
    store.stage(identifier("RNC_refresh"), ScalarValue::fromInt32(1), at(2));
    store.stage(identifier("check_data"), ScalarValue::fromFloat(1.f), at(3));
    WriteScope scope;
    scope.kind = WriteScopeKind::Selected;
    scope.selected.push_back(identifier("Step"));
    scope.selected.push_back(identifier("RNC_refresh"));
    scope.selected.push_back(identifier("check_data"));
    const WritePlan plan =
        ernc::domain::buildWritePlan(loadedCatalog.catalog(), store, loadedLayout.layout(), scope);
    if (plan.included.size() != 0 || plan.excluded.size() != 3 || plan.excluded[0].reason != WriteExclusion::ReadOnly
        || plan.excluded[1].reason != WriteExclusion::Command || plan.excluded[2].reason != WriteExclusion::Blob) {
        return fail("blocked_kinds_stay_out");
    }
    return true;
}

/// @brief pack4 路径可以进入常规计划；布局里没有的路径仍然排除。
/// @return ANC_OFF 在可写列表，alpha1 的原因是 Unverified 时为 true。
bool pack4PathIsIncluded() {
    const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadText(sampleCatalog());
    const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadText(layoutText("pack4-sequential"));
    if (!loadedCatalog.ok() || !loadedLayout.ok()) {
        return fail("pack4_path_is_included");
    }
    ParameterRuntimeStore store;
    store.stage(identifier("ANC_OFF"), ScalarValue::fromFloat(0.f), at(1));
    store.stage(identifier("RNC_param.alpha1"), ScalarValue::fromFloat(0.25f), at(2));
    WriteScope scope;
    scope.kind = WriteScopeKind::Selected;
    scope.selected.push_back(identifier("ANC_OFF"));
    scope.selected.push_back(identifier("RNC_param.alpha1"));
    const WritePlan plan =
        ernc::domain::buildWritePlan(loadedCatalog.catalog(), store, loadedLayout.layout(), scope);
    if (plan.included.size() != 1 || plan.included[0].id.text() != "ANC_OFF" || plan.excluded.size() != 1
        || plan.excluded[0].id.text() != "RNC_param.alpha1"
        || plan.excluded[0].reason != WriteExclusion::Unverified) {
        return fail("pack4_path_is_included");
    }
    return true;
}

/// @brief 当前分类只收该分类里的脏参数。
/// @return RNC_Param 的计划不含 ERNC_Flag 的 ANC_OFF 时为 true。
bool categoryFilters() {
    const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadText(sampleCatalog());
    const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadText(layoutText("verified"));
    if (!loadedCatalog.ok() || !loadedLayout.ok()) {
        return fail("category_filters");
    }
    ParameterRuntimeStore store;
    store.stage(identifier("ANC_OFF"), ScalarValue::fromFloat(0.f), at(1));
    store.stage(identifier("Step"), ScalarValue::fromFloat(2.f), at(2));
    WriteScope scope;
    scope.kind = WriteScopeKind::Category;
    scope.category = "RNC_Param";
    const WritePlan plan =
        ernc::domain::buildWritePlan(loadedCatalog.catalog(), store, loadedLayout.layout(), scope);
    if (plan.included.size() != 0 || plan.excluded.size() != 1 || plan.excluded[0].id.text() != "Step"
        || plan.excluded[0].reason != WriteExclusion::ReadOnly) {
        return fail("category_filters");
    }
    return true;
}

/// @brief 过期读数仍可进入预览，但 stale 标记保持。
/// @return included 的 stale 为 true，且设备值还是离线前的值时为 true。
bool staleFlagKept() {
    const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadText(sampleCatalog());
    const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadText(layoutText("verified"));
    if (!loadedCatalog.ok() || !loadedLayout.ok()) {
        return fail("stale_flag_kept");
    }
    ParameterRuntimeStore store;
    store.observe(identifier("ANC_OFF"), ScalarValue::fromFloat(1.f), at(1));
    store.stage(identifier("ANC_OFF"), ScalarValue::fromFloat(0.f), at(2));
    store.markOffline();
    WriteScope scope;
    scope.kind = WriteScopeKind::Selected;
    scope.selected.push_back(identifier("ANC_OFF"));
    const WritePlan plan =
        ernc::domain::buildWritePlan(loadedCatalog.catalog(), store, loadedLayout.layout(), scope);
    if (plan.included.size() != 1 || !plan.included[0].stale || !plan.included[0].deviceValue
        || *plan.included[0].deviceValue != ScalarValue::fromFloat(1.f)) {
        return fail("stale_flag_kept");
    }
    return true;
}

/// @brief 生成计划不改 Store。
/// @return 调用前后设备值、目标值和 dirty 都相同，且没有新增记录时为 true。
bool storeUnchanged() {
    const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadText(sampleCatalog());
    const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadText(layoutText("verified"));
    if (!loadedCatalog.ok() || !loadedLayout.ok()) {
        return fail("store_unchanged");
    }
    ParameterRuntimeStore store;
    store.observe(identifier("ANC_OFF"), ScalarValue::fromFloat(1.f), at(1));
    store.stage(identifier("ANC_OFF"), ScalarValue::fromFloat(0.f), at(2));
    const std::size_t before = store.size();
    WriteScope scope;
    scope.kind = WriteScopeKind::Selected;
    scope.selected.push_back(identifier("ANC_OFF"));
    const WritePlan plan =
        ernc::domain::buildWritePlan(loadedCatalog.catalog(), store, loadedLayout.layout(), scope);
    const auto* state = store.find(identifier("ANC_OFF"));
    if (plan.included.size() != 1 || store.size() != before || state == nullptr || !state->dirty || !state->deviceValue
        || *state->deviceValue != ScalarValue::fromFloat(1.f) || !state->stagedValue
        || *state->stagedValue != ScalarValue::fromFloat(0.f)) {
        return fail("store_unchanged");
    }
    return true;
}

/// @brief 重复选择只出现一次。
/// @return included 只有一条 ANC_OFF 时为 true。
bool duplicateSelectionOnce() {
    const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadText(sampleCatalog());
    const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadText(layoutText("verified"));
    if (!loadedCatalog.ok() || !loadedLayout.ok()) {
        return fail("duplicate_selection_once");
    }
    ParameterRuntimeStore store;
    store.stage(identifier("ANC_OFF"), ScalarValue::fromFloat(0.f), at(1));
    WriteScope scope;
    scope.kind = WriteScopeKind::Selected;
    scope.selected.push_back(identifier("ANC_OFF"));
    scope.selected.push_back(identifier("ANC_OFF"));
    const WritePlan plan =
        ernc::domain::buildWritePlan(loadedCatalog.catalog(), store, loadedLayout.layout(), scope);
    if (plan.included.size() != 1 || plan.excluded.size() != 0) {
        return fail("duplicate_selection_once");
    }
    return true;
}

/// @brief 正式 manifest 上的脏标量按四字节布局进入常规计划。
/// @param catalogPath 正式目录路径，只在本次调用期间借用。
/// @param manifestPath 正式布局路径，只在本次调用期间借用。
/// @return alpha1 在可写列表且不在排除列表时为 true。
bool realManifestAlphaIncluded(const char* catalogPath, const char* manifestPath) {
    const CatalogLoad loadedCatalog = ParameterCatalogLoader::loadFile(catalogPath);
    const LayoutLoad loadedLayout = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loadedCatalog.ok() || !loadedLayout.ok()) {
        return fail("real_manifest_alpha_included");
    }
    ParameterRuntimeStore store;
    store.stage(identifier("RNC_param.alpha1"), ScalarValue::fromFloat(0.25f), at(1));
    WriteScope scope;
    scope.kind = WriteScopeKind::Category;
    scope.category = "RNC_Param";
    const WritePlan plan =
        ernc::domain::buildWritePlan(loadedCatalog.catalog(), store, loadedLayout.layout(), scope);
    if (plan.included.size() != 1 || plan.included[0].id.text() != "RNC_param.alpha1"
        || plan.included[0].firmwarePath != "RNC_param.alpha1"
        || plan.included[0].stagedValue != ScalarValue::fromFloat(0.25f)) {
        return fail("real_manifest_alpha_included");
    }
    for (const auto& item : plan.excluded) {
        if (item.id.text() == "RNC_param.alpha1") {
            return fail("real_manifest_alpha_included");
        }
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param catalogPath 正式参数目录路径。合成样本不读取它。
/// @param manifestPath 正式布局路径。合成样本不读取它。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const char* catalogPath, const char* manifestPath, bool& recognized) {
    recognized = true;
    if (name == "selected_verified_is_included") {
        return selectedVerifiedIsIncluded();
    }
    if (name == "blocked_kinds_stay_out") {
        return blockedKindsStayOut();
    }
    if (name == "pack4_path_is_included") {
        return pack4PathIsIncluded();
    }
    if (name == "category_filters") {
        return categoryFilters();
    }
    if (name == "stale_flag_kept") {
        return staleFlagKept();
    }
    if (name == "store_unchanged") {
        return storeUnchanged();
    }
    if (name == "duplicate_selection_once") {
        return duplicateSelectionOnce();
    }
    if (name == "real_manifest_alpha_included") {
        return realManifestAlphaIncluded(catalogPath, manifestPath);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个写入计划单元测试。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是 parameter_catalog.json，argv[3] 是 layout_manifest.json。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_write_plan_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], argv[2], argv[3], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
