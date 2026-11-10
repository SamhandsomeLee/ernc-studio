#include "ernc/domain/validation.hpp"

#include <cmath>

#if defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#error "ernc_domain must not include Qt"
#endif

namespace ernc::domain {
namespace {

/// @brief 判断标量种类是否等于目录里声明的数据类型。
/// @param declared 目录中的标量类型。调用方已经确认它存在。
/// @param value 用户标量，只在本次调用期间借用。
/// @return 种类一致时为 true。浮点是否有限不在这里判断。
bool kindMatches(DataType declared, const ScalarValue& value) {
    switch (declared) {
    case DataType::Float32:
        return value.kind() == ScalarValue::Kind::Float32;
    case DataType::Int32:
        return value.kind() == ScalarValue::Kind::Int32;
    case DataType::UInt32:
        return value.kind() == ScalarValue::Kind::UInt32;
    }
    return false;
}

}  // namespace

Result<void> validateStagedValue(const ParameterDefinition& definition, const ScalarValue& value) {
    if (!definition.access) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (*definition.access == AccessMode::ReadOnly) {
        return Result<void>::failure(ErrorCode::ReadOnlyViolation);
    }
    if (*definition.access == AccessMode::Command) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    // ReadWrite 与 WriteOnly 都是已声明的写入口。本阶段不读取上下限。
    if (!definition.dataType) {
        return Result<void>::success();
    }
    if (!kindMatches(*definition.dataType, value)) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (*definition.dataType == DataType::Float32) {
        const Result<float> number = value.asFloat();
        if (!number.ok() || !std::isfinite(number.value())) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
    }
    return Result<void>::success();
}

}  // namespace ernc::domain
