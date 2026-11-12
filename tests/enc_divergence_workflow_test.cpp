#include "ernc/application/workflows/enc_divergence.hpp"
#include "ernc/application/workflows/workflow_runner.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/types.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::application::EncDivergenceCompareStatus;
using ernc::application::WorkflowRunner;
using ernc::application::buildEncDivergenceWorkflowDefinition;
using ernc::application::encDivergenceBlocksCheckTriggerWrite;
using ernc::application::encDivergenceExpectedAmpThreCount;
using ernc::application::encDivergenceForbiddenCheckTriggerIds;
using ernc::application::encDivergenceOrderCheckFieldPath;
using ernc::application::materializeEncDivergenceView;
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
    const auto definition = buildEncDivergenceWorkflowDefinition();
    if (definition.id != "threshold.enc_divergence" || definition.steps.size() != 6U) {
        return fail("definition_has_six_steps_no_write");
    }
    for (const auto& step : definition.steps) {
        if (step.permitsWrite) {
            return fail("definition_has_six_steps_no_write");
        }
    }
    if (!definition.steps[4].requiresUserConfirm) {
        return fail("definition_has_six_steps_no_write");
    }
    return true;
}

bool forbidden_check_triggers_blocked() {
    for (const std::string_view id : encDivergenceForbiddenCheckTriggerIds()) {
        if (!encDivergenceBlocksCheckTriggerWrite(id)) {
            return fail("forbidden_check_triggers_blocked");
        }
    }
    if (encDivergenceBlocksCheckTriggerWrite("SpkOrderAmpThre")) {
        return fail("forbidden_check_triggers_blocked");
    }
    return true;
}

bool materialize_decodes_five_floats(const char* manifestPath) {
    constexpr const char* name = "materialize_decodes_five_floats";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    const RegionDefinition* check = catalog.value().find("check_data");
    const SubRegion* order = check == nullptr ? nullptr : findChild(*check, encDivergenceOrderCheckFieldPath());
    if (order == nullptr) {
        return fail(name);
    }
    std::vector<std::uint8_t> bytes(check->size, 0);
    const std::size_t relative = static_cast<std::size_t>(order->offset) - static_cast<std::size_t>(check->offset);
    for (std::size_t index = 0; index < 5U; ++index) {
        if (!putFloat(bytes, relative + index * 4U, static_cast<float>(index) + 0.25F)) {
            return fail(name);
        }
    }

    ParameterRuntimeStore store;
    store.observe(ParameterId::parse("ENC_Divergence_Con_Flag").value(), ScalarValue::fromFloat(1.0F), at(1));
    const float ampThre[] = {10.0F, 20.0F, 30.0F};
    const auto started = WorkflowRunner::start(buildEncDivergenceWorkflowDefinition());
    if (!started.ok()) {
        return fail(name);
    }
    const auto snapshot = materializeEncDivergenceView(started.value(), store, *check,
                                                       std::span<const std::uint8_t>{bytes}, ampThre);
    if (!snapshot.ok()) {
        return fail(name);
    }
    const auto& value = snapshot.value();
    if (!value.orderSpkoutmaxCheck.has_value() || value.orderSpkoutmaxCheck->values.size() != 5U
        || value.orderSpkoutmaxCheck->shape != std::vector<std::uint32_t>{5U}) {
        return fail(name);
    }
    if (value.spkOrderAmpThreRaw.size() != encDivergenceExpectedAmpThreCount()) {
        return fail(name);
    }
    for (std::size_t index = 0; index < 3U; ++index) {
        if (value.spkOrderAmpThreRaw[index] != ampThre[index]) {
            return fail(name);
        }
    }
    if (value.status != EncDivergenceCompareStatus::ReadyForOperatorReview) {
        return fail(name);
    }
    return true;
}

bool rejects_wrong_amp_thre_length(const char* manifestPath) {
    constexpr const char* name = "rejects_wrong_amp_thre_length";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    const RegionDefinition* check = catalog.value().find("check_data");
    if (check == nullptr) {
        return fail(name);
    }
    std::vector<std::uint8_t> bytes(check->size, 0);
    const float twoOnly[] = {1.0F, 2.0F};
    ParameterRuntimeStore store;
    const auto started = WorkflowRunner::start(buildEncDivergenceWorkflowDefinition());
    if (!started.ok()) {
        return fail(name);
    }
    const auto materialized =
        materializeEncDivergenceView(started.value(), store, *check, std::span<const std::uint8_t>{bytes}, twoOnly);
    return !materialized.ok() && materialized.tryError() != nullptr
           && *materialized.tryError() == ErrorCode::InvalidParameter;
}

bool workflow_reaches_operator_review() {
    const auto started = WorkflowRunner::start(buildEncDivergenceWorkflowDefinition());
    if (!started.ok()) {
        return fail("workflow_reaches_operator_review");
    }
    WorkflowRunState state = started.value();
    WorkflowGateContext gates;
    gates.deviceConnected = true;
    for (int pass = 0; pass < 3; ++pass) {
        state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    }
    if (state.activeStepIndex != 3U) {
        return fail("workflow_reaches_operator_review");
    }
    state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    if (state.activeStepIndex != 4U) {
        return fail("workflow_reaches_operator_review");
    }
    state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    if (state.stepRuns[4].phase != WorkflowStepPhase::Blocked) {
        return fail("workflow_reaches_operator_review");
    }
    state = WorkflowRunner::confirmCurrentStep(std::move(state)).value();
    state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    if (state.activeStepIndex != 5U) {
        return fail("workflow_reaches_operator_review");
    }
    state = WorkflowRunner::tryAdvance(std::move(state), gates).value();
    return state.runPhase == WorkflowRunPhase::Completed;
}

bool runCase(std::string_view name, const char* manifestPath, bool& recognized) {
    recognized = true;
    if (name == "definition_has_six_steps_no_write") {
        return definition_has_six_steps_no_write();
    }
    if (name == "forbidden_check_triggers_blocked") {
        return forbidden_check_triggers_blocked();
    }
    if (name == "materialize_decodes_five_floats") {
        return materialize_decodes_five_floats(manifestPath);
    }
    if (name == "rejects_wrong_amp_thre_length") {
        return rejects_wrong_amp_thre_length(manifestPath);
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
        std::cerr << "usage: ernc_enc_divergence_workflow_tests <case> <layout_manifest.json>\n";
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
