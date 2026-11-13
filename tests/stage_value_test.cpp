#include "ernc/application/stage_value.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"

#include <chrono>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

using ernc::application::ParameterStager;
using ernc::domain::CatalogLoad;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeState;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 解析参数 ID。
/// @param text 标识文本，只在本次调用期间借用。
/// @param name 用例名。
/// @param id 成功时写入解析结果。
/// @return 文本非空且解析成功时为 true。
bool parseId(std::string_view text, const char* name, ParameterId& id) {
    const Result<ParameterId> parsed = ParameterId::parse(text);
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail(name);
    }
    id = *parsed.tryValue();
    return true;
}

/// @brief 确认失败结果的错误码，并且 Store 没有新增记录。
/// @param result 暂存结果。
/// @param expected 期望的错误码。
/// @param store 运行状态。
/// @param name 用例名。
/// @return 错误码匹配且记录数为 0 时为 true。
bool rejected(const Result<void>& result, ErrorCode expected, const ParameterRuntimeStore& store, const char* name) {
    if (result.ok() || result.tryError() == nullptr || *result.tryError() != expected || store.size() != 0U) {
        return fail(name);
    }
    return true;
}

/// @brief ANC_OFF 写入暂存值，同时保留已经读到的设备值和观察错误。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return stagedValue 为 2、deviceValue 为 1、dirty 为 true 时为 true。
bool ancOffStagesScalar(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterStager stager{catalog, layout, store};
    const Result<ParameterId> parsed = ParameterId::parse("ANC_OFF");
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail("anc_off_stages_scalar");
    }
    const ParameterId& id = *parsed.tryValue();
    const RuntimeTime readAt{std::chrono::seconds{10}};
    const RuntimeTime stagedAt{std::chrono::seconds{11}};
    store.observe(id, ScalarValue::fromFloat(1.0F), readAt);
    store.failObservation(id, ErrorCode::ResponseTimeout);
    const Result<void> staged = stager.stage(id, ScalarValue::fromFloat(2.0F), stagedAt);
    const ParameterRuntimeState* state = store.find(id);
    if (!staged.ok() || state == nullptr || !state->deviceValue || !state->stagedValue || !state->dirty || state->stale
        || state->readAt != readAt || state->stagedAt != stagedAt || state->error != ErrorCode::ResponseTimeout) {
        return fail("anc_off_stages_scalar");
    }
    const Result<float> device = state->deviceValue->asFloat();
    const Result<float> stagedValue = state->stagedValue->asFloat();
    if (!device.ok() || !stagedValue.ok() || device.value() != 1.0F || stagedValue.value() != 2.0F) {
        return fail("anc_off_stages_scalar");
    }
    return true;
}

/// @brief Step 可暂存浮点。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return Store 里出现 stagedValue 时为 true。
bool stepCanStage(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterStager stager{catalog, layout, store};
    ParameterId id = ParameterId::parse("ANC_OFF").value();
    if (!parseId("Step", "step_can_stage", id)) {
        return false;
    }
    const RuntimeTime stagedAt{std::chrono::seconds{1}};
    if (!stager.stage(id, ScalarValue::fromFloat(1.0F), stagedAt).ok()) {
        return fail("step_can_stage");
    }
    const ParameterRuntimeState* state = store.find(id);
    if (state == nullptr || !state->dirty || !state->stagedValue) {
        return fail("step_can_stage");
    }
    return true;
}

/// @brief RNC_refresh 可暂存 int32。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return Store 里出现 stagedValue 时为 true。
bool refreshCanStage(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterStager stager{catalog, layout, store};
    ParameterId id = ParameterId::parse("ANC_OFF").value();
    if (!parseId("RNC_refresh", "refresh_can_stage", id)) {
        return false;
    }
    const RuntimeTime stagedAt{std::chrono::seconds{1}};
    if (!stager.stage(id, ScalarValue::fromInt32(1), stagedAt).ok()) {
        return fail("refresh_can_stage");
    }
    const ParameterRuntimeState* state = store.find(id);
    if (state == nullptr || !state->dirty || !state->stagedValue) {
        return fail("refresh_can_stage");
    }
    return true;
}

/// @brief 非有限浮点不能暂存。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return NaN 被拒绝且 Store 仍为空时为 true。
bool nanIsRejected(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterStager stager{catalog, layout, store};
    ParameterId id = ParameterId::parse("ANC_OFF").value();
    if (!parseId("ANC_OFF", "nan_is_rejected", id)) {
        return false;
    }
    return rejected(stager.stage(id, ScalarValue::fromFloat(std::numeric_limits<float>::quiet_NaN()),
                                 RuntimeTime{std::chrono::seconds{1}}),
                    ErrorCode::InvalidParameter, store, "nan_is_rejected");
}

/// @brief 标量种类和目录声明不一致时拒绝。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 把 int32 暂存到 float 参数时 Store 仍为空。
bool typeMismatchIsRejected(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterStager stager{catalog, layout, store};
    ParameterId id = ParameterId::parse("ANC_OFF").value();
    if (!parseId("ANC_OFF", "type_mismatch_is_rejected", id)) {
        return false;
    }
    return rejected(stager.stage(id, ScalarValue::fromInt32(1), RuntimeTime{std::chrono::seconds{1}}),
                    ErrorCode::InvalidParameter, store, "type_mismatch_is_rejected");
}

/// @brief Can_Msg 有 72 个 slot，不能放进一个暂存标量。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 错误码为 InvalidParameter 且 Store 仍为空时为 true。
bool canMsgIsRejected(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterStager stager{catalog, layout, store};
    ParameterId id = ParameterId::parse("ANC_OFF").value();
    if (!parseId("Can_Msg", "can_msg_is_rejected", id)) {
        return false;
    }
    return rejected(stager.stage(id, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{1}}),
                    ErrorCode::InvalidParameter, store, "can_msg_is_rejected");
}

/// @brief check_data 是结构体整段，不能暂存成一个标量。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 错误码为 InvalidParameter 且 Store 仍为空时为 true。
bool checkDataIsRejected(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterStager stager{catalog, layout, store};
    ParameterId id = ParameterId::parse("ANC_OFF").value();
    if (!parseId("check_data", "check_data_is_rejected", id)) {
        return false;
    }
    return rejected(stager.stage(id, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{1}}),
                    ErrorCode::InvalidParameter, store, "check_data_is_rejected");
}

/// @brief 目录里没有的 ID 不新建记录。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 错误码为 InvalidParameter 且 Store 仍为空时为 true。
bool unknownIdIsRejected(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterStager stager{catalog, layout, store};
    ParameterId id = ParameterId::parse("ANC_OFF").value();
    if (!parseId("missing-parameter", "unknown_id_is_rejected", id)) {
        return false;
    }
    return rejected(stager.stage(id, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{1}}),
                    ErrorCode::InvalidParameter, store, "unknown_id_is_rejected");
}

/// @brief 后续非法暂存不覆盖已经暂存的目标值。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return NaN 被拒绝后 stagedValue 仍是 2 时为 true。
bool failedStageKeepsPrevious(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterStager stager{catalog, layout, store};
    const Result<ParameterId> parsed = ParameterId::parse("ANC_OFF");
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail("failed_stage_keeps_previous");
    }
    const ParameterId& id = *parsed.tryValue();
    const RuntimeTime stagedAt{std::chrono::seconds{4}};
    if (!stager.stage(id, ScalarValue::fromFloat(2.0F), stagedAt).ok()) {
        return fail("failed_stage_keeps_previous");
    }
    const Result<void> rejectedValue =
        stager.stage(id, ScalarValue::fromFloat(std::numeric_limits<float>::quiet_NaN()), RuntimeTime{std::chrono::seconds{5}});
    const ParameterRuntimeState* state = store.find(id);
    if (rejectedValue.ok() || rejectedValue.tryError() == nullptr || *rejectedValue.tryError() != ErrorCode::InvalidParameter
        || state == nullptr || !state->stagedValue || !state->dirty || state->stagedAt != stagedAt || store.size() != 1U) {
        return fail("failed_stage_keeps_previous");
    }
    const Result<float> number = state->stagedValue->asFloat();
    if (!number.ok() || number.value() != 2.0F) {
        return fail("failed_stage_keeps_previous");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const ParameterCatalog& catalog, const FirmwareLayout& layout, bool& recognized) {
    recognized = true;
    if (name == "anc_off_stages_scalar") {
        return ancOffStagesScalar(catalog, layout);
    }
    if (name == "step_can_stage") {
        return stepCanStage(catalog, layout);
    }
    if (name == "refresh_can_stage") {
        return refreshCanStage(catalog, layout);
    }
    if (name == "nan_is_rejected") {
        return nanIsRejected(catalog, layout);
    }
    if (name == "type_mismatch_is_rejected") {
        return typeMismatchIsRejected(catalog, layout);
    }
    if (name == "can_msg_is_rejected") {
        return canMsgIsRejected(catalog, layout);
    }
    if (name == "check_data_is_rejected") {
        return checkDataIsRejected(catalog, layout);
    }
    if (name == "unknown_id_is_rejected") {
        return unknownIdIsRejected(catalog, layout);
    }
    if (name == "failed_stage_keeps_previous") {
        return failedStageKeepsPrevious(catalog, layout);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个暂存用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是 layout manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_stage_value_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    const CatalogLoad catalog = ParameterCatalogLoader::loadFile(argv[2]);
    const LayoutLoad layout = FirmwareLayoutLoader::loadFile(argv[3]);
    if (!catalog.ok() || !layout.ok()) {
        std::cerr << "failed: load\n";
        return 1;
    }
    bool recognized = false;
    if (!runCase(argv[1], catalog.catalog(), layout.layout(), recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
