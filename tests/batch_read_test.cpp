#include "ernc/application/batch_read.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/chunk_plan.hpp"
#include "ernc/domain/firmware_layout.hpp"

#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using ernc::application::BatchFramePlan;
using ernc::application::BatchReadScope;
using ernc::application::BatchReadScopeKind;
using ernc::application::BatchReadTicket;
using ernc::application::BatchSkipReason;
using ernc::application::CommandId;
using ernc::application::CommandOutcome;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::ParameterBatchReader;
using ernc::domain::CatalogLoad;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::kMaxReadChunkSlots;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;
using ernc::simulator::MemorySlaveTransport;
using ernc::simulator::SlaveImage;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 收下请求但不产生响应，用来查看已经排好的帧。
class SilentTransport final : public IDeviceTransport {
public:
    /// @brief 打开端口。
    /// @return 成功。成功不是设备响应。
    Result<void> open() override {
        open_ = true;
        return Result<void>::success();
    }

    /// @brief 关闭端口。
    /// @return 成功。
    Result<void> close() override {
        open_ = false;
        return Result<void>::success();
    }

    /// @brief 记下整帧，不排队响应。
    /// @param bytes 带长度前缀的请求，只在本次调用期间借用。
    /// @return 已打开且非空时成功。
    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        if (!open_ || bytes.empty()) {
            return Result<void>::failure(open_ ? ErrorCode::InvalidParameter : ErrorCode::DeviceDisconnected);
        }
        submitted_.emplace_back(bytes.begin(), bytes.end());
        return Result<void>::success();
    }

    /// @brief 始终没有响应。
    /// @return Idle。
    TransportStep poll() override { return TransportStep{}; }

private:
    /// 当前是否打开。
    bool open_{false};

    /// 已提交的帧。本用例不读取它们。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// @brief 记录请求并交给内存从站。
class RecordingTransport final : public IDeviceTransport {
public:
    /// @brief 绑定从站内存。
    /// @param image 从站内存，只借用。调用方保证它在本对象存活期间有效。
    explicit RecordingTransport(SlaveImage& image) : inner_{image} {}

    /// @brief 打开内部端口。
    /// @return 内部端口的打开结果。
    Result<void> open() override { return inner_.open(); }

    /// @brief 关闭内部端口。
    /// @return 内部端口的关闭结果。
    Result<void> close() override { return inner_.close(); }

    /// @brief 记下整帧后再交给从站。
    /// @param bytes 带长度前缀的请求，只在本次调用期间借用。
    /// @return 内部端口的提交结果。
    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        submitted_.emplace_back(bytes.begin(), bytes.end());
        return inner_.submit(bytes);
    }

    /// @brief 取走从站的下一步。
    /// @return 内部端口的步骤。
    TransportStep poll() override { return inner_.poll(); }

    /// @brief 按提交顺序借出已记录的请求。
    /// @return 内部向量的常量引用。
    const std::vector<std::vector<std::uint8_t>>& submitted() const noexcept { return submitted_; }

private:
    /// 实际回答 FC03 的从站端口。
    MemorySlaveTransport inner_;

    /// 调用方提交过的整帧副本。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// @brief 解析一个参数 ID。
/// @param text 标识文本。
/// @param name 用例名。
/// @param id 成功时写入解析结果。
/// @return 解析成功时为 true。
bool parseId(const char* text, const char* name, ParameterId& id) {
    const Result<ParameterId> parsed = ParameterId::parse(text);
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail(name);
    }
    id = *parsed.tryValue();
    return true;
}

/// @brief 判断一帧的参数列表里有没有这个 ID。
/// @param frame 帧计划。
/// @param id 参数标识。
/// @return 找到时为 true。
bool frameHas(const BatchFramePlan& frame, const ParameterId& id) {
    for (const ParameterId& parameter : frame.parameters) {
        if (parameter == id) {
            return true;
        }
    }
    return false;
}

/// @brief 确认一帧的功能码和寄存器头。
/// @param frame 带长度前缀的请求。
/// @param firstSlot 期望的起始逻辑 slot。
/// @param slotCount 期望的 slot 数。
/// @return 功能码是 FC03，且起始寄存器、寄存器数量与 slot 一致时为 true。
bool readHeader(const std::vector<std::uint8_t>& frame, std::uint16_t firstSlot, std::uint16_t slotCount) {
    const std::uint16_t start = static_cast<std::uint16_t>(firstSlot * 2U);
    const std::uint16_t count = static_cast<std::uint16_t>(slotCount * 2U);
    return frame.size() >= 7U && frame[1] == 0x01 && frame[2] == 0x03
           && frame[3] == static_cast<std::uint8_t>((start >> 8U) & 0xFFU)
           && frame[4] == static_cast<std::uint8_t>(start & 0xFFU)
           && frame[5] == static_cast<std::uint8_t>((count >> 8U) & 0xFFU)
           && frame[6] == static_cast<std::uint8_t>(count & 0xFFU);
}

/// @brief 把低字节在前的 32 位模式写成 4 字节。
/// @param bits 位模式。不调用协议编码器。
/// @param out 至少 4 字节的输出。下标 0 接收最低字节。
void storeLittleEndian(std::uint32_t bits, std::uint8_t* out) {
    out[0] = static_cast<std::uint8_t>(bits & 0xFFU);
    out[1] = static_cast<std::uint8_t>((bits >> 8U) & 0xFFU);
    out[2] = static_cast<std::uint8_t>((bits >> 16U) & 0xFFU);
    out[3] = static_cast<std::uint8_t>((bits >> 24U) & 0xFFU);
}

/// @brief 取出 float 的位模式。
/// @param value 标量。
/// @param bits 成功时写入位模式。
/// @return 种类是 Float32 时为 true。
bool floatBits(const ernc::domain::ScalarValue& value, std::uint32_t& bits) {
    const Result<float> decoded = value.asFloat();
    if (!decoded.ok() || decoded.tryValue() == nullptr) {
        return false;
    }
    bits = std::bit_cast<std::uint32_t>(*decoded.tryValue());
    return true;
}

/// @brief 泵调度器直到这批读结束。
/// @param reader 批量读对象。
/// @param scheduler 与 reader 使用同一条端口的调度器。
/// @param ticket 这批读的命令号。
/// @param readAt 标量写入 Store 的时刻。
/// @param name 用例名。
/// @return 不再忙碌时为 true。
bool drive(ParameterBatchReader& reader, CommandScheduler& scheduler, const BatchReadTicket& ticket, RuntimeTime readAt,
           const char* name) {
    for (int turn = 0; turn < 8 && reader.busy(); ++turn) {
        for (const CommandId id : ticket.commands()) {
            if (std::optional<CommandOutcome> outcome = scheduler.takeResult(id)) {
                reader.apply(*outcome, readAt);
            }
        }
        scheduler.pump();
    }
    for (const CommandId id : ticket.commands()) {
        if (std::optional<CommandOutcome> outcome = scheduler.takeResult(id)) {
            reader.apply(*outcome, readAt);
        }
    }
    if (reader.busy()) {
        return fail(name);
    }
    return true;
}

/// @brief All 的第一帧从 slot 0 读满 62 个 slot，并且包含 ANC_OFF。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 第一帧满长，且没有任何一帧超过 62 个 slot 时为 true。
bool allFirstFrameIsFull(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    SilentTransport transport;
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    ParameterRuntimeStore store;
    ParameterBatchReader reader{catalog, layout, store, session, scheduler};
    if (!session.connect().ok()) {
        return fail("all_first_frame_is_full");
    }
    const Result<ParameterId> anc = ParameterId::parse("ANC_OFF");
    if (!anc.ok() || anc.tryValue() == nullptr) {
        return fail("all_first_frame_is_full");
    }
    const Result<BatchReadTicket> started = reader.start(BatchReadScope{BatchReadScopeKind::All, {}, {}});
    if (!started.ok() || started.tryValue() == nullptr || started.value().frames().empty()) {
        return fail("all_first_frame_is_full");
    }
    const BatchFramePlan& first = started.value().frames().front();
    if (first.firstSlot != 0 || first.slotCount != kMaxReadChunkSlots || !frameHas(first, *anc.tryValue())) {
        std::cerr << "first frame " << first.firstSlot << " x " << first.slotCount << '\n';
        return fail("all_first_frame_is_full");
    }
    for (const BatchFramePlan& frame : started.value().frames()) {
        if (frame.slotCount == 0 || frame.slotCount > kMaxReadChunkSlots) {
            return fail("all_first_frame_is_full");
        }
    }
    return true;
}

/// @brief ERNC_Flag 开头三个连续开关合成一帧。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return slot 0 的那一帧长度为 3，并且含 ANC_OFF、ENC_OFF、RNC_OFF 时为 true。
bool flagSwitchesShareOneFrame(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    SilentTransport transport;
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    ParameterRuntimeStore store;
    ParameterBatchReader reader{catalog, layout, store, session, scheduler};
    if (!session.connect().ok()) {
        return fail("flag_switches_share_one_frame");
    }
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    ParameterId enc = ParameterId::parse("ENC_OFF").value();
    ParameterId rnc = ParameterId::parse("RNC_OFF").value();
    if (!parseId("ANC_OFF", "flag_switches_share_one_frame", anc)
        || !parseId("ENC_OFF", "flag_switches_share_one_frame", enc)
        || !parseId("RNC_OFF", "flag_switches_share_one_frame", rnc)) {
        return false;
    }
    BatchReadScope scope;
    scope.kind = BatchReadScopeKind::Category;
    scope.category = "ERNC_Flag";
    const Result<BatchReadTicket> started = reader.start(scope);
    if (!started.ok() || started.tryValue() == nullptr) {
        return fail("flag_switches_share_one_frame");
    }
    const BatchFramePlan* head = nullptr;
    for (const BatchFramePlan& frame : started.value().frames()) {
        if (frame.firstSlot == 0) {
            head = &frame;
            break;
        }
    }
    if (head == nullptr || head->slotCount != 3 || !frameHas(*head, anc) || !frameHas(*head, enc) || !frameHas(*head, rnc)) {
        return fail("flag_switches_share_one_frame");
    }
    return true;
}

/// @brief 两个字段中间有空洞时，不把空洞读进同一帧。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return ANC_OFF 和 RNC_refresh 各占一帧，没有长度为 5 的帧时为 true。
bool gapIsNotFilled(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    SilentTransport transport;
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    ParameterRuntimeStore store;
    ParameterBatchReader reader{catalog, layout, store, session, scheduler};
    if (!session.connect().ok()) {
        return fail("gap_is_not_filled");
    }
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    ParameterId refresh = ParameterId::parse("RNC_refresh").value();
    if (!parseId("ANC_OFF", "gap_is_not_filled", anc) || !parseId("RNC_refresh", "gap_is_not_filled", refresh)) {
        return false;
    }
    BatchReadScope scope;
    scope.kind = BatchReadScopeKind::Selected;
    scope.selected.push_back(refresh);
    scope.selected.push_back(anc);
    const Result<BatchReadTicket> started = reader.start(scope);
    if (!started.ok() || started.tryValue() == nullptr || started.value().frames().size() != 2U) {
        return fail("gap_is_not_filled");
    }
    const BatchFramePlan& first = started.value().frames()[0];
    const BatchFramePlan& second = started.value().frames()[1];
    if (first.firstSlot != 0 || first.slotCount != 1 || second.firstSlot != 4 || second.slotCount != 1) {
        return fail("gap_is_not_filled");
    }
    return true;
}

/// @brief 先点 ENC_OFF 再点 ANC_OFF，线上仍从 slot 0 一次读回两个值。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 只有一帧，ANC_OFF 的位模式是 0x3f800000，ENC_OFF 的位模式是 0x40000000 时为 true。
bool selectedPairUpdatesValues(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    std::vector<std::uint8_t> bytes(8U, 0);
    storeLittleEndian(0x3f800000U, bytes.data());
    storeLittleEndian(0x40000000U, bytes.data() + 4);
    SlaveImage image{std::move(bytes)};
    RecordingTransport transport{image};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    ParameterRuntimeStore store;
    ParameterBatchReader reader{catalog, layout, store, session, scheduler};
    if (!session.connect().ok()) {
        return fail("selected_pair_updates_values");
    }
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    ParameterId enc = ParameterId::parse("ENC_OFF").value();
    if (!parseId("ANC_OFF", "selected_pair_updates_values", anc)
        || !parseId("ENC_OFF", "selected_pair_updates_values", enc)) {
        return false;
    }
    BatchReadScope scope;
    scope.kind = BatchReadScopeKind::Selected;
    scope.selected.push_back(enc);
    scope.selected.push_back(anc);
    const Result<BatchReadTicket> started = reader.start(scope);
    if (!started.ok() || started.tryValue() == nullptr || started.value().frames().size() != 1U) {
        return fail("selected_pair_updates_values");
    }
    const BatchFramePlan& frame = started.value().frames().front();
    if (frame.firstSlot != 0 || frame.slotCount != 2 || frame.parameters.size() != 2U || frame.parameters[0] != anc
        || frame.parameters[1] != enc) {
        return fail("selected_pair_updates_values");
    }
    if (!drive(reader, scheduler, started.value(), RuntimeTime{std::chrono::seconds{5}}, "selected_pair_updates_values")) {
        return false;
    }
    const ernc::domain::ParameterRuntimeState* ancState = store.find(anc);
    const ernc::domain::ParameterRuntimeState* encState = store.find(enc);
    std::uint32_t ancBits = 0;
    std::uint32_t encBits = 0;
    if (ancState == nullptr || encState == nullptr || !ancState->deviceValue || !encState->deviceValue
        || !floatBits(*ancState->deviceValue, ancBits) || !floatBits(*encState->deviceValue, encBits)
        || ancBits != 0x3f800000U || encBits != 0x40000000U || transport.submitted().size() != 1U
        || !readHeader(transport.submitted()[0], 0, 2)) {
        return fail("selected_pair_updates_values");
    }
    return true;
}

/// @brief 单独读取 Can_Msg 时用 62 加 10 两帧，不拆成 72 条命令。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 两帧覆盖 72 个 slot，结果有 72 个 float，且 deviceValue 仍为空时为 true。
bool canMsgUsesFullFrames(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    constexpr std::uint16_t kCanSlot = 14869;
    constexpr std::uint16_t kCanCount = 72;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kCanSlot + kCanCount) * 4U, 0);
    for (std::uint16_t index = 0; index < kCanCount; ++index) {
        storeLittleEndian(static_cast<std::uint32_t>(index) + 1U,
                          bytes.data() + (static_cast<std::size_t>(kCanSlot + index) * 4U));
    }
    SlaveImage image{std::move(bytes)};
    RecordingTransport transport{image};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    ParameterRuntimeStore store;
    ParameterBatchReader reader{catalog, layout, store, session, scheduler};
    if (!session.connect().ok()) {
        return fail("can_msg_uses_full_frames");
    }
    ParameterId can = ParameterId::parse("Can_Msg").value();
    if (!parseId("Can_Msg", "can_msg_uses_full_frames", can)) {
        return false;
    }
    BatchReadScope scope;
    scope.kind = BatchReadScopeKind::Selected;
    scope.selected.push_back(can);
    const Result<BatchReadTicket> started = reader.start(scope);
    if (!started.ok() || started.tryValue() == nullptr || started.value().frames().size() != 2U) {
        return fail("can_msg_uses_full_frames");
    }
    const BatchFramePlan& head = started.value().frames()[0];
    const BatchFramePlan& tail = started.value().frames()[1];
    if (head.firstSlot != kCanSlot || head.slotCount != kMaxReadChunkSlots || tail.firstSlot != kCanSlot + kMaxReadChunkSlots
        || tail.slotCount != static_cast<std::uint16_t>(kCanCount - kMaxReadChunkSlots)) {
        return fail("can_msg_uses_full_frames");
    }
    if (!drive(reader, scheduler, started.value(), RuntimeTime{std::chrono::seconds{6}}, "can_msg_uses_full_frames")) {
        return false;
    }
    const ernc::application::BatchParameterImage* imageResult = reader.imageOf(can);
    if (imageResult == nullptr || imageResult->scalar || imageResult->elements.size() != kCanCount || store.find(can) != nullptr
        || transport.submitted().size() != 2U) {
        return fail("can_msg_uses_full_frames");
    }
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    if (!floatBits(imageResult->elements.front(), first) || !floatBits(imageResult->elements.back(), last) || first != 1U
        || last != kCanCount) {
        return fail("can_msg_uses_full_frames");
    }
    return true;
}

/// @brief check_data 整段不出现在任何一帧里。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return All 的帧都不含 check_data，并且它被记成非标量跳过时为 true。
bool checkDataIsNotInAFrame(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    SilentTransport transport;
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    ParameterRuntimeStore store;
    ParameterBatchReader reader{catalog, layout, store, session, scheduler};
    if (!session.connect().ok()) {
        return fail("check_data_is_not_in_a_frame");
    }
    ParameterId check = ParameterId::parse("check_data").value();
    if (!parseId("check_data", "check_data_is_not_in_a_frame", check)) {
        return false;
    }
    const Result<BatchReadTicket> started = reader.start(BatchReadScope{BatchReadScopeKind::All, {}, {}});
    if (!started.ok() || started.tryValue() == nullptr) {
        return fail("check_data_is_not_in_a_frame");
    }
    for (const BatchFramePlan& frame : started.value().frames()) {
        if (frameHas(frame, check)) {
            return fail("check_data_is_not_in_a_frame");
        }
    }
    bool skipped = false;
    for (const ernc::application::BatchSkip& skip : reader.skipped()) {
        if (skip.id == check && skip.reason == BatchSkipReason::NotScalar) {
            skipped = true;
        }
    }
    if (!skipped) {
        return fail("check_data_is_not_in_a_frame");
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
    if (name == "all_first_frame_is_full") {
        return allFirstFrameIsFull(catalog, layout);
    }
    if (name == "flag_switches_share_one_frame") {
        return flagSwitchesShareOneFrame(catalog, layout);
    }
    if (name == "gap_is_not_filled") {
        return gapIsNotFilled(catalog, layout);
    }
    if (name == "selected_pair_updates_values") {
        return selectedPairUpdatesValues(catalog, layout);
    }
    if (name == "can_msg_uses_full_frames") {
        return canMsgUsesFullFrames(catalog, layout);
    }
    if (name == "check_data_is_not_in_a_frame") {
        return checkDataIsNotInAFrame(catalog, layout);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个批量读用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是 layout manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_batch_read_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
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
