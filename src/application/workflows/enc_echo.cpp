#include "ernc/application/workflows/enc_echo.hpp"

#include "ernc/domain/check_data_codec.hpp"
#include "ernc/domain/types.hpp"

namespace ernc::application {

namespace {

using ernc::domain::DecodedField;
using ernc::domain::ErrorCode;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeState;
using ernc::domain::Result;
using ernc::domain::ScalarValue;
using ernc::domain::SubRegion;
using ernc::domain::WorkflowDefinition;
using ernc::domain::WorkflowStepDefinition;

constexpr std::string_view kControlFlagId = "ENCEchoControlFlag";
constexpr std::string_view kGainThresholdId = "ENCEchoGainThreshold";

constexpr std::string_view kForbiddenIds[] = {
    "CheckENCWFlag",
    "CheckOrder",
    "DataSpeedCheck",
};

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

WorkflowDefinition buildEncEchoWorkflowDefinition() {
    WorkflowDefinition definition;
    definition.id = "threshold.enc_echo";
    definition.steps = {
        step("link_ready", "设备连接", "确认会话已连接，未触发 Check 命令。", true, false, false, false),
        step("read_check_data", "读取 check_data", "仅 FC03 读取父区域 check_data，不写 CheckENCWFlag 或 CheckOrder。", true,
             false, false, false),
        step("decode_echo_gain", "解码 ENCEchoGainCheck", "从已读字节解出 200 点检测数据，不发起额外 Check。", false, false,
             false, false),
        step("load_threshold_state", "加载阈值与控制开关", "展示 ENCEchoGainThreshold 与 ENCEchoControlFlag 的 deviceValue。",
             true, false, false, false),
        step("operator_review", "人工对照", "操作员对照检测曲线与阈值；本步不自动写阈值。", false, true, false, false),
        step("done", "完成", "对照结束，不留未确认的自动写入。", false, false, false, false),
    };
    return definition;
}

std::span<const std::string_view> encEchoForbiddenCheckTriggerIds() noexcept {
    return kForbiddenIds;
}

bool encEchoBlocksCheckTriggerWrite(std::string_view parameterId) noexcept {
    for (const std::string_view forbidden : kForbiddenIds) {
        if (parameterId == forbidden) {
            return true;
        }
    }
    return false;
}

Result<EncEchoSnapshot> materializeEncEchoView(ernc::domain::WorkflowRunState workflow,
                                               const ernc::domain::ParameterRuntimeStore& store,
                                               const ernc::domain::RegionDefinition& checkData,
                                               std::span<const std::uint8_t> checkDataBytes) {
    if (checkData.path != encEchoCheckDataRegionPath()) {
        return Result<EncEchoSnapshot>::failure(ErrorCode::InvalidParameter);
    }
    if (checkDataBytes.size() != checkData.size) {
        return Result<EncEchoSnapshot>::failure(ErrorCode::PayloadSizeMismatch);
    }
    const SubRegion* echoField = findChild(checkData, encEchoGainCheckFieldPath());
    if (echoField == nullptr) {
        return Result<EncEchoSnapshot>::failure(ErrorCode::InvalidParameter);
    }
    if (echoField->offset < checkData.offset
        || echoField->offset + echoField->size > checkData.offset + checkData.size) {
        return Result<EncEchoSnapshot>::failure(ErrorCode::InvalidParameter);
    }
    const std::size_t relative =
        static_cast<std::size_t>(echoField->offset) - static_cast<std::size_t>(checkData.offset);
    if (relative + echoField->size > checkDataBytes.size()) {
        return Result<EncEchoSnapshot>::failure(ErrorCode::PayloadSizeMismatch);
    }
    const std::span<const std::uint8_t> slice =
        checkDataBytes.subspan(relative, static_cast<std::size_t>(echoField->size));
    const Result<DecodedField> decoded = ernc::domain::decodeCheckDataField(*echoField, slice);
    if (!decoded.ok()) {
        return Result<EncEchoSnapshot>::failure(
            decoded.tryError() == nullptr ? ErrorCode::Unknown : *decoded.tryError());
    }

    EncEchoSnapshot snapshot;
    snapshot.workflow = std::move(workflow);
    snapshot.controlFlag = scalarFloatFromStore(store, kControlFlagId);
    snapshot.gainThreshold = scalarFloatFromStore(store, kGainThresholdId);
    snapshot.controlFlagStale = scalarStaleFromStore(store, kControlFlagId);
    snapshot.thresholdStale = scalarStaleFromStore(store, kGainThresholdId);
    snapshot.echoGainCheck = decoded.value();

    if (!snapshot.echoGainCheck.has_value()) {
        snapshot.status = EncEchoCompareStatus::Incomplete;
    } else if (snapshot.controlFlagStale || snapshot.thresholdStale || !snapshot.controlFlag.has_value()
               || !snapshot.gainThreshold.has_value()) {
        snapshot.status = EncEchoCompareStatus::StaleScalars;
    } else {
        snapshot.status = EncEchoCompareStatus::ReadyForOperatorReview;
    }
    return Result<EncEchoSnapshot>::success(std::move(snapshot));
}

}  // namespace ernc::application
