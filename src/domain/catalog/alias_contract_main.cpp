#include "ernc/domain/alias_resolver.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

using ernc::domain::AliasError;
using ernc::domain::AliasLoad;
using ernc::domain::AliasRecord;
using ernc::domain::AliasResolution;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutError;
using ernc::domain::LayoutLoad;
using ernc::domain::LayoutResolve;
using ernc::domain::ParameterAliasLoader;
using ernc::domain::ParameterAliasTable;

/// @brief 打印失败并记下一个检查没有通过。
/// @param name 检查名，只在本次调用期间借用。调用方保留这段字面量。
/// @return 恒为 false，便于用 && 串起后续检查。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 包一份只含两个 pack4 字段的最小 manifest。
/// @param firstPath 第一个字段路径，只在本次调用期间借用。offset 0，slot 0。
/// @param secondPath 第二个字段路径，只在本次调用期间借用。offset 4，slot 1。
/// @return 调用方拥有的 manifest JSON。两个字段都是 4 字节，总长度 8 字节。
std::string twoFieldManifest(std::string_view firstPath, std::string_view secondPath) {
    std::string text = R"({"alignment":4,"base":0,"total_size":8,"total_slots":2,"fields":[)";
    text += R"({"path":")";
    text.append(firstPath);
    text += R"(","offset":0,"size":4,"slot":0,"slot_count":1,"type":"int32_t","shape":[],"status":"pack4-sequential","writable":false},)";
    text += R"({"path":")";
    text.append(secondPath);
    text += R"(","offset":4,"size":4,"slot":1,"slot_count":1,"type":"float","shape":[],"status":"pack4-sequential","writable":false}]})";
    return text;
}

/// @brief 包装一份别名文档。
/// @param aliases 放进 aliases 数组的 JSON 片段，不含外层方括号。只在本次调用期间借用。
/// @return 调用方拥有的完整 JSON 文本。schema 版本固定为 1。
std::string aliasDocument(std::string_view aliases) {
    std::string text = R"({"schema_version":1,"aliases":[)";
    text.append(aliases);
    text.append("]}");
    return text;
}

/// @brief 确认一次加载以指定错误失败，并且没有别名表。
/// @param jsonText 待校验的 JSON 文本，只在本次调用期间借用。
/// @param layout 核对路径用的布局，只在本次调用期间借用。
/// @param expected 期望的别名错误。
/// @param name 检查名。
/// @return 错误类别匹配且没有别名表时为 true。
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

/// @brief 确认正式别名文件里的成员短名、共享绑定和未绑定文字。
/// @param aliasPath 别名 JSON 路径，只在本次调用期间借用。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return 成员别名指向结构体完整路径，且没有读取范围的文字不带路径时为 true。
bool checkAliasFile(const char* aliasPath, const char* manifestPath) {
    const LayoutLoad manifest = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!manifest.ok()) {
        return fail("manifest-file");
    }
    const FirmwareLayout& layout = manifest.layout();
    const AliasLoad loaded = ParameterAliasLoader::loadFile(aliasPath, layout);
    if (!loaded.ok()) {
        return fail("alias-file");
    }
    const ParameterAliasTable& table = loaded.table();
    if (table.schemaVersion() != 1 || table.size() < 100) {
        return fail("alias-size");
    }

    const AliasRecord* alpha = table.find("alpha1");
    const AliasRecord* refer = table.find("Refer_Num");
    const AliasRecord* referMember = table.find("refernum");
    const AliasRecord* radius = table.find("SpecRadius");
    const AliasRecord* can = table.find("CAN_Msg");
    const AliasRecord* date = table.find("check_date");
    if (alpha == nullptr || alpha->resolution != AliasResolution::Unique || !alpha->firmwarePath
        || *alpha->firmwarePath != "RNC_param.alpha1" || alpha->shared) {
        return fail("alpha1");
    }
    if (refer == nullptr || referMember == nullptr || !refer->shared || !referMember->shared
        || !refer->firmwarePath || !referMember->firmwarePath
        || *refer->firmwarePath != "RNC_param.refernum"
        || *referMember->firmwarePath != "RNC_param.refernum") {
        return fail("refer-num");
    }
    if (radius == nullptr || !radius->firmwarePath || *radius->firmwarePath != "ERNC_TF.SpecRadius") {
        return fail("spec-radius");
    }
    if (can == nullptr || date == nullptr || can->resolution != AliasResolution::Unresolved
        || date->resolution != AliasResolution::Unresolved || can->firmwarePath || date->firmwarePath
        || can->category || date->category) {
        return fail("unresolved-text");
    }
    if (table.find("Can_Msg") != nullptr || table.find("refer_num") != nullptr || table.find("") != nullptr) {
        return fail("case-fold");
    }
    for (const AliasRecord& record : table.entries()) {
        if (record.resolution != AliasResolution::Unique || !record.firmwarePath) {
            continue;
        }
        if (record.firmwarePath->find("Can_Msg[") != std::string::npos) {
            return fail("can-slot");
        }
        const LayoutResolve resolved = layout.resolve(*record.firmwarePath);
        if (resolved.tryError() == nullptr || *resolved.tryError() != LayoutError::Unverified) {
            return fail("alias-unverified");
        }
    }
    const LayoutResolve orderFour = layout.resolve("OrderFour_Flag");
    if (orderFour.tryError() == nullptr || *orderFour.tryError() != LayoutError::NotFound) {
        return fail("order-four-layout");
    }
    return true;
}

/// @brief 用两字段布局检查重复名称、路径冲突、未知路径和显式共享。
/// @return 每个拒绝样本都失败且共享样本成功时为 true。
bool checkRejections() {
    const LayoutLoad manifest = FirmwareLayoutLoader::loadText(twoFieldManifest("RNC_param.refernum", "Other.path"));
    if (!manifest.ok()) {
        return fail("tiny-manifest");
    }
    const FirmwareLayout& layout = manifest.layout();
    const std::string duplicate = aliasDocument(
        R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum"},{"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum"})");
    const std::string samePath = aliasDocument(
        R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum"},{"name":"ReferNum","status":"unique","firmware_path":"RNC_param.refernum"})");
    const std::string oneShared = aliasDocument(
        R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum","shared":true},{"name":"ReferNum","status":"unique","firmware_path":"RNC_param.refernum"})");
    const std::string loneShared = aliasDocument(
        R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum","shared":true})");
    const std::string unknownPath = aliasDocument(
        R"({"name":"Refer_Num","status":"unique","firmware_path":"Refer_Num"})");
    const std::string unresolvedWithPath = aliasDocument(
        R"({"name":"CAN_Msg","status":"unresolved","firmware_path":"Can_Msg","reason":"不应带路径"})");
    const std::string missingPath = aliasDocument(R"({"name":"Refer_Num","status":"unique"})");
    const std::string blankPath = aliasDocument(
        R"({"name":"Refer_Num","status":"unique","firmware_path":"  "})");
    const char* badSchema = R"({"schema_version":2,"aliases":[]})";
    const char* unknownKey =
        R"({"schema_version":1,"aliases":[{"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum","note":"x"}]})";

    if (!expectError(duplicate, layout, AliasError::DuplicateName, "duplicate-name")
        || !expectError(samePath, layout, AliasError::PathConflict, "same-path")
        || !expectError(oneShared, layout, AliasError::PathConflict, "one-shared")
        || !expectError(loneShared, layout, AliasError::InvalidDocument, "lone-shared")
        || !expectError(unknownPath, layout, AliasError::UnknownPath, "unknown-path")
        || !expectError(unresolvedWithPath, layout, AliasError::InvalidDocument, "unresolved-path")
        || !expectError(missingPath, layout, AliasError::InvalidDocument, "missing-path")
        || !expectError(blankPath, layout, AliasError::InvalidDocument, "blank-path")
        || !expectError(badSchema, layout, AliasError::InvalidDocument, "schema-version")
        || !expectError(unknownKey, layout, AliasError::InvalidDocument, "unknown-key")
        || !expectError("{", layout, AliasError::InvalidDocument, "malformed")
        || !expectError("", layout, AliasError::InvalidDocument, "empty")) {
        return false;
    }

    const AliasLoad shared = ParameterAliasLoader::loadText(
        aliasDocument(
            R"({"name":"Refer_Num","status":"unique","firmware_path":"RNC_param.refernum","shared":true},{"name":"ReferNum","status":"unique","firmware_path":"RNC_param.refernum","shared":true})"),
        layout);
    if (!shared.ok() || shared.table().size() != 2 || !shared.table().find("Refer_Num")
        || !shared.table().find("Refer_Num")->shared || !shared.table().find("ReferNum")->firmwarePath
        || *shared.table().find("ReferNum")->firmwarePath != "RNC_param.refernum") {
        return fail("explicit-share");
    }
    const LayoutResolve stillUnverified = layout.resolve("RNC_param.refernum");
    if (stillUnverified.tryError() == nullptr || *stillUnverified.tryError() != LayoutError::Unverified) {
        return fail("share-does-not-verify");
    }
    return true;
}

}  // namespace

/// @brief 运行别名契约检查。
/// @param argc 参数个数，包含程序名。必须等于 3。
/// @param argv 参数文本。argv[1] 是别名 JSON，argv[2] 是 layout manifest。两段路径只在本次进程内借用。
/// @return 全部检查通过时为 0。用法不对时为 2，内容或拒绝样本不符合契约时为 1。
int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc-alias-contract <parameter-aliases.json> <layout-manifest.json>\n";
        return 2;
    }
    if (!checkAliasFile(argv[1], argv[2]) || !checkRejections()) {
        return 1;
    }
    const LayoutLoad manifest = FirmwareLayoutLoader::loadFile(argv[2]);
    if (!manifest.ok()) {
        std::cerr << "failed: manifest-reload\n";
        return 1;
    }
    const AliasLoad missing = ParameterAliasLoader::loadFile("catalog/does-not-exist.json", manifest.layout());
    if (missing.ok() || missing.tryError() == nullptr || *missing.tryError() != AliasError::InvalidDocument) {
        std::cerr << "failed: missing-file\n";
        return 1;
    }
    std::cout << "alias contract passed\n";
    return 0;
}
