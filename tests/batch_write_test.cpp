#include "ernc/application/batch_write.hpp"
#include "ernc/application/stage_value.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"

#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

using ernc::application::BatchWriteItem;
using ernc::application::BatchWriteKind;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::ParameterBatchWriter;
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
using ernc::domain::WriteExclusion;
using ernc::domain::WriteScope;
using ernc::domain::WriteScopeKind;
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

/// @brief 记录提交帧并转交给内存从站。
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
    /// 实际回答 FC16 和随后 FC03 的从站端口。
    MemorySlaveTransport inner_;

    /// 调用方提交过的整帧副本。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// @brief 已连接的批量写路径。镜像有 slot 0 和 slot 1。
struct Bench {
    /// 两个 slot 的从站镜像。
    SlaveImage image;

    /// 记录请求的端口。
    RecordingTransport transport;

    /// 与 transport 绑定的会话。
    DeviceSession session;

    /// 与 session 使用同一条端口的调度器。
    CommandScheduler scheduler;

    /// 运行状态。
    ParameterRuntimeStore store;

    /// 合法标量的暂存入口。
    ParameterStager stager;

    /// 批量写。
    ParameterBatchWriter writer;

    /// @brief 用已载入的目录和布局组装写路径。
    /// @param catalog 参数目录，只借用。必须比本对象活得久。
    /// @param layout 固件布局，只借用。必须比本对象活得久。
    Bench(const ParameterCatalog& catalog, const FirmwareLayout& layout)
        : image{std::vector<std::uint8_t>(8U, 0)}
        , transport{image}
        , session{transport}
        , scheduler{session, transport}
        , stager{catalog, layout, store}
        , writer{catalog, layout, store, session, scheduler} {}
};

/// @brief 解析参数 ID。
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

/// @brief 确认一帧是指定 slot 的 FC16 或 FC03。
/// @param frame 带长度前缀的请求。
/// @param function FC16 为 0x10，FC03 为 0x03。
/// @param slot 逻辑 slot。每个 slot 对应两个寄存器。
/// @return 从站、功能码、起始寄存器和寄存器数量都匹配时为 true。
bool oneSlotHeader(const std::vector<std::uint8_t>& frame, std::uint8_t function, std::uint16_t slot) {
    const std::uint16_t start = static_cast<std::uint16_t>(slot * 2U);
    return frame.size() >= 7U && frame[1] == 0x01 && frame[2] == function
           && frame[3] == static_cast<std::uint8_t>((start >> 8U) & 0xFFU)
           && frame[4] == static_cast<std::uint8_t>(start & 0xFFU) && frame[5] == 0x00 && frame[6] == 0x02;
}

/// @brief 在结果里查找一个参数。
/// @param items 批量写结果。
/// @param id 参数标识。
/// @return 找到时指向该项。没有时为空。
const BatchWriteItem* findItem(const std::vector<BatchWriteItem>& items, const ParameterId& id) {
    for (const BatchWriteItem& item : items) {
        if (item.id == id) {
            return &item;
        }
    }
    return nullptr;
}

/// @brief 泵调度器，直到这批写结束。
/// @param bench 写路径。
/// @param readAt 读回记入 Store 的时刻。
/// @param name 用例名。
/// @return 不再忙碌时为 true。
bool drive(Bench& bench, RuntimeTime readAt, const char* name) {
    for (int turn = 0; turn < 8 && bench.writer.busy(); ++turn) {
        if (const std::optional<ernc::application::CommandId> id = bench.writer.inFlight()) {
            if (std::optional<ernc::application::CommandOutcome> outcome = bench.scheduler.takeResult(*id)) {
                bench.writer.apply(*outcome, readAt);
            }
        }
        bench.scheduler.pump();
    }
    if (const std::optional<ernc::application::CommandId> id = bench.writer.inFlight()) {
        if (std::optional<ernc::application::CommandOutcome> outcome = bench.scheduler.takeResult(*id)) {
            bench.writer.apply(*outcome, readAt);
        }
    }
    if (bench.writer.busy()) {
        return fail(name);
    }
    return true;
}

/// @brief 做成只含两个参数的选中范围。顺序就是提交顺序。
/// @param first 先写的参数。
/// @param second 后写的参数。
/// @return 调用方拥有的范围。
WriteScope selectedTwo(const ParameterId& first, const ParameterId& second) {
    WriteScope scope;
    scope.kind = WriteScopeKind::Selected;
    scope.selected.push_back(first);
    scope.selected.push_back(second);
    return scope;
}

/// @brief ANC_OFF ACK 之后，才发送 ENC_OFF 的写。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 两帧 slot 0 与 slot 1 的 FC16，两项 BatchWriteKind::Verified，且 dirty 清掉时为 true。
bool twoScalarsWriteOneAtATime(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    Bench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("two_scalars_write_one_at_a_time");
    }
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    ParameterId enc = ParameterId::parse("ENC_OFF").value();
    if (!parseId("ANC_OFF", "two_scalars_write_one_at_a_time", anc)
        || !parseId("ENC_OFF", "two_scalars_write_one_at_a_time", enc)) {
        return false;
    }
    const RuntimeTime stagedAt{std::chrono::seconds{11}};
    if (!bench.stager.stage(anc, ScalarValue::fromFloat(2.0F), stagedAt).ok()
        || !bench.stager.stage(enc, ScalarValue::fromFloat(3.0F), stagedAt).ok()) {
        return fail("two_scalars_write_one_at_a_time");
    }
    if (!bench.writer.start(selectedTwo(anc, enc)).ok()) {
        return fail("two_scalars_write_one_at_a_time");
    }
    if (!drive(bench, RuntimeTime{std::chrono::seconds{12}}, "two_scalars_write_one_at_a_time")) {
        return false;
    }
    const ParameterRuntimeState* ancState = bench.store.find(anc);
    const ParameterRuntimeState* encState = bench.store.find(enc);
    const BatchWriteItem* ancItem = findItem(bench.writer.items(), anc);
    const BatchWriteItem* encItem = findItem(bench.writer.items(), enc);
    if (ancState == nullptr || encState == nullptr || ancState->dirty || encState->dirty || ancState->deviceValue.has_value()
        || encState->deviceValue.has_value() || ancItem == nullptr || encItem == nullptr
        || ancItem->kind != BatchWriteKind::Verified || encItem->kind != BatchWriteKind::Verified) {
        return fail("two_scalars_write_one_at_a_time");
    }
    const std::vector<std::vector<std::uint8_t>>& frames = bench.transport.submitted();
    if (frames.size() != 2U || !oneSlotHeader(frames[0], 0x10, 0) || !oneSlotHeader(frames[1], 0x10, 1)) {
        return fail("two_scalars_write_one_at_a_time");
    }
    return true;
}

/// @brief 不从站读回时，镜像与写入不一致也不产生 Mismatch；两项仍按 ACK 记 Verified，ENC_OFF 随后写完。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return ANC_OFF 与 ENC_OFF 均为 Verified、dirty 均为 false、无 ReadBackMismatch 时为 true。
bool mismatchNamesAncThenWritesEnc(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    Bench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("mismatch_names_anc_then_writes_enc");
    }
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    ParameterId enc = ParameterId::parse("ENC_OFF").value();
    if (!parseId("ANC_OFF", "mismatch_names_anc_then_writes_enc", anc)
        || !parseId("ENC_OFF", "mismatch_names_anc_then_writes_enc", enc)) {
        return false;
    }
    const RuntimeTime stagedAt{std::chrono::seconds{3}};
    if (!bench.stager.stage(anc, ScalarValue::fromFloat(2.0F), stagedAt).ok()
        || !bench.stager.stage(enc, ScalarValue::fromFloat(3.0F), stagedAt).ok()) {
        return fail("mismatch_names_anc_then_writes_enc");
    }
    if (!bench.writer.start(selectedTwo(anc, enc)).ok()) {
        return fail("mismatch_names_anc_then_writes_enc");
    }
    const std::uint8_t readback[] = {0x00, 0x00, 0x80, 0x3F};
    if (!bench.image.write(0, readback)) {
        return fail("mismatch_names_anc_then_writes_enc");
    }
    if (!drive(bench, RuntimeTime{std::chrono::seconds{4}}, "mismatch_names_anc_then_writes_enc")) {
        return false;
    }
    const ParameterRuntimeState* ancState = bench.store.find(anc);
    const ParameterRuntimeState* encState = bench.store.find(enc);
    const BatchWriteItem* ancItem = findItem(bench.writer.items(), anc);
    const BatchWriteItem* encItem = findItem(bench.writer.items(), enc);
    if (ancState == nullptr || encState == nullptr || ancState->dirty || encState->dirty || !ancState->stagedValue
        || !encState->stagedValue || ancState->error.has_value() || encState->error.has_value()
        || ancState->deviceValue.has_value() || encState->deviceValue.has_value() || ancItem == nullptr
        || encItem == nullptr || ancItem->kind != BatchWriteKind::Verified || encItem->kind != BatchWriteKind::Verified) {
        return fail("mismatch_names_anc_then_writes_enc");
    }
    const Result<float> staged = ancState->stagedValue->asFloat();
    const Result<float> encStaged = encState->stagedValue->asFloat();
    const std::vector<std::vector<std::uint8_t>>& frames = bench.transport.submitted();
    if (!staged.ok() || !encStaged.ok() || staged.value() != 2.0F || encStaged.value() != 3.0F || frames.size() != 2U
        || !oneSlotHeader(frames[0], 0x10, 0) || !oneSlotHeader(frames[1], 0x10, 1)) {
        return fail("mismatch_names_anc_then_writes_enc");
    }
    return true;
}

/// @brief Step 和 Can_Msg 不产生 FC16。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return Step 被排除，Can_Msg 因不是 1 个 slot 被跳过，提交数为 0 时为 true。
bool stepAndCanMsgAreNotWritten(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    Bench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("step_and_can_msg_are_not_written");
    }
    ParameterId step = ParameterId::parse("Step").value();
    ParameterId can = ParameterId::parse("Can_Msg").value();
    if (!parseId("Step", "step_and_can_msg_are_not_written", step)
        || !parseId("Can_Msg", "step_and_can_msg_are_not_written", can)) {
        return false;
    }
    const RuntimeTime stagedAt{std::chrono::seconds{1}};
    bench.store.stage(step, ScalarValue::fromFloat(1.0F), stagedAt);
    bench.store.stage(can, ScalarValue::fromFloat(1.0F), stagedAt);
    if (!bench.writer.start(selectedTwo(step, can)).ok() || bench.writer.busy()) {
        return fail("step_and_can_msg_are_not_written");
    }
    const BatchWriteItem* stepItem = findItem(bench.writer.items(), step);
    const BatchWriteItem* canItem = findItem(bench.writer.items(), can);
    if (stepItem == nullptr || canItem == nullptr || stepItem->kind != BatchWriteKind::Excluded
        || stepItem->exclusion != WriteExclusion::ReadOnly || canItem->kind != BatchWriteKind::NotOneSlot
        || !bench.transport.submitted().empty()) {
        return fail("step_and_can_msg_are_not_written");
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
    if (name == "two_scalars_write_one_at_a_time") {
        return twoScalarsWriteOneAtATime(catalog, layout);
    }
    if (name == "mismatch_names_anc_then_writes_enc") {
        return mismatchNamesAncThenWritesEnc(catalog, layout);
    }
    if (name == "step_and_can_msg_are_not_written") {
        return stepAndCanMsgAreNotWritten(catalog, layout);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个批量写用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是 layout manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_batch_write_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
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
