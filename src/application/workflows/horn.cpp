#include "ernc/application/workflows/horn.hpp"

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
using ernc::domain::SubRegion;
using ernc::domain::WorkflowDefinition;
using ernc::domain::WorkflowGateContext;
using ernc::domain::WorkflowStepDefinition;

constexpr std::string_view kHornControlId = "horn_control";
constexpr std::string_view kHornRefThresholdId = "HornRefThreshold";
constexpr std::string_view kRefChooseFloatId = "RefChooseFloat";
constexpr std::string_view kHornOffContinueTimeId = "HornOFFContinueTime";
constexpr std::string_view kHornTriggerId = "horn_trigger";

/// ERNC_IN_OUT 侧输入点位。R-12：不更换输入滤波器。证据等级 Needs Evidence，名单可扩。
constexpr std::string_view kInputFilterWriteIds[] = {
    "InputPoi",
    "input_Amplitude",
    "Refer_Num",
};

constexpr std::string_view kHornScalarWriteAllowlist[] = {
    "horn_control",
    "HornRefThreshold",
    "RefChooseFloat",
    "HornOFFContinueTime",
    "horn_trigger",
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

bool anyScalarStale(const ernc::domain::ParameterRuntimeStore& store, std::initializer_list<std::string_view> ids) {
    for (const std::string_view idText : ids) {
        const Result<ParameterId> parsed = ParameterId::parse(idText);
        if (!parsed.ok()) {
            return true;
        }
        const ParameterRuntimeState* state = store.find(parsed.value());
        if (state == nullptr || state->stale) {
            return true;
        }
    }
    return false;
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

Result<DecodedField> decodeFieldFromParent(const ernc::domain::RegionDefinition& checkData,
                                             std::span<const std::uint8_t> parentBytes, std::string_view fieldPath) {
    if (checkData.path != hornCheckDataRegionPath()) {
        return Result<DecodedField>::failure(ErrorCode::InvalidParameter);
    }
    if (parentBytes.size() != checkData.size) {
        return Result<DecodedField>::failure(ErrorCode::PayloadSizeMismatch);
    }
    const SubRegion* field = findChild(checkData, fieldPath);
    if (field == nullptr) {
        return Result<DecodedField>::failure(ErrorCode::InvalidParameter);
    }
    const std::size_t relative = static_cast<std::size_t>(field->offset) - static_cast<std::size_t>(checkData.offset);
    if (relative + field->size > parentBytes.size()) {
        return Result<DecodedField>::failure(ErrorCode::PayloadSizeMismatch);
    }
    return ernc::domain::decodeCheckDataField(*field, parentBytes.subspan(relative, static_cast<std::size_t>(field->size)));
}

}  // namespace

WorkflowGateContext HornWorkflowGates::toWorkflowGates() const noexcept {
    WorkflowGateContext merged = workflow;
    merged.formulaConfirmed = hornFilterActionConfirmed;
    return merged;
}

WorkflowDefinition buildHornWorkflowDefinition() {
    WorkflowDefinition definition;
    definition.id = "threshold.horn";
    definition.steps = {
        step("link_ready", "设备连接", "确认会话已连接。", true, false, false, false),
        step("read_check_data_silent", "读取不鸣笛 check_data", "FC03 读取父区，准备解 HornRefCheck_F。", true, false, false,
             false),
        step("decode_horn_ref_f_silent", "解码不鸣笛 F", "解出 250 点 HornRefCheck_F。", false, false, false, false),
        step("confirm_silent_captured", "确认不鸣笛数据", "操作员确认现场为不鸣笛工况。", false, true, false, false),
        step("read_check_data_horn", "读取鸣笛 check_data", "FC03 再次读取父区（鸣笛工况）。", true, false, false, false),
        step("decode_horn_ref_f_horn", "解码鸣笛 F", "解出第二份 250 点 HornRefCheck_F。", false, false, false, false),
        step("operator_judge_frequency", "人工判频", "对照两份 F；不自动写参数。", false, true, false, false),
        step(std::string{hornFilterSwitchStepId()}, "鸣笛滤波器步骤",
             "鸣笛滤波器动作未确认时保持 Blocked；不更换输入滤波器。", false, false, true, false),
        step("read_check_data_for_t", "读取 T", "切换鸣笛滤波器后 FC03 读 check_data。", true, false, false, false),
        step("decode_horn_ref_t", "解码 HornRefCheck_T", "解出 200 点时域检测。", false, false, false, false),
        step("present_horn_parameters", "人工对照 Horn 参数", "展示 horn_control、HornRefThreshold 等；本切片不自动写入。", false,
             true, false, false),
        step("done", "完成", "引导结束。", false, false, false, false),
    };
    return definition;
}

std::span<const std::string_view> hornForbiddenCheckTriggerIds() noexcept {
    return encEchoForbiddenCheckTriggerIds();
}

bool hornBlocksCheckTriggerWrite(std::string_view parameterId) noexcept {
    return encEchoBlocksCheckTriggerWrite(parameterId);
}

bool hornBlocksInputFilterWrite(std::string_view parameterId) noexcept {
    for (const std::string_view blocked : kInputFilterWriteIds) {
        if (parameterId == blocked) {
            return true;
        }
    }
    return false;
}

bool hornPermitsParameterWrite(std::string_view parameterId, bool hornFilterActionConfirmed,
                               bool allowHornScalarWrites) noexcept {
    if (hornBlocksInputFilterWrite(parameterId) || hornBlocksCheckTriggerWrite(parameterId)) {
        return false;
    }
    if (!hornFilterActionConfirmed) {
        return false;
    }
    if (!allowHornScalarWrites) {
        return false;
    }
    for (const std::string_view allowed : kHornScalarWriteAllowlist) {
        if (parameterId == allowed) {
            return true;
        }
    }
    return false;
}

Result<HornSnapshot> materializeHornView(ernc::domain::WorkflowRunState workflow,
                                         const ernc::domain::ParameterRuntimeStore& store,
                                         const ernc::domain::RegionDefinition& checkData,
                                         std::span<const std::uint8_t> checkDataBytesSilent,
                                         std::span<const std::uint8_t> checkDataBytesHorn,
                                         std::span<const std::uint8_t> checkDataBytesForT) {
    const Result<DecodedField> silentF = decodeFieldFromParent(checkData, checkDataBytesSilent, hornRefCheckFieldPath());
    if (!silentF.ok()) {
        return Result<HornSnapshot>::failure(silentF.tryError() == nullptr ? ErrorCode::Unknown : *silentF.tryError());
    }
    const Result<DecodedField> hornF = decodeFieldFromParent(checkData, checkDataBytesHorn, hornRefCheckFieldPath());
    if (!hornF.ok()) {
        return Result<HornSnapshot>::failure(hornF.tryError() == nullptr ? ErrorCode::Unknown : *hornF.tryError());
    }
    const Result<DecodedField> fieldT = decodeFieldFromParent(checkData, checkDataBytesForT, hornRefCheckTFieldPath());
    if (!fieldT.ok()) {
        return Result<HornSnapshot>::failure(fieldT.tryError() == nullptr ? ErrorCode::Unknown : *fieldT.tryError());
    }

    HornSnapshot snapshot;
    snapshot.workflow = std::move(workflow);
    snapshot.hornRefCheckF_silent = silentF.value();
    snapshot.hornRefCheckF_horn = hornF.value();
    snapshot.hornRefCheckT = fieldT.value();
    snapshot.hornControl = scalarFloatFromStore(store, kHornControlId);
    snapshot.hornRefThreshold = scalarFloatFromStore(store, kHornRefThresholdId);
    snapshot.refChooseFloat = scalarFloatFromStore(store, kRefChooseFloatId);
    snapshot.hornOffContinueTime = scalarFloatFromStore(store, kHornOffContinueTimeId);
    snapshot.hornTrigger = scalarFloatFromStore(store, kHornTriggerId);
    snapshot.scalarsStale = anyScalarStale(store, {kHornControlId, kHornRefThresholdId, kRefChooseFloatId, kHornOffContinueTimeId,
                                                   kHornTriggerId});

    const bool fieldsOk = snapshot.hornRefCheckF_silent.has_value() && snapshot.hornRefCheckF_horn.has_value()
                          && snapshot.hornRefCheckT.has_value();
    const bool scalarsOk = snapshot.hornControl.has_value() && snapshot.hornRefThreshold.has_value()
                           && snapshot.refChooseFloat.has_value() && snapshot.hornOffContinueTime.has_value()
                           && snapshot.hornTrigger.has_value();
    if (!fieldsOk) {
        snapshot.status = HornCompareStatus::Incomplete;
    } else if (snapshot.scalarsStale || !scalarsOk) {
        snapshot.status = HornCompareStatus::StaleScalars;
    } else {
        snapshot.status = HornCompareStatus::ReadyForOperatorReview;
    }
    return Result<HornSnapshot>::success(std::move(snapshot));
}

}  // namespace ernc::application
