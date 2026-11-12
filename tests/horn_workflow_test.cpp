#include "ernc/application/workflows/horn.hpp"
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

using ernc::application::HornCompareStatus;
using ernc::application::HornSnapshot;
using ernc::application::HornWorkflowGates;
using ernc::application::WorkflowRunner;
using ernc::application::buildHornWorkflowDefinition;
using ernc::application::hornBlocksCheckTriggerWrite;
using ernc::application::hornBlocksInputFilterWrite;
using ernc::application::hornFilterSwitchStepId;
using ernc::application::hornPermitsParameterWrite;
using ernc::application::hornRefCheckFieldPath;
using ernc::application::hornRefCheckTFieldPath;
using ernc::application::materializeHornView;
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

WorkflowRunState advance(WorkflowRunState state, const HornWorkflowGates& gates) {
    return WorkflowRunner::tryAdvance(std::move(state), gates.toWorkflowGates()).value();
}

bool definition_has_twelve_steps_filter_gate() {
    const auto definition = buildHornWorkflowDefinition();
    if (definition.id != "threshold.horn" || definition.steps.size() != 12U) {
        return fail("definition_has_twelve_steps_filter_gate");
    }
    for (const auto& step : definition.steps) {
        if (step.permitsWrite) {
            return fail("definition_has_twelve_steps_filter_gate");
        }
    }
    if (definition.steps[7].id != hornFilterSwitchStepId() || !definition.steps[7].requiresConfirmedFormula) {
        return fail("definition_has_twelve_steps_filter_gate");
    }
    return true;
}

bool input_filter_write_guard() {
    if (!hornBlocksInputFilterWrite("InputPoi") || !hornBlocksInputFilterWrite("input_Amplitude")) {
        return fail("input_filter_write_guard");
    }
    if (hornBlocksInputFilterWrite("HornRefThreshold")) {
        return fail("input_filter_write_guard");
    }
    if (hornPermitsParameterWrite("InputPoi", true, true)) {
        return fail("input_filter_write_guard");
    }
    if (!hornPermitsParameterWrite("HornRefThreshold", true, true)) {
        return fail("input_filter_write_guard");
    }
    if (hornPermitsParameterWrite("HornRefThreshold", false, true)) {
        return fail("input_filter_write_guard");
    }
    if (!hornBlocksCheckTriggerWrite("CheckOrder")) {
        return fail("input_filter_write_guard");
    }
    return true;
}

bool filter_unconfirmed_blocks_advance() {
    const auto started = WorkflowRunner::start(buildHornWorkflowDefinition());
    if (!started.ok()) {
        return fail("filter_unconfirmed_blocks_advance");
    }
    WorkflowRunState state = started.value();
    HornWorkflowGates gates;
    gates.workflow.deviceConnected = true;
    state = advance(std::move(state), gates);
    state = advance(std::move(state), gates);
    state = advance(std::move(state), gates);
    state = WorkflowRunner::confirmCurrentStep(std::move(state)).value();
    state = advance(std::move(state), gates);
    state = advance(std::move(state), gates);
    state = advance(std::move(state), gates);
    state = WorkflowRunner::confirmCurrentStep(std::move(state)).value();
    state = advance(std::move(state), gates);
    if (state.activeStepIndex != 7U || state.stepRuns[7].phase != WorkflowStepPhase::Active) {
        return fail("filter_unconfirmed_blocks_advance");
    }
    gates.hornFilterActionConfirmed = false;
    state = advance(std::move(state), gates);
    if (state.stepRuns[7].phase != WorkflowStepPhase::Blocked) {
        return fail("filter_unconfirmed_blocks_advance");
    }
    return true;
}

bool materialize_decodes_f_twice_and_t(const char* manifestPath) {
    constexpr const char* name = "materialize_decodes_f_twice_and_t";
    const Result<RegionCatalog> catalog = openCatalog(manifestPath, name);
    if (!catalog.ok()) {
        return false;
    }
    const RegionDefinition* check = catalog.value().find("check_data");
    const SubRegion* fieldF = check == nullptr ? nullptr : findChild(*check, hornRefCheckFieldPath());
    const SubRegion* fieldT = check == nullptr ? nullptr : findChild(*check, hornRefCheckTFieldPath());
    if (fieldF == nullptr || fieldT == nullptr) {
        return fail(name);
    }
    std::vector<std::uint8_t> silent(check->size, 0);
    std::vector<std::uint8_t> horn(check->size, 0);
    std::vector<std::uint8_t> afterT(check->size, 0);
    const std::size_t relF = static_cast<std::size_t>(fieldF->offset) - static_cast<std::size_t>(check->offset);
    const std::size_t relT = static_cast<std::size_t>(fieldT->offset) - static_cast<std::size_t>(check->offset);
    if (!putFloat(silent, relF, 1.0F) || !putFloat(horn, relF, 2.0F) || !putFloat(afterT, relT, 3.0F)) {
        return fail(name);
    }

    ParameterRuntimeStore store;
    store.observe(ParameterId::parse("horn_control").value(), ScalarValue::fromFloat(1.0F), at(1));
    store.observe(ParameterId::parse("HornRefThreshold").value(), ScalarValue::fromFloat(0.1F), at(1));
    store.observe(ParameterId::parse("RefChooseFloat").value(), ScalarValue::fromFloat(2.0F), at(1));
    store.observe(ParameterId::parse("HornOFFContinueTime").value(), ScalarValue::fromFloat(1100.0F), at(1));
    store.observe(ParameterId::parse("horn_trigger").value(), ScalarValue::fromFloat(0.0F), at(1));

    const auto started = WorkflowRunner::start(buildHornWorkflowDefinition());
    const Result<HornSnapshot> snapshot =
        materializeHornView(started.value(), store, *check, silent, horn, afterT);
    if (!snapshot.ok()) {
        return fail(name);
    }
    const HornSnapshot& value = snapshot.value();
    if (!value.hornRefCheckF_silent.has_value() || value.hornRefCheckF_silent->values.front() != 1.0F
        || value.hornRefCheckF_silent->shape != std::vector<std::uint32_t>{250U}) {
        return fail(name);
    }
    if (!value.hornRefCheckF_horn.has_value() || value.hornRefCheckF_horn->values.front() != 2.0F) {
        return fail(name);
    }
    if (!value.hornRefCheckT.has_value() || value.hornRefCheckT->values.front() != 3.0F
        || value.hornRefCheckT->shape != std::vector<std::uint32_t>{200U}) {
        return fail(name);
    }
    if (value.status != HornCompareStatus::ReadyForOperatorReview) {
        return fail(name);
    }
    return true;
}

bool workflow_completes_with_confirmations() {
    const auto started = WorkflowRunner::start(buildHornWorkflowDefinition());
    if (!started.ok()) {
        return fail("workflow_completes_with_confirmations");
    }
    WorkflowRunState state = started.value();
    HornWorkflowGates gates;
    gates.workflow.deviceConnected = true;
    state = advance(std::move(state), gates);
    state = advance(std::move(state), gates);
    state = advance(std::move(state), gates);
    if (state.activeStepIndex != 3U) {
        return fail("workflow_completes_with_confirmations");
    }
    state = WorkflowRunner::confirmCurrentStep(std::move(state)).value();
    state = advance(std::move(state), gates);
    state = advance(std::move(state), gates);
    state = advance(std::move(state), gates);
    if (state.activeStepIndex != 6U) {
        return fail("workflow_completes_with_confirmations");
    }
    state = WorkflowRunner::confirmCurrentStep(std::move(state)).value();
    state = advance(std::move(state), gates);
    if (state.activeStepIndex != 7U) {
        return fail("workflow_completes_with_confirmations");
    }
    gates.hornFilterActionConfirmed = true;
    state = advance(std::move(state), gates);
    state = advance(std::move(state), gates);
    state = advance(std::move(state), gates);
    if (state.activeStepIndex != 10U) {
        return fail("workflow_completes_with_confirmations");
    }
    state = WorkflowRunner::confirmCurrentStep(std::move(state)).value();
    state = advance(std::move(state), gates);
    if (state.activeStepIndex != 11U) {
        return fail("workflow_completes_with_confirmations");
    }
    state = advance(std::move(state), gates);
    return state.runPhase == WorkflowRunPhase::Completed;
}

bool runCase(std::string_view name, const char* manifestPath, bool& recognized) {
    recognized = true;
    if (name == "definition_has_twelve_steps_filter_gate") {
        return definition_has_twelve_steps_filter_gate();
    }
    if (name == "input_filter_write_guard") {
        return input_filter_write_guard();
    }
    if (name == "filter_unconfirmed_blocks_advance") {
        return filter_unconfirmed_blocks_advance();
    }
    if (name == "materialize_decodes_f_twice_and_t") {
        return materialize_decodes_f_twice_and_t(manifestPath);
    }
    if (name == "workflow_completes_with_confirmations") {
        return workflow_completes_with_confirmations();
    }
    recognized = false;
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_horn_workflow_tests <case> <layout_manifest.json>\n";
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
