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
using ernc::domain::kAdvancedParameterCategory;

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

/// @brief 确认正式别名文件里的显式绑定，以及对应路径仍然未核验。
/// @param aliasPath 别名 JSON 路径，只在本次调用期间借用。
/// @param manifestPath 正式 manifest 路径，只在本次调用期间借用。
/// @return 14 条记录、分类、路径和 Unresolved 原因都匹配时为 true。
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
    if (table.schemaVersion() != 1 || table.size() != 14) {
        return fail("alias-size");
    }

    struct Expected {
        /// Excel 名称原文。
        const char* name;

        /// 主分类。空指针表示这条记录没有分类。
        const char* category;

        /// true 表示 Unique，并带 firmwarePath。
        bool unique;

        /// Unique 时期望的 manifest 路径。Unresolved 时为空指针。
        const char* firmwarePath;

        /// Unresolved 时期望的原因原文。Unique 时为空指针。
        const char* reason;
    };
    const Expected expected[] = {
        {"Refer_Num", "ERNC_IN_OUT", true, "RNC_param.refernum", nullptr},
        {"Err_Num", "ERNC_IN_OUT", true, "RNC_param.errnum", nullptr},
        {"Spk_Num", "ERNC_IN_OUT", true, "RNC_param.spknum", nullptr},
        {"RNC_Divergence_Con_Flag", "Threshold", true, "rnc_dicergence.rnc_divergence_con_flag", nullptr},
        {"RNC_Divergence_Counting", "Threshold", true, "rnc_dicergence.rnc_divergence_counting", nullptr},
        {"RNC_Divergence_OFF_Time", "Threshold", true, "rnc_dicergence.rnc_divergence_off_time", nullptr},
        {"RNC_Divergence_threshold", "Threshold", true, "rnc_dicergence.rnc_diverge_threshold", nullptr},
        {"RNCSpkFAdapMax", "Threshold", true, "rnc_dicergence.RNCSpkFAdapMax", nullptr},
        {"CAN_Msg", "Check", false, nullptr,
            "36 个 CAN 名称没有 Can_Msg 槽位，不能把 CAN_Msg 收成数组元素路径。"},
        {"MiuControlFlag", "高级参数", true, "MiuControlFlag", nullptr},
        {"TestMode", "高级参数", true, "TestMode", nullptr},
        {"RNC_MIC_Gain_BT", "高级参数", true, "RNC_MIC_Gain_BT", nullptr},
        {"Datarecordflag", "高级参数", true, "Datarecordflag", nullptr},
        {"OrderFour_Flag", nullptr, false, nullptr, "OrderFour_Flag 不在 manifest 字段树里。"},
    };

    for (std::size_t index = 0; index < table.size(); ++index) {
        const AliasRecord& record = table.entries()[index];
        const Expected& item = expected[index];
        if (record.name != item.name || record.shared) {
            return fail(item.name);
        }
        if ((record.category.has_value()) != (item.category != nullptr)
            || (item.category != nullptr && *record.category != item.category)) {
            return fail(item.name);
        }
        if (item.unique) {
            if (record.resolution != AliasResolution::Unique || !record.firmwarePath
                || *record.firmwarePath != item.firmwarePath || record.reason) {
                return fail(item.name);
            }
            const LayoutResolve resolved = layout.resolve(*record.firmwarePath);
            if (resolved.tryField() != nullptr || resolved.tryError() == nullptr
                || *resolved.tryError() != LayoutError::Unverified) {
                return fail(item.name);
            }
        } else if (record.resolution != AliasResolution::Unresolved || record.firmwarePath
                   || !record.reason || *record.reason != item.reason) {
            return fail(item.name);
        }
        if (table.find(item.name) != &record) {
            return fail("find");
        }
    }

    if (table.find("") != nullptr || table.find("Can_Msg") != nullptr || table.find("refernum") != nullptr) {
        return fail("case-fold");
    }
    const LayoutResolve canArray = layout.resolve("Can_Msg");
    if (canArray.tryError() == nullptr || *canArray.tryError() != LayoutError::Unverified) {
        return fail("can-array-still-unverified");
    }
    const LayoutResolve orderFour = layout.resolve("OrderFour_Flag");
    if (orderFour.tryError() == nullptr || *orderFour.tryError() != LayoutError::NotFound) {
        return fail("order-four-layout");
    }

    const std::vector<const AliasRecord*> advanced = table.findByCategory(kAdvancedParameterCategory);
    if (advanced.size() != 4 || advanced[0]->name != "MiuControlFlag" || advanced[1]->name != "TestMode"
        || advanced[2]->name != "RNC_MIC_Gain_BT" || advanced[3]->name != "Datarecordflag") {
        return fail("advanced-category");
    }
    if (table.findByCategory("").size() != 0 || table.findByCategory("ERNC_IN_OUT").size() != 3
        || table.findByCategory("Threshold").size() != 5 || table.findByCategory("Check").size() != 1) {
        return fail("categories");
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
