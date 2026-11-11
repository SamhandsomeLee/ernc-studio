#include "ernc/application/stage_value.hpp"
#include "ernc/application/write_parameter.hpp"
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

using ernc::application::CommandId;
using ernc::application::CommandOutcome;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::ParameterStager;
using ernc::application::ParameterWriter;
using ernc::application::ReadRetryLimit;
using ernc::application::RetryTime;
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

/// @brief 记录提交帧并转交给内存从站的端口。
class RecordingTransport final : public IDeviceTransport {
public:
    /// @brief 绑定从站内存。初始为关闭。
    /// @param image 从站内存，只借用。调用方保证它在本对象存活期间有效。
    explicit RecordingTransport(SlaveImage& image) : inner_{image} {}

    /// @brief 打开内部端口。
    /// @return 内部端口的打开结果。
    Result<void> open() override { return inner_.open(); }

    /// @brief 关闭内部端口。
    /// @return 内部端口的关闭结果。
    Result<void> close() override { return inner_.close(); }

    /// @brief 记下整帧后再交给从站。
    /// @param bytes 带长度前缀的请求，只在本次调用期间借用。函数保存自己的副本。
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
    /// 实际回答 FC16 和 FC03 的从站端口。
    MemorySlaveTransport inner_;

    /// 调用方提交过的整帧副本。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// @brief 收下请求但不产生响应的端口。用来观察 ACK 超时。
class SilentTransport final : public IDeviceTransport {
public:
    /// @brief 打开端口。
    /// @return 进入打开并成功。成功不是设备响应。
    Result<void> open() override {
        open_ = true;
        return Result<void>::success();
    }

    /// @brief 关闭端口。
    /// @return 成功。不排步骤。
    Result<void> close() override {
        open_ = false;
        return Result<void>::success();
    }

    /// @brief 记下整帧，不排队响应。
    /// @param bytes 带长度前缀的请求，只在本次调用期间借用。
    /// @return 已打开且非空时成功。关闭时为 DeviceDisconnected。
    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        if (!open_) {
            return Result<void>::failure(ErrorCode::DeviceDisconnected);
        }
        if (bytes.empty()) {
            return Result<void>::failure(ErrorCode::InvalidParameter);
        }
        submitted_.emplace_back(bytes.begin(), bytes.end());
        return Result<void>::success();
    }

    /// @brief 始终没有响应。
    /// @return Idle。
    TransportStep poll() override { return TransportStep{}; }

    /// @brief 按提交顺序借出已记录的请求。
    /// @return 内部向量的常量引用。
    const std::vector<std::vector<std::uint8_t>>& submitted() const noexcept { return submitted_; }

private:
    /// 当前是否打开。
    bool open_{false};

    /// 调用方提交过的整帧副本。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// @brief 已连接的单参数写路径。成员按声明顺序持有镜像、端口、会话和写对象。
struct Bench {
    /// 一个 slot 的从站镜像。
    SlaveImage image;

    /// 记录请求的端口。
    RecordingTransport transport;

    /// 与 transport 绑定的会话。
    DeviceSession session;

    /// 与 session 使用同一条端口的调度器。
    CommandScheduler scheduler;

    /// 运行状态。
    ParameterRuntimeStore store;

    /// 暂存入口。写测试用它放入合法目标。
    ParameterStager stager;

    /// 写用例。
    ParameterWriter writer;

    /// @brief 用已载入的目录和布局组装写路径。
    /// @param catalog 参数目录，只借用。必须比本对象活得久。
    /// @param layout 固件布局，只借用。必须比本对象活得久。
    Bench(const ParameterCatalog& catalog, const FirmwareLayout& layout)
        : image{std::vector<std::uint8_t>(4U, 0)}
        , transport{image}
        , session{transport}
        , scheduler{session, transport}
        , stager{catalog, layout, store}
        , writer{catalog, layout, store, session, scheduler} {}
};

/// @brief 确认一帧的功能码和 slot 0 的寄存器头。
/// @param frame 带长度前缀的请求。
/// @param function 期望的功能码。FC16 为 0x10，FC03 为 0x03。
/// @return 从站、功能码、起始寄存器 0 和寄存器数量 2 都匹配时为 true。
bool slotZeroHeader(const std::vector<std::uint8_t>& frame, std::uint8_t function) {
    return frame.size() >= 7U && frame[1] == 0x01 && frame[2] == function && frame[3] == 0x00 && frame[4] == 0x00
           && frame[5] == 0x00 && frame[6] == 0x02;
}

/// @brief 取出 float 暂存值或设备值。
/// @param value 标量，只在本次调用期间借用。
/// @param number 成功时写入 float。
/// @return 种类是 Float32 时为 true。
bool asFloat(const ScalarValue& value, float& number) {
    const Result<float> decoded = value.asFloat();
    if (!decoded.ok() || decoded.tryValue() == nullptr) {
        return false;
    }
    number = *decoded.tryValue();
    return true;
}

/// @brief 把调度器里的写结果送进写对象，直到不再忙碌或次数用尽。
/// @param bench 写路径。
/// @param id 这笔写的命令号。
/// @param readAt 读回记入 Store 的时刻。
/// @param name 用例名。
/// @return 写对象不再忙碌时为 true。
bool drive(Bench& bench, CommandId id, RuntimeTime readAt, const char* name) {
    for (int turn = 0; turn < 8 && bench.writer.busy(); ++turn) {
        if (std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(id)) {
            bench.writer.apply(*outcome, readAt);
        }
        bench.scheduler.pump();
    }
    if (std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(id)) {
        bench.writer.apply(*outcome, readAt);
    }
    if (bench.writer.busy()) {
        return fail(name);
    }
    return true;
}

/// @brief 读回与写入字节相同后，dirty 被清掉，stagedValue 仍保留。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return deviceValue 和 stagedValue 都是 2，且先 FC16 后 FC03 时为 true。
bool ancOffVerifiedClearsDirty(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    Bench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("anc_off_verified_clears_dirty");
    }
    const Result<ParameterId> parsed = ParameterId::parse("ANC_OFF");
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail("anc_off_verified_clears_dirty");
    }
    const ParameterId& id = *parsed.tryValue();
    const RuntimeTime stagedAt{std::chrono::seconds{11}};
    const RuntimeTime readAt{std::chrono::seconds{12}};
    if (!bench.stager.stage(id, ScalarValue::fromFloat(2.0F), stagedAt).ok()) {
        return fail("anc_off_verified_clears_dirty");
    }
    const Result<CommandId> started = bench.writer.write(id);
    if (!started.ok() || started.tryValue() == nullptr) {
        return fail("anc_off_verified_clears_dirty");
    }
    if (!drive(bench, *started.tryValue(), readAt, "anc_off_verified_clears_dirty")) {
        return false;
    }
    const ParameterRuntimeState* state = bench.store.find(id);
    float device = 0.0F;
    float staged = 0.0F;
    if (state == nullptr || !state->deviceValue || !state->stagedValue || state->dirty || state->error.has_value()
        || state->stagedAt != stagedAt || state->readAt != readAt || !asFloat(*state->deviceValue, device)
        || !asFloat(*state->stagedValue, staged) || device != 2.0F || staged != 2.0F) {
        return fail("anc_off_verified_clears_dirty");
    }
    const std::vector<std::vector<std::uint8_t>>& frames = bench.transport.submitted();
    if (frames.size() != 2U || !slotZeroHeader(frames[0], 0x10) || !slotZeroHeader(frames[1], 0x03)) {
        return fail("anc_off_verified_clears_dirty");
    }
    return true;
}

/// @brief 读回字节不同时保留用户目标，并把 deviceValue 改成读回值。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return stagedValue 仍是 2、deviceValue 是 1、dirty 为 true 时为 true。
bool readbackMismatchKeepsStaged(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    Bench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("readback_mismatch_keeps_staged");
    }
    const Result<ParameterId> parsed = ParameterId::parse("ANC_OFF");
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail("readback_mismatch_keeps_staged");
    }
    const ParameterId& id = *parsed.tryValue();
    const RuntimeTime stagedAt{std::chrono::seconds{3}};
    if (!bench.stager.stage(id, ScalarValue::fromFloat(2.0F), stagedAt).ok()) {
        return fail("readback_mismatch_keeps_staged");
    }
    const Result<CommandId> started = bench.writer.write(id);
    if (!started.ok() || started.tryValue() == nullptr) {
        return fail("readback_mismatch_keeps_staged");
    }
    const std::uint8_t readback[] = {0x00, 0x00, 0x80, 0x3F};
    if (!bench.image.write(0, readback)) {
        return fail("readback_mismatch_keeps_staged");
    }
    if (!drive(bench, *started.tryValue(), RuntimeTime{std::chrono::seconds{4}}, "readback_mismatch_keeps_staged")) {
        return false;
    }
    const ParameterRuntimeState* state = bench.store.find(id);
    float device = 0.0F;
    float staged = 0.0F;
    if (state == nullptr || !state->deviceValue || !state->stagedValue || !state->dirty || state->stagedAt != stagedAt
        || state->error != ErrorCode::ReadBackMismatch || !asFloat(*state->deviceValue, device)
        || !asFloat(*state->stagedValue, staged) || device != 1.0F || staged != 2.0F) {
        return fail("readback_mismatch_keeps_staged");
    }
    return true;
}

/// @brief ACK 超时后不重发 FC16，也不改 deviceValue。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 只提交一条 FC16，stagedValue 仍是 2，deviceValue 为空时为 true。
bool ackTimeoutDoesNotResend(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    SilentTransport transport;
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    ParameterRuntimeStore store;
    ParameterStager stager{catalog, layout, store};
    ParameterWriter writer{catalog, layout, store, session, scheduler};
    if (!session.connect().ok()) {
        return fail("ack_timeout_does_not_resend");
    }
    const Result<ParameterId> parsed = ParameterId::parse("ANC_OFF");
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail("ack_timeout_does_not_resend");
    }
    const ParameterId& id = *parsed.tryValue();
    const RuntimeTime stagedAt{std::chrono::seconds{7}};
    if (!stager.stage(id, ScalarValue::fromFloat(2.0F), stagedAt).ok()) {
        return fail("ack_timeout_does_not_resend");
    }
    const Result<CommandId> started = writer.write(id);
    if (!started.ok() || started.tryValue() == nullptr) {
        return fail("ack_timeout_does_not_resend");
    }
    const CommandId command = *started.tryValue();
    const RetryTime start{std::chrono::milliseconds{0}};
    const ReadRetryLimit once{std::chrono::milliseconds{100}, 1};
    scheduler.pump(start, once);
    scheduler.pump(start + once.timeout, once);
    const std::optional<CommandOutcome> outcome = scheduler.takeResult(command);
    if (!outcome.has_value()) {
        return fail("ack_timeout_does_not_resend");
    }
    writer.apply(*outcome, RuntimeTime{std::chrono::seconds{8}});
    const ParameterRuntimeState* state = store.find(id);
    float staged = 0.0F;
    if (writer.busy() || state == nullptr || state->deviceValue.has_value() || !state->stagedValue || !state->dirty
        || state->stagedAt != stagedAt || state->error != ErrorCode::ResponseTimeout || !asFloat(*state->stagedValue, staged)
        || staged != 2.0F || transport.submitted().size() != 1U || !slotZeroHeader(transport.submitted()[0], 0x10)) {
        return fail("ack_timeout_does_not_resend");
    }
    return true;
}

/// @brief 直接放进 Store 的 Step 仍然不入队。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 错误码为 ReadOnlyViolation 且提交数为 0 时为 true。
bool stepDoesNotEnqueue(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    Bench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("step_does_not_enqueue");
    }
    const Result<ParameterId> parsed = ParameterId::parse("Step");
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail("step_does_not_enqueue");
    }
    const ParameterId& id = *parsed.tryValue();
    bench.store.stage(id, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{1}});
    const Result<CommandId> started = bench.writer.write(id);
    if (started.ok() || started.tryError() == nullptr || *started.tryError() != ErrorCode::ReadOnlyViolation
        || !bench.transport.submitted().empty()) {
        return fail("step_does_not_enqueue");
    }
    return true;
}

/// @brief 直接放进 Store 的 Can_Msg 仍然不入队。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 错误码为 InvalidParameter 且提交数为 0 时为 true。
bool canMsgDoesNotEnqueue(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    Bench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("can_msg_does_not_enqueue");
    }
    const Result<ParameterId> parsed = ParameterId::parse("Can_Msg");
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail("can_msg_does_not_enqueue");
    }
    const ParameterId& id = *parsed.tryValue();
    bench.store.stage(id, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{1}});
    const Result<CommandId> started = bench.writer.write(id);
    if (started.ok() || started.tryError() == nullptr || *started.tryError() != ErrorCode::InvalidParameter
        || !bench.transport.submitted().empty()) {
        return fail("can_msg_does_not_enqueue");
    }
    return true;
}

/// @brief 写已经发出后又改暂存值时，读回成功也不清 dirty。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return deviceValue 是 2、stagedValue 是 3、dirty 为 true 时为 true。
bool editDuringWriteStaysDirty(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    Bench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("edit_during_write_stays_dirty");
    }
    const Result<ParameterId> parsed = ParameterId::parse("ANC_OFF");
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail("edit_during_write_stays_dirty");
    }
    const ParameterId& id = *parsed.tryValue();
    if (!bench.stager.stage(id, ScalarValue::fromFloat(2.0F), RuntimeTime{std::chrono::seconds{1}}).ok()) {
        return fail("edit_during_write_stays_dirty");
    }
    const Result<CommandId> started = bench.writer.write(id);
    if (!started.ok() || started.tryValue() == nullptr) {
        return fail("edit_during_write_stays_dirty");
    }
    const RuntimeTime editedAt{std::chrono::seconds{2}};
    if (!bench.stager.stage(id, ScalarValue::fromFloat(3.0F), editedAt).ok()) {
        return fail("edit_during_write_stays_dirty");
    }
    if (!drive(bench, *started.tryValue(), RuntimeTime{std::chrono::seconds{3}}, "edit_during_write_stays_dirty")) {
        return false;
    }
    const ParameterRuntimeState* state = bench.store.find(id);
    float device = 0.0F;
    float staged = 0.0F;
    if (state == nullptr || !state->deviceValue || !state->stagedValue || !state->dirty || state->stagedAt != editedAt
        || !asFloat(*state->deviceValue, device) || !asFloat(*state->stagedValue, staged) || device != 2.0F
        || staged != 3.0F) {
        return fail("edit_during_write_stays_dirty");
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
    if (name == "anc_off_verified_clears_dirty") {
        return ancOffVerifiedClearsDirty(catalog, layout);
    }
    if (name == "readback_mismatch_keeps_staged") {
        return readbackMismatchKeepsStaged(catalog, layout);
    }
    if (name == "ack_timeout_does_not_resend") {
        return ackTimeoutDoesNotResend(catalog, layout);
    }
    if (name == "step_does_not_enqueue") {
        return stepDoesNotEnqueue(catalog, layout);
    }
    if (name == "can_msg_does_not_enqueue") {
        return canMsgDoesNotEnqueue(catalog, layout);
    }
    if (name == "edit_during_write_stays_dirty") {
        return editDuringWriteStaysDirty(catalog, layout);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个单参数写用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是 layout manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_write_parameter_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
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
