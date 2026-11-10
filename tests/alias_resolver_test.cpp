#include "ernc/domain/alias_resolver.hpp"

#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

using ernc::domain::AliasError;
using ernc::domain::AliasLoad;
using ernc::domain::AliasRecord;
using ernc::domain::AliasResolution;
using ernc::domain::DataType;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutError;
using ernc::domain::LayoutLoad;
using ernc::domain::LayoutResolve;
using ernc::domain::ParameterAliasLoader;
using ernc::domain::ParameterAliasTable;
using ernc::domain::kAdvancedParameterCategory;

/// 两个 pack4 字段。路径分别是 RNC_param.refernum 和 Other.path，各占 4 字节。
constexpr std::string_view kPack4Manifest =
    R"({"alignment":4,"base":0,"total_size":8,"total_slots":2,"fields":[{"path":"RNC_param.refernum","offset":0,"size":4,"slot":0,"slot_count":1,"type":"int32_t","shape":[],"status":"pack4-sequential","writable":false},{"path":"Other.path","offset":4,"size":4,"slot":1,"slot_count":1,"type":"float","shape":[],"status":"pack4-sequential","writable":false}]})";

/// 与 kPack4Manifest 相同的几何，但 status 为 verified，resolve 可以返回 slot 和类型。
constexpr std::string_view kVerifiedManifest =
    R"({"alignment":4,"base":0,"total_size":8,"total_slots":2,"fields":[{"path":"RNC_param.refernum","offset":0,"size":4,"slot":0,"slot_count":1,"type":"int32_t","shape":[],"status":"verified","writable":false},{"path":"Other.path","offset":4,"size":4,"slot":1,"slot_count":1,"type":"float","shape":[],"status":"verified","writable":false}]})";

/// 字段路径存在，但已核验几何的字节长度不是 4。resolve 返回 OutOfRange。
constexpr std::string_view kOutOfRangeManifest =
    R"({"alignment":4,"base":0,"total_size":16,"total_slots":4,"fields":[{"path":"RNC_param.refernum","offset":0,"size":3,"slot":0,"slot_count":1,"type":"int32_t","shape":[],"status":"verified","writable":false}]})";

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 载入一份最小布局。
/// @param jsonText manifest 文本，只在本次调用期间借用。
/// @return 文档合法时为布局副本。失败时为空，不把空字段索引当成成功。
std::optional<FirmwareLayout> loadLayout(std::string_view jsonText) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadText(jsonText);
    if (!loaded.ok()) {
        return std::nullopt;
    }
    return loaded.layout();
}

/// @brief 包装一份别名文档。
/// @param aliases aliases 数组内部的 JSON，不含外层方括号。只在本次调用期间借用。
/// @return 调用方拥有的完整 JSON。schema 版本固定为 1。
std::string aliasDocument(std::string_view aliases) {
    std::string text = R"({"schema_version":1,"aliases":[)";
    text.append(aliases);
    text.append("]}");
    return text;
}

/// @brief 确认加载失败且没有别名表。
/// @param jsonText 别名 JSON，只在本次调用期间借用。
/// @param layout 核对路径用的布局。
/// @param expected 期望的 AliasError。
/// @param name 用例名。
/// @return 错误类别匹配且 tryTable() 为空时为 true。
bool expectError(
    std::string_view jsonText,
    const FirmwareLayout& layout,
    AliasError expected,
    const char* name) {
    const AliasLoad loaded = ParameterAliasLoader::loadText(jsonText, layout);
    if (loaded.tryTable() != nullptr || loaded.tryError() == nullptr || *loaded.tryError() != expected) {
        return fail(name);
    }
    return true;
}

/// @brief Unique 绑定到 pack4 路径后，布局解析仍是 Unverified。
/// @return 别名路径为 RNC_param.refernum 且 resolve 为 Unverified 时为 true。
bool uniquePack4StaysUnverified() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("unique_pack4_stays_unverified");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadText(
        aliasDocument(R"({"name":"Refer_Num","category":"ERNC_IN_OUT","status":"unique","firmware_path":"RNC_param.refernum"})"),
        *layout);
    if (!loaded.ok()) {
        return fail("unique_pack4_stays_unverified");
    }
    const AliasRecord* record = loaded.table().find("Refer_Num");
    if (record == nullptr || record->resolution != AliasResolution::Unique || !record->firmwarePath
        || *record->firmwarePath != "RNC_param.refernum" || record->shared || record->reason) {
        return fail("unique_pack4_stays_unverified");
    }
    const LayoutResolve resolved = layout->resolve(*record->firmwarePath);
    if (resolved.tryField() != nullptr || resolved.tryError() == nullptr
        || *resolved.tryError() != LayoutError::Unverified) {
        return fail("unique_pack4_stays_unverified");
    }
    return true;
}

/// @brief 已核验字段被绑定时，读取范围仍是该字段自己的 slot、长度和类型。
/// @return slot 为 0、长度为 4 字节、类型为 int32 时为 true。
bool uniqueVerifiedKeepsGeometry() {
    const std::optional<FirmwareLayout> layout = loadLayout(kVerifiedManifest);
    if (!layout) {
        return fail("unique_verified_keeps_geometry");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadText(
        aliasDocument(R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum"})"),
        *layout);
    if (!loaded.ok() || loaded.table().find("Refer_Num") == nullptr
        || !loaded.table().find("Refer_Num")->firmwarePath) {
        return fail("unique_verified_keeps_geometry");
    }
    const LayoutResolve resolved = layout->resolve(*loaded.table().find("Refer_Num")->firmwarePath);
    if (!resolved.ok() || resolved.field().slot != 0 || resolved.field().slotCount != 1
        || resolved.field().size != 4 || !resolved.field().scalarType
        || *resolved.field().scalarType != DataType::Int32) {
        return fail("unique_verified_keeps_geometry");
    }
    return true;
}

/// @brief 路径在字段树里、几何越界时，别名仍然绑定这条路径，不把它当成未知路径。
/// @return 加载成功且 resolve 仍为 OutOfRange 时为 true。
bool outOfRangePathIsNotUnknown() {
    const std::optional<FirmwareLayout> layout = loadLayout(kOutOfRangeManifest);
    if (!layout) {
        return fail("out_of_range_path_is_not_unknown");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadText(
        aliasDocument(R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum"})"),
        *layout);
    if (!loaded.ok() || loaded.table().find("Refer_Num") == nullptr
        || !loaded.table().find("Refer_Num")->firmwarePath
        || *loaded.table().find("Refer_Num")->firmwarePath != "RNC_param.refernum") {
        return fail("out_of_range_path_is_not_unknown");
    }
    const LayoutResolve resolved = layout->resolve("RNC_param.refernum");
    if (resolved.tryField() != nullptr || resolved.tryError() == nullptr
        || *resolved.tryError() != LayoutError::OutOfRange) {
        return fail("out_of_range_path_is_not_unknown");
    }
    return true;
}

/// @brief Excel 短名不能当作固件路径。
/// @return firmware_path 为 Refer_Num 时结果为 UnknownPath，且没有别名表。
bool unknownPath() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("unknown_path");
    }
    return expectError(
        aliasDocument(R"({"name":"Refer_Num","status":"unique","firmware_path":"Refer_Num"})"),
        *layout,
        AliasError::UnknownPath,
        "unknown_path");
}

/// @brief 名称比较区分大小写。小写名称不是另一条记录。
/// @return find("refer_num") 为空，而 Refer_Num 仍指向原路径时为 true。
bool caseSensitiveName() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("case_sensitive_name");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadText(
        aliasDocument(R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum"})"),
        *layout);
    if (!loaded.ok() || loaded.table().find("Refer_Num") == nullptr
        || loaded.table().find("refer_num") != nullptr || loaded.table().find("REFER_NUM") != nullptr) {
        return fail("case_sensitive_name");
    }
    return true;
}

/// @brief 路径比较区分大小写。只改大小写的路径不算同一个字段。
/// @return firmware_path 为 RNC_param.Refernum 时结果为 UnknownPath。
bool caseSensitivePath() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("case_sensitive_path");
    }
    return expectError(
        aliasDocument(R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.Refernum"})"),
        *layout,
        AliasError::UnknownPath,
        "case_sensitive_path");
}

/// @brief Unresolved 记录可以被找到，但没有固件路径，也不能借此生成读取范围。
/// @return 记录的 resolution 为 Unresolved、firmwarePath 为空，且布局里没有 CAN_Msg 时为 true。
bool unresolvedHasNoPath() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("unresolved_has_no_path");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadText(
        aliasDocument(R"({"name":"CAN_Msg","category":"Check","status":"unresolved","reason":"没有槽位"})"),
        *layout);
    if (!loaded.ok()) {
        return fail("unresolved_has_no_path");
    }
    const AliasRecord* record = loaded.table().find("CAN_Msg");
    if (record == nullptr || record->resolution != AliasResolution::Unresolved || record->firmwarePath
        || record->shared || !record->reason || *record->reason != "没有槽位"
        || !record->category || *record->category != "Check") {
        return fail("unresolved_has_no_path");
    }
    const LayoutResolve resolved = layout->resolve("CAN_Msg");
    if (resolved.tryError() == nullptr || *resolved.tryError() != LayoutError::NotFound) {
        return fail("unresolved_has_no_path");
    }
    return true;
}

/// @brief 表里没有的名称只是未找到，不是一条 Unresolved 记录。
/// @return find("OrderFive_Flag") 为空指针时为 true。
bool absentNameIsNotUnresolved() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("absent_name_is_not_unresolved");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadText(
        aliasDocument(R"({"name":"CAN_Msg","status":"unresolved","reason":"没有槽位"})"),
        *layout);
    if (!loaded.ok() || loaded.table().find("CAN_Msg") == nullptr
        || loaded.table().find("OrderFive_Flag") != nullptr || loaded.table().find("") != nullptr) {
        return fail("absent_name_is_not_unresolved");
    }
    return true;
}

/// @brief 同一名称出现两次时整份表失败，不保留先出现的记录。
/// @return 结果为 DuplicateName 且 tryTable() 为空时为 true。
bool duplicateNameDropsTable() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("duplicate_name_drops_table");
    }
    return expectError(
        aliasDocument(
            R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum"},{"name":"Refer_Num","status":"unique","firmware_path":"Other.path"})"),
        *layout,
        AliasError::DuplicateName,
        "duplicate_name_drops_table");
}

/// @brief 两个名称指向同一路径且没有共享标志时，后一条不能盖掉前一条。
/// @return 结果为 PathConflict 且没有别名表时为 true。
bool pathConflictDropsTable() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("path_conflict_drops_table");
    }
    return expectError(
        aliasDocument(
            R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum"},{"name":"ReferNum","status":"unique","firmware_path":"RNC_param.refernum"})"),
        *layout,
        AliasError::PathConflict,
        "path_conflict_drops_table");
}

/// @brief 双方都写 shared 时，两个名称可以指向同一字段。
/// @return 两条记录的路径都是 RNC_param.refernum 且 shared 为 true 时为 true。
bool explicitShare() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("explicit_share");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadText(
        aliasDocument(
            R"({"name":"SpecRadius","status":"unique","firmware_path":"RNC_param.refernum","shared":true},{"name":"ERNC_TF.SpecRadius","status":"unique","firmware_path":"RNC_param.refernum","shared":true})"),
        *layout);
    if (!loaded.ok() || loaded.table().size() != 2) {
        return fail("explicit_share");
    }
    const AliasRecord* first = loaded.table().find("SpecRadius");
    const AliasRecord* second = loaded.table().find("ERNC_TF.SpecRadius");
    if (first == nullptr || second == nullptr || !first->shared || !second->shared || !first->firmwarePath
        || !second->firmwarePath || *first->firmwarePath != *second->firmwarePath
        || *first->firmwarePath != "RNC_param.refernum") {
        return fail("explicit_share");
    }
    return true;
}

/// @brief 只有一条记录时不能自称共享。
/// @return 结果为 InvalidDocument 时为 true。
bool loneShared() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("lone_shared");
    }
    return expectError(
        aliasDocument(R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum","shared":true})"),
        *layout,
        AliasError::InvalidDocument,
        "lone_shared");
}

/// @brief 同一路径上只要有一条没写 shared，整份表就失败。
/// @return 三条里缺一条 shared 时结果为 PathConflict。
bool partialShareOfThree() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("partial_share_of_three");
    }
    return expectError(
        aliasDocument(
            R"({"name":"A","status":"unique","firmware_path":"RNC_param.refernum","shared":true},{"name":"B","status":"unique","firmware_path":"RNC_param.refernum","shared":true},{"name":"C","status":"unique","firmware_path":"RNC_param.refernum"})"),
        *layout,
        AliasError::PathConflict,
        "partial_share_of_three");
}

/// @brief 三条记录都显式共享同一路径时可以加载，分类查询保持文件顺序。
/// @return findByCategory 先返回 First 再返回 Third 时为 true。
bool sharedCategoryKeepsOrder() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("shared_category_keeps_order");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadText(
        aliasDocument(
            R"({"name":"First","category":"高级参数","status":"unique","firmware_path":"RNC_param.refernum","shared":true},{"name":"Middle","category":"Check","status":"unique","firmware_path":"Other.path"},{"name":"Third","category":"高级参数","status":"unique","firmware_path":"RNC_param.refernum","shared":true})"),
        *layout);
    if (!loaded.ok()) {
        return fail("shared_category_keeps_order");
    }
    const auto advanced = loaded.table().findByCategory(kAdvancedParameterCategory);
    if (advanced.size() != 2 || advanced[0]->name != "First" || advanced[1]->name != "Third"
        || advanced[0] != &loaded.table().entries()[0] || advanced[1] != &loaded.table().entries()[2]) {
        return fail("shared_category_keeps_order");
    }
    if (!loaded.table().findByCategory("高级").empty() || loaded.table().findByCategory("").size() != 0) {
        return fail("shared_category_keeps_order");
    }
    return true;
}

/// @brief 空别名数组是一份合法的空表。
/// @return size 为 0 且查找不到 Refer_Num 时为 true。
bool emptyAliases() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("empty_aliases");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadText(R"({"schema_version":1,"aliases":[]})", *layout);
    if (!loaded.ok() || loaded.table().size() != 0 || loaded.table().schemaVersion() != 1
        || loaded.table().find("Refer_Num") != nullptr) {
        return fail("empty_aliases");
    }
    return true;
}

/// @brief Unique 记录不能附带原因，避免把原因读成另一条路径。
/// @return 结果为 InvalidDocument 时为 true。
bool uniqueRejectsReason() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("unique_rejects_reason");
    }
    return expectError(
        aliasDocument(
            R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum","reason":"多余"})"),
        *layout,
        AliasError::InvalidDocument,
        "unique_rejects_reason");
}

/// @brief Unresolved 必须给出非空白原因。
/// @return 缺少 reason 时结果为 InvalidDocument。
bool unresolvedRequiresReason() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("unresolved_requires_reason");
    }
    return expectError(
               aliasDocument(R"({"name":"CAN_Msg","status":"unresolved"})"),
               *layout,
               AliasError::InvalidDocument,
               "unresolved_requires_reason")
        && expectError(
               aliasDocument(R"({"name":"CAN_Msg","status":"unresolved","reason":"   "})"),
               *layout,
               AliasError::InvalidDocument,
               "unresolved_requires_reason");
}

/// @brief Unresolved 不能携带路径或 shared 标志。
/// @return 带 firmware_path 或 shared 时结果为 InvalidDocument。
bool unresolvedRejectsPath() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("unresolved_rejects_path");
    }
    return expectError(
               aliasDocument(
                   R"({"name":"CAN_Msg","status":"unresolved","reason":"没有槽位","firmware_path":"Other.path"})"),
               *layout,
               AliasError::InvalidDocument,
               "unresolved_rejects_path")
        && expectError(
               aliasDocument(R"({"name":"CAN_Msg","status":"unresolved","reason":"没有槽位","shared":false})"),
               *layout,
               AliasError::InvalidDocument,
               "unresolved_rejects_path");
}

/// @brief 空白名称、空白分类和未知 status 都拒绝整份文档。
/// @return 三个样本都是 InvalidDocument 时为 true。
bool rejectsBlankAndUnknownStatus() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("rejects_blank_and_unknown_status");
    }
    return expectError(
               aliasDocument(R"({"name":"  ","status":"unique","firmware_path":"RNC_param.refernum"})"),
               *layout,
               AliasError::InvalidDocument,
               "rejects_blank_and_unknown_status")
        && expectError(
               aliasDocument(
                   R"({"name":"Refer_Num","category":" ","status":"unique","firmware_path":"RNC_param.refernum"})"),
               *layout,
               AliasError::InvalidDocument,
               "rejects_blank_and_unknown_status")
        && expectError(
               aliasDocument(R"({"name":"Refer_Num","status":"mapped","firmware_path":"RNC_param.refernum"})"),
               *layout,
               AliasError::InvalidDocument,
               "rejects_blank_and_unknown_status");
}

/// @brief 同一对象里的重复键不能让后一个值盖掉前一个。
/// @return 结果为 InvalidDocument 时为 true。
bool duplicateKey() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("duplicate_key");
    }
    return expectError(
        aliasDocument(
            R"({"name":"Refer_Num","name":"Other","status":"unique","firmware_path":"RNC_param.refernum"})"),
        *layout,
        AliasError::InvalidDocument,
        "duplicate_key");
}

/// @brief 尾逗号和根对象里的重复键都不是合法文档。
/// @return 两个样本都是 InvalidDocument 时为 true。
bool malformedAliasDocument() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("malformed_alias_document");
    }
    return expectError(
               R"({"schema_version":1,"aliases":[{"name":"CAN_Msg","status":"unresolved","reason":"没有槽位"},]})",
               *layout,
               AliasError::InvalidDocument,
               "malformed_alias_document")
        && expectError(
               R"({"schema_version":1,"schema_version":1,"aliases":[]})",
               *layout,
               AliasError::InvalidDocument,
               "malformed_alias_document");
}

/// @brief 打不开的别名文件失败，不返回空表。
/// @return 结果为 InvalidDocument 时为 true。
bool missingAliasFile() {
    const std::optional<FirmwareLayout> layout = loadLayout(kPack4Manifest);
    if (!layout) {
        return fail("missing_alias_file");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadFile("catalog/does-not-exist-aliases.json", *layout);
    if (loaded.tryTable() != nullptr || loaded.tryError() == nullptr
        || *loaded.tryError() != AliasError::InvalidDocument) {
        return fail("missing_alias_file");
    }
    return true;
}

/// @brief 正式别名文件里的 Refer_Num 指向 RNC_param.refernum，并且该路径仍未核验。
/// @param aliasPath 别名 JSON 路径，只在本次调用期间借用。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return 名称、分类、路径和 Unverified 都匹配时为 true。
bool shippedReferNum(const char* aliasPath, const char* manifestPath) {
    const LayoutLoad manifest = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!manifest.ok()) {
        return fail("shipped_refer_num");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadFile(aliasPath, manifest.layout());
    if (!loaded.ok()) {
        return fail("shipped_refer_num");
    }
    const AliasRecord* record = loaded.table().find("Refer_Num");
    if (record == nullptr || record->resolution != AliasResolution::Unique || !record->category
        || *record->category != "ERNC_IN_OUT" || !record->firmwarePath
        || *record->firmwarePath != "RNC_param.refernum" || loaded.table().find("refer_num") != nullptr) {
        return fail("shipped_refer_num");
    }
    const LayoutResolve resolved = manifest.layout().resolve(*record->firmwarePath);
    if (resolved.tryError() == nullptr || *resolved.tryError() != LayoutError::Unverified) {
        return fail("shipped_refer_num");
    }
    return true;
}

/// @brief 正式别名文件里的 CAN_Msg 不绑定 Can_Msg 数组。
/// @param aliasPath 别名 JSON 路径，只在本次调用期间借用。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return CAN_Msg 为 Unresolved，Can_Msg 仍是未核验数组路径时为 true。
bool shippedCanMsg(const char* aliasPath, const char* manifestPath) {
    const LayoutLoad manifest = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!manifest.ok()) {
        return fail("shipped_can_msg");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadFile(aliasPath, manifest.layout());
    if (!loaded.ok()) {
        return fail("shipped_can_msg");
    }
    const AliasRecord* record = loaded.table().find("CAN_Msg");
    if (record == nullptr || record->resolution != AliasResolution::Unresolved || record->firmwarePath
        || !record->category || *record->category != "Check" || !record->reason || record->reason->empty()
        || loaded.table().find("Can_Msg") != nullptr) {
        return fail("shipped_can_msg");
    }
    const LayoutResolve excelName = manifest.layout().resolve("CAN_Msg");
    const LayoutResolve arrayPath = manifest.layout().resolve("Can_Msg");
    if (excelName.tryError() == nullptr || *excelName.tryError() != LayoutError::NotFound
        || arrayPath.tryError() == nullptr || *arrayPath.tryError() != LayoutError::Unverified) {
        return fail("shipped_can_msg");
    }
    return true;
}

/// @brief 正式别名文件里的高级参数入口只有四条有路径的记录。
/// @param aliasPath 别名 JSON 路径，只在本次调用期间借用。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return 四条都是 Unique，且 OrderFour_Flag 不在这个分类里时为 true。
bool shippedAdvanced(const char* aliasPath, const char* manifestPath) {
    const LayoutLoad manifest = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!manifest.ok()) {
        return fail("shipped_advanced");
    }
    const AliasLoad loaded = ParameterAliasLoader::loadFile(aliasPath, manifest.layout());
    if (!loaded.ok()) {
        return fail("shipped_advanced");
    }
    const auto advanced = loaded.table().findByCategory(kAdvancedParameterCategory);
    if (advanced.size() != 4 || advanced[0]->name != "MiuControlFlag" || advanced[1]->name != "TestMode"
        || advanced[2]->name != "RNC_MIC_Gain_BT" || advanced[3]->name != "Datarecordflag") {
        return fail("shipped_advanced");
    }
    for (const AliasRecord* record : advanced) {
        if (record->resolution != AliasResolution::Unique || !record->firmwarePath
            || *record->firmwarePath != record->name) {
            return fail("shipped_advanced");
        }
        const LayoutResolve resolved = manifest.layout().resolve(*record->firmwarePath);
        if (resolved.tryError() == nullptr || *resolved.tryError() != LayoutError::Unverified) {
            return fail("shipped_advanced");
        }
    }
    const AliasRecord* orderFour = loaded.table().find("OrderFour_Flag");
    if (orderFour == nullptr || orderFour->resolution != AliasResolution::Unresolved || orderFour->firmwarePath
        || orderFour->category) {
        return fail("shipped_advanced");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param aliasPath 正式别名 JSON 路径。合成样本不读取它。
/// @param manifestPath 正式 manifest 路径。合成样本不读取它。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const char* aliasPath, const char* manifestPath, bool& recognized) {
    recognized = true;
    if (name == "unique_pack4_stays_unverified") {
        return uniquePack4StaysUnverified();
    }
    if (name == "unique_verified_keeps_geometry") {
        return uniqueVerifiedKeepsGeometry();
    }
    if (name == "out_of_range_path_is_not_unknown") {
        return outOfRangePathIsNotUnknown();
    }
    if (name == "unknown_path") {
        return unknownPath();
    }
    if (name == "case_sensitive_name") {
        return caseSensitiveName();
    }
    if (name == "case_sensitive_path") {
        return caseSensitivePath();
    }
    if (name == "unresolved_has_no_path") {
        return unresolvedHasNoPath();
    }
    if (name == "absent_name_is_not_unresolved") {
        return absentNameIsNotUnresolved();
    }
    if (name == "duplicate_name_drops_table") {
        return duplicateNameDropsTable();
    }
    if (name == "path_conflict_drops_table") {
        return pathConflictDropsTable();
    }
    if (name == "explicit_share") {
        return explicitShare();
    }
    if (name == "lone_shared") {
        return loneShared();
    }
    if (name == "partial_share_of_three") {
        return partialShareOfThree();
    }
    if (name == "shared_category_keeps_order") {
        return sharedCategoryKeepsOrder();
    }
    if (name == "empty_aliases") {
        return emptyAliases();
    }
    if (name == "unique_rejects_reason") {
        return uniqueRejectsReason();
    }
    if (name == "unresolved_requires_reason") {
        return unresolvedRequiresReason();
    }
    if (name == "unresolved_rejects_path") {
        return unresolvedRejectsPath();
    }
    if (name == "rejects_blank_and_unknown_status") {
        return rejectsBlankAndUnknownStatus();
    }
    if (name == "duplicate_key") {
        return duplicateKey();
    }
    if (name == "malformed_alias_document") {
        return malformedAliasDocument();
    }
    if (name == "missing_alias_file") {
        return missingAliasFile();
    }
    if (name == "shipped_refer_num") {
        return shippedReferNum(aliasPath, manifestPath);
    }
    if (name == "shipped_can_msg") {
        return shippedCanMsg(aliasPath, manifestPath);
    }
    if (name == "shipped_advanced") {
        return shippedAdvanced(aliasPath, manifestPath);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个别名解析单元测试。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是 parameter_aliases.json，argv[3] 是 layout_manifest.json。三段文本都只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_alias_resolver_tests <case> <parameter_aliases.json> <layout_manifest.json>\n";
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
