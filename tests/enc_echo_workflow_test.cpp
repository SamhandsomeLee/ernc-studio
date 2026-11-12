#include "ernc/application/workflows/enc_echo.hpp"
#include "ernc/application/workflows/workflow_runner.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/types.hpp"
#include "ernc/domain/workflows/workflow_gate_context.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::application::EncEchoCompareStatus;
using ernc::application::EncEchoSnapshot;
using ernc::application::WorkflowRunner;
using ernc::application::buildEncEchoWorkflowDefinition;
using ernc::application::encEchoBlocksCheckTriggerWrite;
using ernc::application::encEchoForbiddenCheckTriggerIds;
using ernc::application::encEchoGainCheckFieldPath;
using ernc::application::materializeEncEchoView;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::RegionCatalog;
using ernc::domain::RegionDefinition;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::domain::SubRegion;
using ernc::domain::WorkflowGateContext;
using ernc::domain::WorkflowRunPhase;
using ernc::domain::WorkflowRunState;
using ernc::domain::WorkflowStepPhase;

bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

RuntimeTime at(int seconds) {
    return RuntimeTime{std::chrono::seconds{seconds}};
}

const SubRegion* findChild(const RegionDefinition& region, std::string_view path) {
    for (const SubRegion& child : region.subregions) {
        if (child.path == path) {
            return &child;
        }
    }
    return nullptr;
}

Result<RegionCatalog> openCatalog(const char* manifestPath, const char* name) {
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        fail(name);
        return Result<RegionCatalog>::failure(ErrorCode::InvalidParameter);
    }
    Result<RegionCatalog> built = RegionCatalog::load(loaded.layout());
    if (!built.ok()) {
        fail(name);
    }
    return built;
}

bool putFloat(std::vector<std::uint8_t>& bytes, std::size_t byteOffset, float value) {
    static_assert(sizeof(float) == 4U);
    const auto bits = __builtin_bit_cast(std::uint32_t, value);
    if (byteOffset + 4U > bytes.size()) {
        return false;
    }
    bytes[byteOffset] = static_cast<std::uint8_t>(bits & 0xFFU);
    bytes[byteOffset + 1U] = static_cast<std::uint8_t>((bits >> 8U) & 0xFFU);
    bytes[byteOffset + 2U] = static_cast<std::uint8_t>((bits >> 16U) & 0xFFU);
    bytes[byteOffset + 3U] = static_cast<std::uint8_t>((bits >> 24U) & 0xFFU);
    return true;
}

bool definition_has_six_steps_no_write() {
    const auto definition = buildEncEchoWorkflowDefinition();
    if (definition.id != "threshold.enc_echo" || definition.steps.size() != 6U) {
        return fail("definition_has_six_steps_no_write");
    }
    for (const auto& step : definition.steps) {
        if (step.permitsWrite) {
            return fail("definition_has_six_steps_no_write");
        }
    }
    if (definition.steps[4].requiresUserConfirm != true) {
        return fail("definition_has_six_steps_no_write");
    }
    return true;
}

bool forbidden_check_triggers_blocked() {
    for (const std::string_view id : encEchoForbiddenCheckTriggerIds()) {
        if (!encEchoBlocksCheckTriggerWrite(id)) {
            return fail("forbidden_check_triggers_blocked");
        }
    }
    if (encEchoBlocksCheckTriggerWrite("ENCEchoGainThreshold")) {
        return fail("forbidden_check_triggers_blocked");
    }
    return true;
}

bool materialize_decodes_echo_field(const char* manifestPath) {
    constexpr const char* name = "materialize_decodes_echo_field";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    const RegionDefinition* check = catalog.value().find("check_data");
    if (check == nullptr) {
        return fail(name);
    }
    const SubRegion* echo = findChild(*check, encEchoGainCheckFieldPath());
    if (echo == nullptr) {
        return fail(name);
    }
    std::vector<std::uint8_t> bytes(check->size, 0);
    const std::size_t relative =
        static_cast<std::size_t>(echo->offset) - static_cast<std::size_t>(check->offset);
    if (!putFloat(bytes, relative, 3.25F)) {
        return fail(name);
    }

    ParameterRuntimeStore store;
    const ParameterId control = ParameterId::parse("ENCEchoControlFlag").value();
    const ParameterId threshold = ParameterId::parse("ENCEchoGainThreshold").value();
    store.observe(control, ScalarValue::fromFloat(1.0F), at(1));
    store.observe(threshold, ScalarValue::fromFloat(0.5F), at(1));

    const auto started = WorkflowRunner::start(buildEncEchoWorkflowDefinition());
    if (!started.ok()) {
        return fail(name);
    }
    const Result<EncEchoSnapshot> snapshot =
        materializeEncEchoView(started.value(), store, *check, std::span<const std::uint8_t>{bytes});
    if (!snapshot.ok() || !snapshot.value().echoGainCheck.has_value()) {
        return fail(name);
    }
    const auto& field = snapshot.value().echoGainCheck.value();
    if (field.values.empty() || field.values.front() != 3.25F || field.shape.size() != 1U || field.shape.front() != 200U) {
        return fail(name);
    }
    if (snapshot.value().status != EncEchoCompareStatus::ReadyForOperatorReview) {
        return fail(name);
    }
    return true;
}

bool workflow_reaches_operator_review() {
    const auto started = WorkflowRunner::start(buildEncEchoWorkflowDefinition());
    if (!started.ok()) {
        return fail("workflow_reaches_operator_review");
    }
    WorkflowRunState state = started.value();
    WorkflowGateContext gates;
    gates.deviceConnected = true;
    for (int pass = 0; pass < 3; ++pass) {
        const auto advanced = WorkflowRunner::tryAdvance(std::move(state), gates);
        if (!advanced.ok()) {
            return fail("workflow_reaches_operator_review");
        }
        state = advanced.value();
    }
    if (state.activeStepIndex != 3U) {
        return fail("workflow_reaches_operator_review");
    }
    state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    if (state.activeStepIndex != 4U || state.stepRuns[4].phase != WorkflowStepPhase::Active) {
        return fail("workflow_reaches_operator_review");
    }
    WorkflowGateContext blocked = gates;
    state = WorkflowRunner::tryAdvance(std::move(state), blocked).value();
    if (state.stepRuns[4].phase != WorkflowStepPhase::Blocked) {
        return fail("workflow_reaches_operator_review");
    }
    state = WorkflowRunner::confirmCurrentStep(std::move(state)).value();
    state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    if (state.activeStepIndex != 5U || state.stepRuns[5].phase != WorkflowStepPhase::Active) {
        return fail("workflow_reaches_operator_review");
    }
    state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    if (state.runPhase != WorkflowRunPhase::Completed) {
        return fail("workflow_reaches_operator_review");
    }
    return true;
}

bool runCase(std::string_view name, const char* manifestPath, bool& recognized) {
    recognized = true;
    if (name == "definition_has_six_steps_no_write") {
        return definition_has_six_steps_no_write();
    }
    if (name == "forbidden_check_triggers_blocked") {
        return forbidden_check_triggers_blocked();
    }
    if (name == "materialize_decodes_echo_field") {
        return materialize_decodes_echo_field(manifestPath);
    }
    if (name == "workflow_reaches_operator_review") {
        return workflow_reaches_operator_review();
    }
    recognized = false;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_enc_echo_workflow_tests <case> <layout_manifest.json>\n";
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
