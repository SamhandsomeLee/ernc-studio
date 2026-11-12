#include "ernc/application/workflows/workflow_runner.hpp"
#include "ernc/domain/workflows/workflow_definition.hpp"
#include "ernc/domain/workflows/workflow_gate_context.hpp"
#include "ernc/domain/workflows/workflow_run_state.hpp"

#include <iostream>
#include <string_view>

namespace {

using ernc::application::WorkflowRunner;
using ernc::domain::WorkflowBlockReason;
using ernc::domain::WorkflowDefinition;
using ernc::domain::WorkflowGateContext;
using ernc::domain::WorkflowRunPhase;
using ernc::domain::WorkflowRunState;
using ernc::domain::WorkflowStepDefinition;
using ernc::domain::WorkflowStepPhase;

/// @brief 报告当前用例失败。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 合成两步工作流：先人工确认，再普通步。
WorkflowDefinition twoStepConfirmFirst() {
    WorkflowDefinition definition;
    definition.id = "synthetic.confirm_then_plain";
    definition.steps = {
        WorkflowStepDefinition{
            "confirm_me",
            "Confirm",
            "Needs operator",
            false,
            true,
            false,
            false,
        },
        WorkflowStepDefinition{
            "plain",
            "Plain",
            "No extra gates",
            false,
            false,
            false,
            false,
        },
    };
    return definition;
}

/// @brief 单步工作流：未确认公式门禁。
WorkflowDefinition formulaBlockedStep() {
    WorkflowDefinition definition;
    definition.id = "synthetic.formula";
    definition.steps = {WorkflowStepDefinition{
        "formula_step",
        "Formula",
        "Blocked until formula evidence",
        false,
        false,
        true,
        false,
    }};
    return definition;
}

/// @brief 单步工作流：需要设备连接读取。
WorkflowDefinition deviceReadStep() {
    WorkflowDefinition definition;
    definition.id = "synthetic.device_read";
    definition.steps = {WorkflowStepDefinition{
        "read_check",
        "Read",
        "Needs link",
        true,
        false,
        false,
        false,
    }};
    return definition;
}

/// @brief 未确认公式时 tryAdvance 保持 Blocked，不能 Completed。
bool unconfirmed_formula_blocks_advance() {
    const auto started = WorkflowRunner::start(formulaBlockedStep());
    if (!started.ok()) {
        return fail("unconfirmed_formula_blocks_advance");
    }
    WorkflowRunState state = started.value();
    WorkflowGateContext gates;
    gates.formulaConfirmed = false;
    gates.deviceConnected = true;
    const auto advanced = WorkflowRunner::tryAdvance(std::move(state), gates);
    if (!advanced.ok()) {
        return fail("unconfirmed_formula_blocks_advance");
    }
    state = advanced.value();
    if (state.runPhase != WorkflowRunPhase::Running
        || state.stepRuns[0].phase != WorkflowStepPhase::Blocked
        || state.stepRuns[0].blockReason != WorkflowBlockReason::UnconfirmedFormula) {
        return fail("unconfirmed_formula_blocks_advance");
    }
    const auto advancedAgain = WorkflowRunner::tryAdvance(std::move(state), gates);
    if (!advancedAgain.ok()) {
        return fail("unconfirmed_formula_blocks_advance");
    }
    state = advancedAgain.value();
    if (state.runPhase != WorkflowRunPhase::Running
        || state.stepRuns[0].phase != WorkflowStepPhase::Blocked
        || state.stepRuns[0].blockReason != WorkflowBlockReason::UnconfirmedFormula) {
        return fail("unconfirmed_formula_blocks_advance");
    }
    return true;
}

/// @brief 公式确认后 resume，再 advance 才能完成。
bool formula_confirmed_resume_then_complete() {
    const auto started = WorkflowRunner::start(formulaBlockedStep());
    if (!started.ok()) {
        return fail("formula_confirmed_resume_then_complete");
    }
    WorkflowRunState state = started.value();
    WorkflowGateContext blocked;
    blocked.formulaConfirmed = false;
    blocked.deviceConnected = true;
    state = WorkflowRunner::tryAdvance(std::move(state), blocked).value();
    WorkflowGateContext cleared = blocked;
    cleared.formulaConfirmed = false;
    state = WorkflowRunner::resume(std::move(state), cleared).value();
    if (state.stepRuns[0].phase != WorkflowStepPhase::Blocked) {
        return fail("formula_confirmed_resume_then_complete");
    }
    cleared.formulaConfirmed = true;
    state = WorkflowRunner::resume(std::move(state), cleared).value();
    if (state.stepRuns[0].phase != WorkflowStepPhase::Active) {
        return fail("formula_confirmed_resume_then_complete");
    }
    state = WorkflowRunner::tryAdvance(std::move(state), cleared).value();
    if (state.runPhase != WorkflowRunPhase::Completed) {
        return fail("formula_confirmed_resume_then_complete");
    }
    return true;
}

/// @brief 需要用户确认时，未 confirm 不能进入第二步。
bool user_confirm_required_before_second_step() {
    const auto started = WorkflowRunner::start(twoStepConfirmFirst());
    if (!started.ok()) {
        return fail("user_confirm_required_before_second_step");
    }
    WorkflowRunState state = started.value();
    WorkflowGateContext gates;
    gates.deviceConnected = true;
    state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    if (state.stepRuns[0].phase != WorkflowStepPhase::Blocked
        || state.stepRuns[0].blockReason != WorkflowBlockReason::UserConfirmationRequired) {
        return fail("user_confirm_required_before_second_step");
    }
    if (state.activeStepIndex != 0 || state.stepRuns[1].phase != WorkflowStepPhase::Pending) {
        return fail("user_confirm_required_before_second_step");
    }
    state = WorkflowRunner::confirmCurrentStep(std::move(state)).value();
    state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    if (state.stepRuns[0].phase != WorkflowStepPhase::Completed || state.activeStepIndex != 1
        || state.stepRuns[1].phase != WorkflowStepPhase::Active) {
        return fail("user_confirm_required_before_second_step");
    }
    state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    return state.runPhase == WorkflowRunPhase::Completed;
}

/// @brief 设备断连阻断读步骤，连接后 resume 再 advance。
bool device_disconnect_resume_cycle() {
    const auto started = WorkflowRunner::start(deviceReadStep());
    if (!started.ok()) {
        return fail("device_disconnect_resume_cycle");
    }
    WorkflowRunState state = started.value();
    WorkflowGateContext disconnected;
    disconnected.deviceConnected = false;
    state = WorkflowRunner::tryAdvance(std::move(state), disconnected).value();
    if (state.stepRuns[0].blockReason != WorkflowBlockReason::DeviceDisconnected) {
        return fail("device_disconnect_resume_cycle");
    }
    WorkflowGateContext connected = disconnected;
    connected.deviceConnected = true;
    state = WorkflowRunner::resume(std::move(state), connected).value();
    state = WorkflowRunner::tryAdvance(std::move(state), connected).value();
    return state.runPhase == WorkflowRunPhase::Completed;
}

/// @brief cancel 后不能 advance。
bool cancel_stops_running() {
    const auto started = WorkflowRunner::start(deviceReadStep());
    if (!started.ok()) {
        return fail("cancel_stops_running");
    }
    WorkflowRunState state = WorkflowRunner::cancel(started.value()).value();
    if (state.runPhase != WorkflowRunPhase::Cancelled) {
        return fail("cancel_stops_running");
    }
    WorkflowGateContext gates;
    gates.deviceConnected = true;
    const auto advanced = WorkflowRunner::tryAdvance(std::move(state), gates);
    return !advanced.ok();
}

/// @brief 空定义不能 start。
bool empty_definition_rejected() {
    WorkflowDefinition empty;
    empty.id = "empty";
    return !WorkflowRunner::start(std::move(empty)).ok();
}

bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "unconfirmed_formula_blocks_advance") {
        return unconfirmed_formula_blocks_advance();
    }
    if (name == "formula_confirmed_resume_then_complete") {
        return formula_confirmed_resume_then_complete();
    }
    if (name == "user_confirm_required_before_second_step") {
        return user_confirm_required_before_second_step();
    }
    if (name == "device_disconnect_resume_cycle") {
        return device_disconnect_resume_cycle();
    }
    if (name == "cancel_stops_running") {
        return cancel_stops_running();
    }
    if (name == "empty_definition_rejected") {
        return empty_definition_rejected();
    }
    recognized = false;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_workflow_tests <case>\n";
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
