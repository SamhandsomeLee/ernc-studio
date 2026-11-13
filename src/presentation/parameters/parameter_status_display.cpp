#include "parameters/parameter_status_display.hpp"

namespace ernc::presentation {

QString parameterStatusText(const ernc::domain::ParameterRuntimeState* state) {
    if (state == nullptr) {
        return QStringLiteral("未读取");
    }
    if (state->projectReadValue) {
        return QStringLiteral("工程历史读数");
    }
    if (state->stale && state->deviceValue.has_value()) {
        return QStringLiteral("数据已过期");
    }
    if (state->dirty) {
        return QStringLiteral("待写入");
    }
    if (state->deviceValue.has_value() && !state->stale) {
        return QStringLiteral("已读取");
    }
    return QStringLiteral("未读取");
}

} // namespace ernc::presentation
