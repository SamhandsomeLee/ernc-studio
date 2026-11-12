#include "ernc/application/workflows/enc_divergence.hpp"

#include "ernc/application/workflows/enc_echo.hpp"
#include "ernc/domain/check_data_codec.hpp"
#include "ernc/domain/types.hpp"

#include <utility>

namespace ernc::application {

namespace {

using ernc::domain::DecodedField;
using ernc::domain::ErrorCode;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeState;
using ernc::domain::Result;
using ernc::domain::SubRegion;
using ernc::domain::WorkflowDefinition;
using ernc::domain::WorkflowStepDefinition;

constexpr std::string_view kControlFlagId = "ENC_Divergence_Con_Flag";

const SubRegion* findChild(const ernc::domain::RegionDefinition& region, std::string_view path) {
    for (const SubRegion& child : region.subregions) {
        if (child.path == path) {
            return &child;
        }
    }
    return nullptr;
}

std::optional<float> scalarFloatFromStore(const ernc::domain::ParameterRuntimeStore& store, std::string_view idText) {
    const Result<ParameterId> parsed = ParameterId::parse(idText);
    if (!parsed.ok()) {
        return std::nullopt;
    }
    const ParameterRuntimeState* state = store.find(parsed.value());
    if (state == nullptr || !state->deviceValue.has_value()) {
        return std::nullopt;
    }
    const Result<float> asFloat = state->deviceValue->asFloat();
    if (!asFloat.ok()) {
        return std::nullopt;
    }
    return asFloat.value();
}

bool scalarStaleFromStore(const ernc::domain::ParameterRuntimeStore& store, std::string_view idText) {
    const Result<ParameterId> parsed = ParameterId::parse(idText);
    if (!parsed.ok()) {
        return true;
    }
    const ParameterRuntimeState* state = store.find(parsed.value());
    if (state == nullptr) {
        return true;
    }
    return state->stale;
}

WorkflowStepDefinition step(std::string id, std::string title, std::string description, bool device, bool confirm,
                            bool formula, bool write) {
    WorkflowStepDefinition definition;
    definition.id = std::move(id);
    definition.title = std::move(title);
    definition.description = std::move(description);
    definition.requiresDeviceRead = device;
    definition.requiresUserConfirm = confirm;
    definition.requiresConfirmedFormula = formula;
    definition.permitsWrite = write;
    return definition;
}

}  // namespace

WorkflowDefinition buildEncDivergenceWorkflowDefinition() {
    WorkflowDefinition definition;
    definition.id = "threshold.enc_divergence";
    definition.steps = {
        step("link_ready", "设备连接", "确认会话已连接，未触发 Check 命令。", true, false, false, false),
        step("read_check_data", "读取 check_data", "仅 FC03 读取父区域；不写 CheckENCWFlag 或 CheckOrder。", true, false,
             false, false),
        step("decode_order_spkoutmax", "解码 OrderSpkoutmaxCheck",
             "从已读字节解出 5 点发散特征值。Excel SpkOrderOutmaxCheck 与源码路径需独立核实。", false, false, false,
             false),
        step("load_divergence_state", "加载发散控制开关", "展示 ENC_Divergence_Con_Flag 的 deviceValue。", true, false,
             false, false),
        step("present_amp_thre_manual", "人工对照 SpkOrderAmpThre",
             "2/4 阶与数组下标未确认时只展示 OrderSpkoutmaxCheck 与 SpkOrderAmpThre 原始值，不自动设阈值。", false,
             true, false, false),
        step("done", "完成", "对照结束，不写入推导后的阶次阈值。", false, false, false, false),
    };
    return definition;
}

std::span<const std::string_view> encDivergenceForbiddenCheckTriggerIds() noexcept {
    return encEchoForbiddenCheckTriggerIds();
}

bool encDivergenceBlocksCheckTriggerWrite(std::string_view parameterId) noexcept {
    return encEchoBlocksCheckTriggerWrite(parameterId);
}

Result<EncDivergenceSnapshot> materializeEncDivergenceView(ernc::domain::WorkflowRunState workflow,
                                                           const ernc::domain::ParameterRuntimeStore& store,
                                                           const ernc::domain::RegionDefinition& checkData,
                                                           std::span<const std::uint8_t> checkDataBytes,
                                                           std::span<const float> spkOrderAmpThre) {
    if (!spkOrderAmpThre.empty() && spkOrderAmpThre.size() != encDivergenceExpectedAmpThreCount()) {
        return Result<EncDivergenceSnapshot>::failure(ErrorCode::InvalidParameter);
    }
    if (checkData.path != encDivergenceCheckDataRegionPath()) {
        return Result<EncDivergenceSnapshot>::failure(ErrorCode::InvalidParameter);
    }
    if (checkDataBytes.size() != checkData.size) {
        return Result<EncDivergenceSnapshot>::failure(ErrorCode::PayloadSizeMismatch);
    }
    const SubRegion* orderField = findChild(checkData, encDivergenceOrderCheckFieldPath());
    if (orderField == nullptr) {
        return Result<EncDivergenceSnapshot>::failure(ErrorCode::InvalidParameter);
    }
    if (orderField->offset < checkData.offset
        || orderField->offset + orderField->size > checkData.offset + checkData.size) {
        return Result<EncDivergenceSnapshot>::failure(ErrorCode::InvalidParameter);
    }
    const std::size_t relative =
        static_cast<std::size_t>(orderField->offset) - static_cast<std::size_t>(checkData.offset);
    if (relative + orderField->size > checkDataBytes.size()) {
        return Result<EncDivergenceSnapshot>::failure(ErrorCode::PayloadSizeMismatch);
    }
    const std::span<const std::uint8_t> slice =
        checkDataBytes.subspan(relative, static_cast<std::size_t>(orderField->size));
    const Result<DecodedField> decoded = ernc::domain::decodeCheckDataField(*orderField, slice);
    if (!decoded.ok()) {
        return Result<EncDivergenceSnapshot>::failure(
            decoded.tryError() == nullptr ? ErrorCode::Unknown : *decoded.tryError());
    }

    EncDivergenceSnapshot snapshot;
    snapshot.workflow = std::move(workflow);
    snapshot.divergenceControlFlag = scalarFloatFromStore(store, kControlFlagId);
    snapshot.controlFlagStale = scalarStaleFromStore(store, kControlFlagId);
    snapshot.orderSpkoutmaxCheck = decoded.value();
    if (!spkOrderAmpThre.empty()) {
        snapshot.spkOrderAmpThreRaw.assign(spkOrderAmpThre.begin(), spkOrderAmpThre.end());
        snapshot.ampThreProvided = true;
    }

    if (!snapshot.orderSpkoutmaxCheck.has_value()) {
        snapshot.status = EncDivergenceCompareStatus::Incomplete;
    } else if (snapshot.controlFlagStale || !snapshot.divergenceControlFlag.has_value() || !snapshot.ampThreProvided) {
        snapshot.status = EncDivergenceCompareStatus::StaleScalars;
    } else {
        snapshot.status = EncDivergenceCompareStatus::ReadyForOperatorReview;
    }
    return Result<EncDivergenceSnapshot>::success(std::move(snapshot));
}

}  // namespace ernc::application
