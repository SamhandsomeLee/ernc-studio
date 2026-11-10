#include "ernc/domain/control_semantics.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using ernc::domain::AccessMode;
using ernc::domain::CatalogLoad;
using ernc::domain::DataType;
using ernc::domain::EditorKind;
using ernc::domain::ErrorCode;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterDefinition;
using ernc::domain::ParameterId;
using ernc::domain::ScalarValue;
using ernc::domain::TogglePosition;
using ernc::domain::WireLevels;

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

/// @brief 三个反向开关的 0 是 On，1 是 Off。
/// @param catalogPath 目录路径，只在本次调用期间借用。
/// @return ANC_OFF、ENC_OFF、RNC_OFF 的 0/1/其他值都符合反向语义时为 true。
bool reverseSwitches(const char* catalogPath) {
    const char* names[] = {"ANC_OFF", "ENC_OFF", "RNC_OFF"};
    for (const char* name : names) {
        const CatalogLoad loaded = ParameterCatalogLoader::loadFile(catalogPath);
        if (!loaded.ok()) {
            return fail("reverse_switches");
        }
        const ParameterDefinition* parameter = loaded.catalog().find(name);
        if (parameter == nullptr) {
            return fail("reverse_switches");
        }
        if (ernc::domain::decodeToggle(*parameter, ScalarValue::fromFloat(0.f)) != TogglePosition::On
            || ernc::domain::decodeToggle(*parameter, ScalarValue::fromFloat(1.f)) != TogglePosition::Off
            || ernc::domain::decodeToggle(*parameter, ScalarValue::fromFloat(2.f)) != TogglePosition::Unknown
            || ernc::domain::decodeToggle(*parameter, ScalarValue::fromFloat(std::numeric_limits<float>::quiet_NaN()))
                != TogglePosition::Unknown
            || ernc::domain::decodeToggle(*parameter, ScalarValue::fromInt32(0)) != TogglePosition::Unknown) {
            return fail("reverse_switches");
        }
    }
    return true;
}

/// @brief 没有 wire_semantics 的 0 和 1 不是开或关。
/// @param catalogPath 目录路径，只在本次调用期间借用。
/// @return Bypass_mod、check_mod、Step 和 RNC_refresh 都是 Unknown 时为 true。
bool unconfirmedStayUnknown(const char* catalogPath) {
    const char* names[] = {"Bypass_mod", "check_mod", "Step", "RNC_refresh"};
    for (const char* name : names) {
        const CatalogLoad loaded = ParameterCatalogLoader::loadFile(catalogPath);
        if (!loaded.ok()) {
            return fail("unconfirmed_stay_unknown");
        }
        const ParameterDefinition* parameter = loaded.catalog().find(name);
        if (parameter == nullptr || parameter->wireLevels) {
            return fail("unconfirmed_stay_unknown");
        }
        if (ernc::domain::decodeToggle(*parameter, ScalarValue::fromFloat(0.f)) != TogglePosition::Unknown
            || ernc::domain::decodeToggle(*parameter, ScalarValue::fromFloat(1.f)) != TogglePosition::Unknown
            || ernc::domain::decodeToggle(*parameter, ScalarValue::fromInt32(0)) != TogglePosition::Unknown
            || ernc::domain::decodeToggle(*parameter, ScalarValue::fromInt32(1)) != TogglePosition::Unknown) {
            return fail("unconfirmed_stay_unknown");
        }
    }
    return true;
}

/// @brief 选择 On 编出 float 0，选择 Off 编出 float 1。
/// @param catalogPath 目录路径，只在本次调用期间借用。
/// @return ANC_OFF 的编码结果符合反向线值时为 true。
bool encodeReverseLevels(const char* catalogPath) {
    const CatalogLoad loaded = ParameterCatalogLoader::loadFile(catalogPath);
    if (!loaded.ok()) {
        return fail("encode_reverse_levels");
    }
    const ParameterDefinition* parameter = loaded.catalog().find("ANC_OFF");
    if (parameter == nullptr) {
        return fail("encode_reverse_levels");
    }
    const auto on = ernc::domain::encodeToggle(*parameter, TogglePosition::On);
    const auto off = ernc::domain::encodeToggle(*parameter, TogglePosition::Off);
    if (!on.ok() || !off.ok() || on.value() != ScalarValue::fromFloat(0.f) || off.value() != ScalarValue::fromFloat(1.f)) {
        return fail("encode_reverse_levels");
    }
    return true;
}

/// @brief Unknown 和没有线值的参数不能编出 0 或 1。
/// @param catalogPath 目录路径，只在本次调用期间借用。
/// @return 两次编码都返回 InvalidParameter 时为 true。
bool encodeRejectsUnknown(const char* catalogPath) {
    const CatalogLoad loaded = ParameterCatalogLoader::loadFile(catalogPath);
    if (!loaded.ok()) {
        return fail("encode_rejects_unknown");
    }
    const ParameterDefinition* anc = loaded.catalog().find("ANC_OFF");
    const ParameterDefinition* bypass = loaded.catalog().find("Bypass_mod");
    if (anc == nullptr || bypass == nullptr) {
        return fail("encode_rejects_unknown");
    }
    const auto unknown = ernc::domain::encodeToggle(*anc, TogglePosition::Unknown);
    const auto missing = ernc::domain::encodeToggle(*bypass, TogglePosition::On);
    if (unknown.ok() || missing.ok() || unknown.tryError() == nullptr || missing.tryError() == nullptr
        || *unknown.tryError() != ErrorCode::InvalidParameter || *missing.tryError() != ErrorCode::InvalidParameter) {
        return fail("encode_rejects_unknown");
    }
    return true;
}

/// @brief 整数线值不接受浮点 0。
/// @return float 0 为 Unknown，int32 0 为 On 时为 true。
bool integerLevelRejectsFloat() {
    ParameterDefinition parameter{
        identifier("synthetic"),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        DataType::Int32,
        std::nullopt,
        AccessMode::ReadWrite,
        EditorKind::Toggle,
        WireLevels{0, 1}};
    if (ernc::domain::decodeToggle(parameter, ScalarValue::fromFloat(0.f)) != TogglePosition::Unknown
        || ernc::domain::decodeToggle(parameter, ScalarValue::fromInt32(0)) != TogglePosition::On
        || ernc::domain::decodeToggle(parameter, ScalarValue::fromInt32(1)) != TogglePosition::Off) {
        return fail("integer_level_rejects_float");
    }
    const auto encoded = ernc::domain::encodeToggle(parameter, TogglePosition::On);
    if (!encoded.ok() || encoded.value() != ScalarValue::fromInt32(0)) {
        return fail("integer_level_rejects_float");
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
    if (name == "reverse_switches") {
        return reverseSwitches(catalogPath);
    }
    if (name == "unconfirmed_stay_unknown") {
        return unconfirmedStayUnknown(catalogPath);
    }
    if (name == "encode_reverse_levels") {
        return encodeReverseLevels(catalogPath);
    }
    if (name == "encode_rejects_unknown") {
        return encodeRejectsUnknown(catalogPath);
    }
    if (name == "integer_level_rejects_float") {
        return integerLevelRejectsFloat();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个开关线值单元测试。
/// @param argc 参数个数，包含程序名。必须等于 3。
/// @param argv argv[1] 是用例名，argv[2] 是 parameter_catalog.json。两段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_toggle_semantics_tests <case> <parameter_catalog.json>\n";
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
