#include "ernc/domain/runtime_store.hpp"

#include <chrono>
#include <iostream>
#include <string_view>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::RuntimeTime;
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

/// @brief 做成一个固定时刻。
/// @param seconds 距系统钟纪元的秒数。没有物理单位以外的换算。
/// @return 调用方可复制的时间点。
RuntimeTime at(int seconds) {
    return RuntimeTime{std::chrono::seconds{seconds}};
}

/// @brief 没有记录时不临时造一条成功状态。
/// @return find 为空且 size 为 0 时为 true。
bool missingIdHasNoState() {
    const ParameterRuntimeStore store;
    if (store.find(id("ANC_OFF")) != nullptr || store.size() != 0) {
        return fail("missing_id_has_no_state");
    }
    return true;
}

/// @brief 读回只替换 deviceValue，并清掉过期和错误。
/// @return stagedValue、dirty 和暂存时刻保持不变时为 true。
bool observeKeepsStaged() {
    ParameterRuntimeStore store;
    const auto parameter = id("ANC_OFF");
    store.stage(parameter, ScalarValue::fromInt32(1), at(10));
    store.failObservation(parameter, ErrorCode::ResponseTimeout);
    store.observe(parameter, ScalarValue::fromFloat(0.f), at(20));
    const auto* state = store.find(parameter);
    if (state == nullptr || !state->deviceValue || !state->stagedValue || !state->dirty || state->stale
        || state->error || !state->readAt || *state->readAt != at(20) || !state->stagedAt
        || *state->stagedAt != at(10)) {
        return fail("observe_keeps_staged");
    }
    if (*state->deviceValue != ScalarValue::fromFloat(0.f)
        || *state->stagedValue != ScalarValue::fromInt32(1)) {
        return fail("observe_keeps_staged");
    }
    return true;
}

/// @brief 用户暂存不把过期读数刷新成当前值。
/// @return deviceValue 和 stale 保持原样，dirty 变为 true 时为 true。
bool stageKeepsStaleDevice() {
    ParameterRuntimeStore store;
    const auto parameter = id("Step");
    store.observe(parameter, ScalarValue::fromFloat(1.5f), at(5));
    store.markOffline();
    store.stage(parameter, ScalarValue::fromInt32(2), at(8));
    const auto* state = store.find(parameter);
    if (state == nullptr || !state->deviceValue || *state->deviceValue != ScalarValue::fromFloat(1.5f)
        || !state->stagedValue || *state->stagedValue != ScalarValue::fromInt32(2) || !state->dirty
        || !state->stale || !state->readAt || *state->readAt != at(5) || !state->stagedAt
        || *state->stagedAt != at(8)) {
        return fail("stage_keeps_stale_device");
    }
    return true;
}

/// @brief 离线保留数值和 dirty，只把已有记录标成过期。
/// @return 两个值都还在、dirty 仍为 true、stale 为 true 时为 true。
bool offlineKeepsValues() {
    ParameterRuntimeStore store;
    const auto parameter = id("RNC_param.alpha1");
    store.observe(parameter, ScalarValue::fromFloat(3.f), at(1));
    store.stage(parameter, ScalarValue::fromFloat(4.f), at(2));
    store.markOffline();
    const auto* state = store.find(parameter);
    if (state == nullptr || !state->deviceValue || *state->deviceValue != ScalarValue::fromFloat(3.f)
        || !state->stagedValue || *state->stagedValue != ScalarValue::fromFloat(4.f) || !state->dirty
        || !state->stale) {
        return fail("offline_keeps_values");
    }
    if (store.find(id("ENC_OFF")) != nullptr) {
        return fail("offline_keeps_values");
    }
    return true;
}

/// @brief 观察失败只记下错误码。
/// @return 已有 deviceValue 不变，且 stale 不被失败改写时为 true。
bool failedObservationKeepsValues() {
    ParameterRuntimeStore store;
    const auto parameter = id("ANC_OFF");
    store.observe(parameter, ScalarValue::fromFloat(3.f), at(4));
    store.failObservation(parameter, ErrorCode::ResponseTimeout);
    const auto* state = store.find(parameter);
    if (state == nullptr || !state->deviceValue || *state->deviceValue != ScalarValue::fromFloat(3.f)
        || state->stagedValue || state->dirty || state->stale || !state->error
        || *state->error != ErrorCode::ResponseTimeout || !state->readAt || *state->readAt != at(4)) {
        return fail("failed_observation_keeps_values");
    }
    return true;
}

/// @brief 第一次失败还没有设备值。
/// @return 记录存在、两个值都空、stale 为 true 且错误码被记下时为 true。
bool failedObservationOnUnknown() {
    ParameterRuntimeStore store;
    const auto parameter = id("CAN_Msg");
    store.failObservation(parameter, ErrorCode::DeviceDisconnected);
    const auto* state = store.find(parameter);
    if (state == nullptr || state->deviceValue || state->stagedValue || state->dirty || !state->stale
        || state->readAt || state->stagedAt || !state->error
        || *state->error != ErrorCode::DeviceDisconnected) {
        return fail("failed_observation_on_unknown");
    }
    return true;
}

/// @brief 只有暂存时，记录从过期状态开始。
/// @return 没有 deviceValue、dirty 为 true 且 stale 为 true 时为 true。
bool stageStartsStale() {
    ParameterRuntimeStore store;
    const auto parameter = id("Step");
    store.stage(parameter, ScalarValue::fromUInt32(7u), at(9));
    const auto* state = store.find(parameter);
    if (state == nullptr || state->deviceValue || !state->stagedValue
        || *state->stagedValue != ScalarValue::fromUInt32(7u) || !state->dirty || !state->stale
        || state->readAt || !state->stagedAt || *state->stagedAt != at(9) || state->error) {
        return fail("stage_starts_stale");
    }
    return true;
}

/// @brief 数值同为 0 的浮点和整数仍然是两个值。
/// @return 读回的 Float32 没有替换暂存的 Int32 时为 true。
bool zeroKindsStayDistinct() {
    ParameterRuntimeStore store;
    const auto parameter = id("ANC_OFF");
    store.stage(parameter, ScalarValue::fromInt32(0), at(1));
    store.observe(parameter, ScalarValue::fromFloat(0.f), at(2));
    const auto* state = store.find(parameter);
    if (state == nullptr || !state->deviceValue || !state->stagedValue
        || *state->deviceValue == *state->stagedValue) {
        return fail("zero_kinds_stay_distinct");
    }
    return true;
}

/// @brief 空 Store 的离线不制造记录。
/// @return size 仍为 0 时为 true。
bool offlineOnEmpty() {
    ParameterRuntimeStore store;
    store.markOffline();
    if (store.size() != 0) {
        return fail("offline_on_empty");
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "missing_id_has_no_state") {
        return missingIdHasNoState();
    }
    if (name == "observe_keeps_staged") {
        return observeKeepsStaged();
    }
    if (name == "stage_keeps_stale_device") {
        return stageKeepsStaleDevice();
    }
    if (name == "offline_keeps_values") {
        return offlineKeepsValues();
    }
    if (name == "failed_observation_keeps_values") {
        return failedObservationKeepsValues();
    }
    if (name == "failed_observation_on_unknown") {
        return failedObservationOnUnknown();
    }
    if (name == "stage_starts_stale") {
        return stageStartsStale();
    }
    if (name == "zero_kinds_stay_distinct") {
        return zeroKindsStayDistinct();
    }
    if (name == "offline_on_empty") {
        return offlineOnEmpty();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个参数运行状态单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名，只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_runtime_store_tests <case>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
