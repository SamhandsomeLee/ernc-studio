#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/validation.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using ernc::domain::AccessMode;
using ernc::domain::CatalogLoad;
using ernc::domain::DataType;
using ernc::domain::ErrorCode;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterDefinition;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::ScalarValue;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 做成一个非空参数标识。
/// @param text 标识文本，只在本次调用期间借用。测试只用非空字面量。
/// @return parse 成功后的标识副本。
ParameterId id(const char* text) {
    return ParameterId::parse(text).value();
}

/// @brief 做成一条只含本用例需要的权限和类型的定义。
/// @param name 参数标识文本。
/// @param access 访问声明。空表示目录没有写权限。
/// @param dataType 标量类型。空表示目录没有声明类型。
/// @return 调用方拥有的定义。没有固件路径、说明、控件和线值。
ParameterDefinition definition(
    const char* name,
    const std::optional<AccessMode>& access,
    const std::optional<DataType>& dataType) {
    return ParameterDefinition{
        id(name),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        dataType,
        std::nullopt,
        access,
        std::nullopt,
        std::nullopt};
}

/// @brief 确认校验失败且错误码符合预期。
/// @param definition 参数定义。
/// @param value 想暂存的标量。
/// @param expected 期望的错误码。
/// @param name 用例名。
/// @return 失败且错误码匹配时为 true。
bool expectRejected(
    const ParameterDefinition& parameter,
    const ScalarValue& value,
    ErrorCode expected,
    const char* name) {
    const auto decision = ernc::domain::validateStagedValue(parameter, value);
    if (decision.ok() || decision.tryError() == nullptr || *decision.tryError() != expected) {
        return fail(name);
    }
    return true;
}

/// @brief 正式目录里的 Step 拒绝普通暂存，并且不写入 RuntimeStore。
/// @param catalogPath 参数目录路径，只在本次调用期间借用。
/// @return 错误码为 ReadOnlyViolation 且 Store 仍为空时为 true。
bool stepRejected(const char* catalogPath) {
    const CatalogLoad loaded = ParameterCatalogLoader::loadFile(catalogPath);
    if (!loaded.ok()) {
        return fail("step_rejected");
    }
    const ParameterDefinition* step = loaded.catalog().find("Step");
    if (step == nullptr) {
        return fail("step_rejected");
    }
    ParameterRuntimeStore store;
    const auto decision = ernc::domain::validateStagedValue(*step, ScalarValue::fromFloat(1.f));
    if (decision.ok() || decision.tryError() == nullptr
        || *decision.tryError() != ErrorCode::ReadOnlyViolation || store.size() != 0) {
        return fail("step_rejected");
    }
    return true;
}

/// @brief 瞬时命令不能按普通数值暂存。
/// @param catalogPath 参数目录路径，只在本次调用期间借用。
/// @return RNC_refresh 返回 InvalidParameter 时为 true。
bool commandRejected(const char* catalogPath) {
    const CatalogLoad loaded = ParameterCatalogLoader::loadFile(catalogPath);
    if (!loaded.ok()) {
        return fail("command_rejected");
    }
    const ParameterDefinition* refresh = loaded.catalog().find("RNC_refresh");
    if (refresh == nullptr) {
        return fail("command_rejected");
    }
    return expectRejected(
        *refresh, ScalarValue::fromInt32(1), ErrorCode::InvalidParameter, "command_rejected");
}

/// @brief 已声明 read_write 且类型匹配的有限浮点可以通过。
/// @return 成功且没有错误码时为 true。
bool readWriteFloatAccepted() {
    const auto decision = ernc::domain::validateStagedValue(
        definition("ANC_OFF", AccessMode::ReadWrite, DataType::Float32), ScalarValue::fromFloat(1.25f));
    if (!decision.ok() || decision.tryError() != nullptr) {
        return fail("read_write_float_accepted");
    }
    return true;
}

/// @brief 声明为 float32 时不能暂存 int32。
/// @return 错误码为 InvalidParameter 时为 true。
bool typeMismatchRejected() {
    return expectRejected(
        definition("ANC_OFF", AccessMode::ReadWrite, DataType::Float32),
        ScalarValue::fromInt32(1),
        ErrorCode::InvalidParameter,
        "type_mismatch_rejected");
}

/// @brief 非有限浮点不能通过。
/// @return NaN 和无穷大都返回 InvalidParameter 时为 true。
bool nonFiniteRejected() {
    const ParameterDefinition parameter = definition("ANC_OFF", AccessMode::ReadWrite, DataType::Float32);
    return expectRejected(
               parameter,
               ScalarValue::fromFloat(std::numeric_limits<float>::quiet_NaN()),
               ErrorCode::InvalidParameter,
               "non_finite_rejected")
        && expectRejected(
               parameter,
               ScalarValue::fromFloat(std::numeric_limits<float>::infinity()),
               ErrorCode::InvalidParameter,
               "non_finite_rejected");
}

/// @brief int32 与 uint32 在种类匹配时可以通过。
/// @return 两种成功时为 true。
bool integerKindsAccepted() {
    const auto signedValue = ernc::domain::validateStagedValue(
        definition("count", AccessMode::ReadWrite, DataType::Int32), ScalarValue::fromInt32(-2));
    const auto unsignedValue = ernc::domain::validateStagedValue(
        definition("slot", AccessMode::ReadWrite, DataType::UInt32), ScalarValue::fromUInt32(2u));
    if (!signedValue.ok() || !unsignedValue.ok()) {
        return fail("integer_kinds_accepted");
    }
    return true;
}

/// @brief 权限没有声明时不能当作可写。
/// @return 错误码为 InvalidParameter 时为 true。
bool missingAccessRejected() {
    return expectRejected(
        definition("HornTime", std::nullopt, DataType::Float32),
        ScalarValue::fromFloat(1.f),
        ErrorCode::InvalidParameter,
        "missing_access_rejected");
}

/// @brief 结构体区域没有标量 data_type。本阶段不猜测类型，read_write 可以通过。
/// @param catalogPath 参数目录路径，只在本次调用期间借用。
/// @return check_data 接受一个 float 时为 true。
bool undeclaredTypeAccepted(const char* catalogPath) {
    const CatalogLoad loaded = ParameterCatalogLoader::loadFile(catalogPath);
    if (!loaded.ok()) {
        return fail("undeclared_type_accepted");
    }
    const ParameterDefinition* region = loaded.catalog().find("check_data");
    if (region == nullptr || !region->access || *region->access != AccessMode::ReadWrite || region->dataType) {
        return fail("undeclared_type_accepted");
    }
    const auto decision = ernc::domain::validateStagedValue(*region, ScalarValue::fromFloat(0.5f));
    if (!decision.ok()) {
        return fail("undeclared_type_accepted");
    }
    return true;
}

/// @brief 正式目录中的标量类型与 param.md 的字段类型一致。
/// @param catalogPath 参数目录路径，只在本次调用期间借用。
/// @return alpha1 为 float32、refernum 为 int32、start_fre_num 为 uint32，且种类不匹配会被拒绝时为 true。
bool catalogTypesMatchHeader(const char* catalogPath) {
    const CatalogLoad loaded = ParameterCatalogLoader::loadFile(catalogPath);
    if (!loaded.ok()) {
        return fail("catalog_types_match_header");
    }
    const ParameterDefinition* alpha = loaded.catalog().find("RNC_param.alpha1");
    const ParameterDefinition* refer = loaded.catalog().find("RNC_param.refernum");
    const ParameterDefinition* frequency = loaded.catalog().find("start_fre_num");
    if (alpha == nullptr || refer == nullptr || frequency == nullptr || !alpha->dataType || !refer->dataType
        || !frequency->dataType || *alpha->dataType != DataType::Float32 || *refer->dataType != DataType::Int32
        || *frequency->dataType != DataType::UInt32) {
        return fail("catalog_types_match_header");
    }
    if (ernc::domain::validateStagedValue(*alpha, ScalarValue::fromFloat(0.5f)).ok()
        && !ernc::domain::validateStagedValue(*alpha, ScalarValue::fromInt32(1)).ok()
        && ernc::domain::validateStagedValue(*refer, ScalarValue::fromInt32(4)).ok()
        && ernc::domain::validateStagedValue(*frequency, ScalarValue::fromUInt32(8u)).ok()) {
        return true;
    }
    return fail("catalog_types_match_header");
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param catalogPath 正式参数目录路径。合成定义的用例不读取它。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const char* catalogPath, bool& recognized) {
    recognized = true;
    if (name == "step_rejected") {
        return stepRejected(catalogPath);
    }
    if (name == "command_rejected") {
        return commandRejected(catalogPath);
    }
    if (name == "read_write_float_accepted") {
        return readWriteFloatAccepted();
    }
    if (name == "type_mismatch_rejected") {
        return typeMismatchRejected();
    }
    if (name == "non_finite_rejected") {
        return nonFiniteRejected();
    }
    if (name == "integer_kinds_accepted") {
        return integerKindsAccepted();
    }
    if (name == "missing_access_rejected") {
        return missingAccessRejected();
    }
    if (name == "undeclared_type_accepted") {
        return undeclaredTypeAccepted(catalogPath);
    }
    if (name == "catalog_types_match_header") {
        return catalogTypesMatchHeader(catalogPath);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个参数校验单元测试。
/// @param argc 参数个数，包含程序名。必须等于 3。
/// @param argv argv[1] 是用例名，argv[2] 是 parameter_catalog.json。两段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_validation_tests <case> <parameter_catalog.json>\n";
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
