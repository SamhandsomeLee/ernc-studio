#include "ernc/domain/catalog.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using ernc::domain::AccessMode;
using ernc::domain::CatalogError;
using ernc::domain::CatalogLoad;
using ernc::domain::DataType;
using ernc::domain::EditorKind;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ValueShape;

/// @brief 打印失败并记下一个检查没有通过。
/// @param name 检查名，只在本次调用期间借用。调用方保留这段字面量。
/// @return 恒为 false，便于用 && 串起后续检查。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 包装一份只含参数数组的最小目录文档。
/// @param parameters 放进 parameters 数组的 JSON 片段，不含外层方括号。只在本次调用期间借用。
/// @return 调用方拥有的完整 JSON 文本。
std::string document(std::string_view parameters) {
    std::string text = R"({"schema_version":1,"catalog_version":"0.1.0","parameters":[)";
    text.append(parameters);
    text.append("]}");
    return text;
}

/// @brief 确认一次加载以指定错误失败，并且没有目录对象。
/// @param jsonText 待校验的 JSON 文本，只在本次调用期间借用。
/// @param expected 期望的目录错误。
/// @param name 检查名。
/// @return 错误类别匹配且没有目录时为 true。
bool expectError(std::string_view jsonText, CatalogError expected, const char* name) {
    const CatalogLoad loaded = ParameterCatalogLoader::loadText(jsonText);
    if (loaded.tryCatalog() != nullptr || loaded.tryError() == nullptr
        || *loaded.tryError() != expected) {
        return fail(name);
    }
    return true;
}

/// @brief 确认正式目录文件里的五条证据记录和原文中文说明。
/// @param path 目录 JSON 路径，只在本次调用期间借用。文件由加载器打开并在返回前关闭。
/// @return 版本、顺序、路径、说明、类型和控制语义都匹配时为 true。
bool checkCatalogFile(const char* path) {
    const CatalogLoad loaded = ParameterCatalogLoader::loadFile(path);
    if (!loaded.ok()) {
        return fail("catalog-file");
    }
    const ParameterCatalog& catalog = *loaded.tryCatalog();
    if (catalog.schemaVersion() != 1 || catalog.catalogVersion() != "0.1.0" || catalog.size() != 5) {
        return fail("catalog-version");
    }

    struct Expected {
        /// 目录主键，也是本检查期望的同名固件路径。
        const char* id;

        /// 期望的 firmware_path 原文。
        const char* firmwarePath;

        /// 分组表中的分类名。
        const char* category;

        /// 参数说明表 B 列原文。
        const char* description;

        /// 同名 manifest 字段对应的数据类型。
        DataType dataType;

        /// 这条记录应载入的控制类型。
        EditorKind editor;

        /// true 表示目录里写了 access。三个开关没有单独的访问证据，应为 false。
        bool hasAccess;

        /// hasAccess 为 true 时才比较。false 时的 ReadOnly 不参与判断。
        AccessMode access;

        /// true 表示必须带 on=0、off=1 的线值。线值没有物理单位。
        bool hasWire;
    };
    const Expected expected[] = {
        {"ANC_OFF", "ANC_OFF", "ERNC_Flag", "ERNC总开关，0开启，1关闭", DataType::Float32,
            EditorKind::Toggle, false, AccessMode::ReadOnly, true},
        {"ENC_OFF", "ENC_OFF", "ERNC_Flag", "ENC开关，0开启，1关闭", DataType::Float32,
            EditorKind::Toggle, false, AccessMode::ReadOnly, true},
        {"RNC_OFF", "RNC_OFF", "ERNC_Flag", "RNC开关，0开启，1关闭", DataType::Float32,
            EditorKind::Toggle, false, AccessMode::ReadOnly, true},
        {"Step", "Step", "RNC_Param", "实时步长显示", DataType::Float32, EditorKind::ReadOnly, true,
            AccessMode::ReadOnly, false},
        {"RNC_refresh", "RNC_refresh", "ERNC_Flag",
            "算法重置，1重置，0算法启动。注：重置1后需要置0开启算法，且需要在车速大于5kmph是重置",
            DataType::Int32, EditorKind::MomentaryCommand, true, AccessMode::Command, false},
    };

    for (std::size_t index = 0; index < catalog.size(); ++index) {
        const auto& entry = catalog.entries()[index];
        const Expected& item = expected[index];
        if (entry.id.text() != item.id || entry.firmwarePath != item.firmwarePath
            || !entry.category || *entry.category != item.category
            || entry.descriptionZh != item.description || !entry.dataType
            || *entry.dataType != item.dataType || !entry.shape || *entry.shape != ValueShape::Scalar
            || entry.editor != item.editor || entry.access.has_value() != item.hasAccess) {
            return fail(item.id);
        }
        if (item.hasAccess && *entry.access != item.access) {
            return fail(item.id);
        }
        if (item.hasWire) {
            if (!entry.wireLevels || entry.wireLevels->on != 0 || entry.wireLevels->off != 1) {
                return fail(item.id);
            }
        } else if (entry.wireLevels) {
            return fail(item.id);
        }
        const auto* found = catalog.find(item.id);
        if (found != &entry) {
            return fail("find");
        }
    }
    if (catalog.find("") != nullptr || catalog.find("Refer_Num") != nullptr) {
        return fail("missing-id");
    }
    return true;
}

/// @brief 运行重复 id、非法控制类型、缺失路径和其他拒绝样本。
/// @return 每个样本都返回约定错误且不附带目录时为 true。
bool checkRejections() {
    const std::string duplicate = document(
        R"({"id":"ANC_OFF","firmware_path":"ANC_OFF","description_zh":"第一","editor":"numeric"},{"id":"ANC_OFF","firmware_path":"ENC_OFF","description_zh":"第二","editor":"numeric"})");
    const std::string invalidControl = document(
        R"({"id":"ANC_OFF","firmware_path":"ANC_OFF","description_zh":"说明","editor":"slider"})");
    const std::string missingPath = document(
        R"({"id":"ANC_OFF","description_zh":"说明","editor":"numeric"})");
    const std::string blankPath = document(
        R"({"id":"ANC_OFF","firmware_path":"  ","description_zh":"说明","editor":"numeric"})");
    const std::string toggleWithoutWire = document(
        R"({"id":"ANC_OFF","firmware_path":"ANC_OFF","description_zh":"说明","editor":"toggle"})");
    const std::string wireOnReadOnly = document(
        R"({"id":"Step","firmware_path":"Step","description_zh":"说明","editor":"read_only","wire_semantics":{"on":0,"off":1}})");
    const char* badSchema =
        R"({"schema_version":2,"catalog_version":"0.1.0","parameters":[]})";
    const char* unknownKey =
        R"({"schema_version":1,"catalog_version":"0.1.0","parameters":[{"id":"ANC_OFF","firmware_path":"ANC_OFF","description_zh":"说明","editor":"numeric","display_name_zh":"ANC 算法"}]})";

    return expectError(duplicate, CatalogError::DuplicateId, "duplicate-id")
        && expectError(invalidControl, CatalogError::InvalidControl, "invalid-control")
        && expectError(missingPath, CatalogError::MissingPath, "missing-path")
        && expectError(blankPath, CatalogError::MissingPath, "blank-path")
        && expectError(toggleWithoutWire, CatalogError::SemanticConflict, "toggle-wire")
        && expectError(wireOnReadOnly, CatalogError::SemanticConflict, "readonly-wire")
        && expectError(badSchema, CatalogError::InvalidDocument, "schema-version")
        && expectError(unknownKey, CatalogError::InvalidDocument, "unknown-key")
        && expectError("{", CatalogError::InvalidDocument, "malformed")
        && expectError("", CatalogError::InvalidDocument, "empty");
}

}  // namespace

/// @brief 运行目录契约检查。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv 参数文本。argv[1] 是 parameter_catalog.json 的路径，只在本次进程内借用。
/// @return 全部检查通过时为 0。用法不对时为 2，目录内容或拒绝样本不符合契约时为 1。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc-catalog-contract <parameter-catalog.json>\n";
        return 2;
    }
    if (!checkCatalogFile(argv[1]) || !checkRejections()) {
        return 1;
    }
    const CatalogLoad missing = ParameterCatalogLoader::loadFile("catalog/source/does-not-exist.json");
    if (missing.ok() || missing.tryError() == nullptr
        || *missing.tryError() != CatalogError::InvalidDocument) {
        std::cerr << "failed: missing-file\n";
        return 1;
    }
    std::cout << "catalog contract passed\n";
    return 0;
}
