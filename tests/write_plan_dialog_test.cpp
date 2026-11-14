#include "parameters/write_plan_dialog.hpp"

#include "device/device_runtime_worker.hpp"
#include "ernc/application/batch_write.hpp"
#include "ernc/domain/array_runtime_store.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

#include <QApplication>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::ParameterBatchWriter;
using ernc::application::ReadRetryLimit;
using ernc::application::RetryTime;
using ernc::domain::CatalogLoad;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::ParameterRuntimeStore;
using ernc::domain::Result;
using ernc::domain::RuntimeTime;
using ernc::domain::ScalarValue;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;
using ernc::presentation::WritePlanColumn;
using ernc::presentation::WritePlanDialog;
using ernc::simulator::MemorySlaveTransport;
using ernc::simulator::SlaveImage;

/// 写 ACK 等待用的测试上限。100 毫秒不是设备基线。
constexpr std::chrono::milliseconds kAckWait{100};

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

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

/// @brief 做成对话框借用的目录、状态、布局和页面。
/// @param catalog 参数目录，只借用。
/// @param store 运行状态，只借用。
/// @param layout 固件布局，只借用。
/// @param pageId 项目树页面 id。调用方拥有原件。
/// @return 对话框按值保存这些指针和页面文本。
WritePlanDialog::Sources sourcesFor(const ParameterCatalog& catalog, ParameterRuntimeStore& store, const FirmwareLayout& layout,
                                    const char* pageId) {
    WritePlanDialog::Sources sources;
    sources.catalog = &catalog;
    sources.store = &store;
    sources.layout = &layout;
    sources.pageId = QString::fromUtf8(pageId);
    return sources;
}

/// @brief 读取一格。列号来自 WritePlanColumn。
/// @param dialog 预览对话框。
/// @param row 行号。
/// @param column 列。
/// @return 单元格文字。
QString cellAt(const WritePlanDialog& dialog, int row, WritePlanColumn column) {
    return dialog.cell(row, static_cast<int>(column));
}

/// @brief 确认一帧是指定 slot 的 FC16 或 FC03。
/// @param frame 带长度前缀的请求。
/// @param function FC16 为 0x10，FC03 为 0x03。
/// @param slot 逻辑 slot。每个 slot 对应两个寄存器。
/// @return 从站、功能码、起始寄存器和寄存器数量都匹配时为 true。
bool oneSlotHeader(const std::vector<std::uint8_t>& frame, std::uint8_t function, std::uint16_t slot) {
    const std::uint16_t start = static_cast<std::uint16_t>(slot * 2U);
    return frame.size() >= 7U && frame[1] == 0x01 && frame[2] == function
        && frame[3] == static_cast<std::uint8_t>((start >> 8U) & 0xFFU) && frame[4] == static_cast<std::uint8_t>(start & 0xFFU)
        && frame[5] == 0x00 && frame[6] == 0x02;
}

/// @brief 统计功能码出现的次数。
/// @param frames 已提交的整帧。
/// @param function FC16 为 0x10，FC03 为 0x03。
/// @return 该功能码的帧数。
int countFunction(const std::vector<std::vector<std::uint8_t>>& frames, std::uint8_t function) {
    int count = 0;
    for (const std::vector<std::uint8_t>& frame : frames) {
        if (frame.size() >= 3U && frame[2] == function) {
            ++count;
        }
    }
    return count;
}

/// 记录提交帧并转交给内存从站。
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

/// 接受提交但不回任何字节。用来让写 ACK 超时。
class SilentTransport final : public IDeviceTransport {
public:
    /// @brief 打开端口。不访问串口。
    /// @return 成功。
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

    /// @brief 记下整帧，不产生应答。
    /// @param bytes 带长度前缀的请求，只在本次调用期间借用。
    /// @return 未打开时为 DeviceDisconnected。打开时成功，并且不表示设备 ACK。
    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        if (!open_) {
            return Result<void>::failure(ernc::domain::ErrorCode::DeviceDisconnected);
        }
        submitted_.emplace_back(bytes.begin(), bytes.end());
        return Result<void>::success();
    }

    /// @brief 始终没有字节。
    /// @return Idle。这不是超时，超时由调度器的时钟判断。
    TransportStep poll() override { return {}; }

    /// @brief 按提交顺序借出已记录的请求。
    /// @return 内部向量的常量引用。
    const std::vector<std::vector<std::uint8_t>>& submitted() const noexcept { return submitted_; }

private:
    /// 为 true 时接受提交。
    bool open_{false};

    /// 调用方提交过的整帧副本。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// 已连接的内存从站写路径。镜像覆盖 slot 0。
struct MemoryBench {
    /// slot 0 的从站镜像。8 字节等于两个 slot。
    SlaveImage image;

    /// 记录请求的端口。
    RecordingTransport transport;

    /// 与 transport 绑定的会话。
    DeviceSession session;

    /// 与 session 使用同一条端口的调度器。
    CommandScheduler scheduler;

    /// 运行状态。对话框和批量写共用它。
    ParameterRuntimeStore store;

    /// 批量写。
    ParameterBatchWriter writer;

    /// @brief 用已载入的目录和布局组装写路径。
    /// @param catalog 参数目录，只借用。必须比本对象活得久。
    /// @param layout 固件布局，只借用。必须比本对象活得久。
    MemoryBench(const ParameterCatalog& catalog, const FirmwareLayout& layout)
        : image{std::vector<std::uint8_t>(8U, 0)}
        , transport{image}
        , session{transport}
        , scheduler{session, transport}
        , writer{catalog, layout, store, session, scheduler} {}
};

/// 不回答的写路径。用来观察超时后的诊断。
struct SilentBench {
    /// 不回字节的端口。
    SilentTransport transport;

    /// 与 transport 绑定的会话。
    DeviceSession session;

    /// 与 session 使用同一条端口的调度器。
    CommandScheduler scheduler;

    /// 运行状态。
    ParameterRuntimeStore store;

    /// 批量写。
    ParameterBatchWriter writer;

    /// @brief 用已载入的目录和布局组装写路径。
    /// @param catalog 参数目录，只借用。必须比本对象活得久。
    /// @param layout 固件布局，只借用。必须比本对象活得久。
    SilentBench(const ParameterCatalog& catalog, const FirmwareLayout& layout)
        : session{transport}, scheduler{session, transport}, writer{catalog, layout, store, session, scheduler} {}
};

/// @brief 泵内存从站，直到这批写结束，并刷新对话框。
/// @param bench 写路径。
/// @param dialog 预览。它只读阶段，不自己泵端口。
/// @param readAt 读回记入 Store 的时刻。
/// @param name 用例名。
/// @return 不再忙碌时为 true。
bool finishMemory(MemoryBench& bench, WritePlanDialog& dialog, RuntimeTime readAt, const char* name) {
    for (int turn = 0; turn < 8 && bench.writer.busy(); ++turn) {
        bench.scheduler.pump();
        if (const std::optional<ernc::application::CommandId> id = bench.writer.inFlight()) {
            if (std::optional<ernc::application::CommandOutcome> outcome = bench.scheduler.takeResult(*id)) {
                bench.writer.apply(*outcome, readAt);
            }
        }
        dialog.noteProgress();
    }
    if (bench.writer.busy()) {
        return fail(name);
    }
    return true;
}

/// @brief ERNC_Flag 在确认前显示开关差异，确认后因没有会话而不发送。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return ANC_OFF 的 Read 为过期的关、Set 为开，RNC_refresh 为待写入，且确认后仍是待写入时为 true。
bool flagPreviewBeforeWrite(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    ParameterId refresh = ParameterId::parse("RNC_refresh").value();
    if (!parseId("ANC_OFF", "flag_preview_before_write", anc) || !parseId("RNC_refresh", "flag_preview_before_write", refresh)) {
        return false;
    }
    store.observe(anc, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{1}});
    store.markOffline();
    store.stage(anc, ScalarValue::fromFloat(0.0F), RuntimeTime{std::chrono::seconds{2}});
    store.stage(refresh, ScalarValue::fromInt32(1), RuntimeTime{std::chrono::seconds{2}});

    WritePlanDialog dialog(sourcesFor(catalog, store, layout, "ERNC_Flag"));
    const int ancRow = dialog.rowOf(QStringLiteral("ANC_OFF"));
    const int refreshRow = dialog.rowOf(QStringLiteral("RNC_refresh"));
    if (ancRow < 0 || refreshRow < 0 || !dialog.confirmEnabled() || !dialog.diagnosticText().isEmpty()) {
        return fail("flag_preview_before_write");
    }
    if (cellAt(dialog, ancRow, WritePlanColumn::Read) != QStringLiteral("关 数据已过期")
        || cellAt(dialog, ancRow, WritePlanColumn::Set) != QStringLiteral("开")
        || cellAt(dialog, ancRow, WritePlanColumn::Status) != QStringLiteral("待写入")
        || cellAt(dialog, refreshRow, WritePlanColumn::Status) != QStringLiteral("待写入")) {
        return fail("flag_preview_before_write");
    }
    dialog.confirmWrite();
    if (dialog.diagnosticText() != QStringLiteral("DeviceDisconnected")
        || cellAt(dialog, ancRow, WritePlanColumn::Status) != QStringLiteral("待写入") || dialog.confirmEnabled()) {
        return fail("flag_preview_before_write");
    }
    return true;
}

/// @brief RNC 页的时间保持固件原值，Step 可进入写入计划。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return running_time 的 Set 为 1100 且不含秒，Step 状态为待写入时为 true。
bool rncTimeAndStep(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId running = ParameterId::parse("running_time").value();
    ParameterId step = ParameterId::parse("Step").value();
    if (!parseId("running_time", "rnc_time_and_step", running) || !parseId("Step", "rnc_time_and_step", step)) {
        return false;
    }
    store.stage(running, ScalarValue::fromFloat(1100.0F), RuntimeTime{std::chrono::seconds{3}});
    store.stage(step, ScalarValue::fromFloat(4.0F), RuntimeTime{std::chrono::seconds{3}});

    WritePlanDialog dialog(sourcesFor(catalog, store, layout, "RNC_Param"));
    const int timeRow = dialog.rowOf(QStringLiteral("running_time"));
    const int stepRow = dialog.rowOf(QStringLiteral("Step"));
    if (timeRow < 0 || stepRow < 0 || !dialog.confirmEnabled()) {
        return fail("rnc_time_and_step");
    }
    const QString setText = cellAt(dialog, timeRow, WritePlanColumn::Set);
    if (setText != QStringLiteral("1100") || setText.contains(QStringLiteral("秒"))
        || cellAt(dialog, timeRow, WritePlanColumn::Read).contains(QStringLiteral("秒"))
        || cellAt(dialog, stepRow, WritePlanColumn::Status) != QStringLiteral("待写入")) {
        return fail("rnc_time_and_step");
    }
    return true;
}

/// @brief ALL 页包含全目录 dirty 标量，与其它页的写入预览语义一致。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 暂存 ANC_OFF 后 ALL 范围出现可写行且确认可用时为 true。
bool allPageIncludesDirty(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    if (!parseId("ANC_OFF", "all_page_includes_dirty", anc)) {
        return false;
    }
    store.stage(anc, ScalarValue::fromFloat(0.0F), RuntimeTime{std::chrono::seconds{1}});
    WritePlanDialog dialog(sourcesFor(catalog, store, layout, "ALL"));
    if (dialog.planRowCount() == 0 || !dialog.confirmEnabled() || dialog.rowOf(QStringLiteral("ANC_OFF")) < 0) {
        return fail("all_page_includes_dirty");
    }
    return true;
}

/// @brief Can_Msg 即使被暂存也不进入可写行。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 状态为不写入，且确认不可用时为 true。
bool canMsgIsNotSent(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    ParameterRuntimeStore store;
    ParameterId can = ParameterId::parse("Can_Msg").value();
    if (!parseId("Can_Msg", "can_msg_is_not_sent", can)) {
        return false;
    }
    store.stage(can, ScalarValue::fromFloat(1.0F), RuntimeTime{std::chrono::seconds{1}});
    WritePlanDialog dialog(sourcesFor(catalog, store, layout, "other"));
    const int row = dialog.rowOf(QStringLiteral("Can_Msg"));
    if (row < 0 || dialog.confirmEnabled() || cellAt(dialog, row, WritePlanColumn::Status) != QStringLiteral("不写入")) {
        return fail("can_msg_is_not_sent");
    }
    return true;
}

/// @brief 确认后写入，FC16 ACK 即结束为「写入已确认」，不自动 FC03。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 确认前没有帧；结束后 dirty 为 false、状态为写入已确认，且只有一条 slot 0 的 FC16 时为 true。
bool ackThenVerified(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    MemoryBench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("ack_then_verified");
    }
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    if (!parseId("ANC_OFF", "ack_then_verified", anc)) {
        return false;
    }
    bench.store.stage(anc, ScalarValue::fromFloat(0.0F), RuntimeTime{std::chrono::seconds{4}});
    WritePlanDialog dialog(sourcesFor(catalog, bench.store, layout, "ERNC_Flag"));
    dialog.setWriter(&bench.writer, &bench.scheduler);
    const int row = dialog.rowOf(QStringLiteral("ANC_OFF"));
    if (row < 0 || cellAt(dialog, row, WritePlanColumn::Set) != QStringLiteral("开") || !bench.transport.submitted().empty()) {
        return fail("ack_then_verified");
    }
    dialog.confirmWrite();
    const std::vector<std::vector<std::uint8_t>>& frames = bench.transport.submitted();
    if (frames.size() != 1U || !oneSlotHeader(frames[0], 0x10, 0)
        || cellAt(dialog, row, WritePlanColumn::Status) != QStringLiteral("正在写入")) {
        return fail("ack_then_verified");
    }
    if (!finishMemory(bench, dialog, RuntimeTime{std::chrono::seconds{5}}, "ack_then_verified")) {
        return false;
    }
    const ernc::domain::ParameterRuntimeState* state = bench.store.find(anc);
    if (state == nullptr || state->dirty || cellAt(dialog, row, WritePlanColumn::Status) != QStringLiteral("写入已确认")
        || !cellAt(dialog, row, WritePlanColumn::Diagnostic).isEmpty() || frames.size() != 1U
        || countFunction(frames, 0x10) != 1 || countFunction(frames, 0x03) != 0) {
        return fail("ack_then_verified");
    }
    return true;
}

/// @brief 不从站读回时，镜像字节不同也不显示读回不一致；ACK 后仍为写入已确认。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 状态为写入已确认、dirty 为 false、仅一条 FC16、诊断为空时为 true。
bool mismatchShowsDiagnostic(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    MemoryBench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("mismatch_shows_diagnostic");
    }
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    if (!parseId("ANC_OFF", "mismatch_shows_diagnostic", anc)) {
        return false;
    }
    bench.store.stage(anc, ScalarValue::fromFloat(2.0F), RuntimeTime{std::chrono::seconds{6}});
    WritePlanDialog dialog(sourcesFor(catalog, bench.store, layout, "ERNC_Flag"));
    dialog.setWriter(&bench.writer, &bench.scheduler);
    dialog.confirmWrite();
    const std::uint8_t readback[] = {0x00, 0x00, 0x80, 0x3F};
    if (!bench.image.write(0, readback)) {
        return fail("mismatch_shows_diagnostic");
    }
    if (!finishMemory(bench, dialog, RuntimeTime{std::chrono::seconds{7}}, "mismatch_shows_diagnostic")) {
        return false;
    }
    const int row = dialog.rowOf(QStringLiteral("ANC_OFF"));
    const ernc::domain::ParameterRuntimeState* state = bench.store.find(anc);
    const std::vector<std::vector<std::uint8_t>>& frames = bench.transport.submitted();
    if (row < 0 || state == nullptr || state->dirty || cellAt(dialog, row, WritePlanColumn::Status) != QStringLiteral("写入已确认")
        || !cellAt(dialog, row, WritePlanColumn::Diagnostic).isEmpty() || state->error.has_value()
        || countFunction(frames, 0x10) != 1 || countFunction(frames, 0x03) != 0) {
        return fail("mismatch_shows_diagnostic");
    }
    return true;
}

/// @brief 写 ACK 超时后显示结果未知，释放弹窗，并保留目标值供读回后确认是否重试。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 结束后能关闭弹窗，重新预览可手动确认，且仅提交了一条 FC16 时为 true。
bool timeoutDoesNotResend(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    SilentBench bench{catalog, layout};
    if (!bench.session.connect().ok()) {
        return fail("timeout_does_not_resend");
    }
    ParameterId anc = ParameterId::parse("ANC_OFF").value();
    if (!parseId("ANC_OFF", "timeout_does_not_resend", anc)) {
        return false;
    }
    bench.store.stage(anc, ScalarValue::fromFloat(0.0F), RuntimeTime{std::chrono::seconds{8}});
    WritePlanDialog dialog(sourcesFor(catalog, bench.store, layout, "ERNC_Flag"));
    dialog.setWriter(&bench.writer, &bench.scheduler);
    dialog.show();
    dialog.confirmWrite();
    const std::vector<std::vector<std::uint8_t>>& frames = bench.transport.submitted();
    if (frames.size() != 1U || !oneSlotHeader(frames[0], 0x10, 0)) {
        return fail("timeout_does_not_resend");
    }
    const RetryTime start{std::chrono::milliseconds{0}};
    const ReadRetryLimit once{kAckWait, 1};
    bench.scheduler.pump(start, once);
    dialog.noteProgress();
    const int row = dialog.rowOf(QStringLiteral("ANC_OFF"));
    if (row < 0 || cellAt(dialog, row, WritePlanColumn::Status) != QStringLiteral("正在写入") || frames.size() != 1U
        || dialog.close()) {
        return fail("timeout_does_not_resend");
    }
    bench.scheduler.pump(start + kAckWait, once);
    if (const std::optional<ernc::application::CommandId> id = bench.writer.inFlight()) {
        if (std::optional<ernc::application::CommandOutcome> outcome = bench.scheduler.takeResult(*id)) {
            bench.writer.apply(*outcome, RuntimeTime{std::chrono::seconds{9}});
        }
    }
    dialog.noteProgress();
    const ernc::domain::ParameterRuntimeState* state = bench.store.find(anc);
    if (state == nullptr || !state->dirty || !state->stagedValue || cellAt(dialog, row, WritePlanColumn::Status) != QStringLiteral("失败")
        || cellAt(dialog, row, WritePlanColumn::Diagnostic) != QStringLiteral("响应超时，写入结果未知")
        || cellAt(dialog, row, WritePlanColumn::Set) != QStringLiteral("开") || frames.size() != 1U || countFunction(frames, 0x10) != 1
        || !dialog.diagnosticText().contains(QStringLiteral("读回设备值")) || !dialog.close() || dialog.isVisible()) {
        return fail("timeout_does_not_resend");
    }
    WritePlanDialog reopened(sourcesFor(catalog, bench.store, layout, "ERNC_Flag"));
    if (!reopened.confirmEnabled() || frames.size() != 1U) {
        return fail("timeout_does_not_resend");
    }
    return true;
}

/// @brief 让一条写排在无响应的读之后，验证桌面 Worker 能结束两条请求并释放批次。
/// @param catalog 正式目录，在本用例内借用。
/// @param layout 正式布局，在本用例内借用。
/// @return 各发送一次 FC03 和 FC16，写以 ResponseTimeout 结束且 Set 仍待写时为 true。
bool serialWorkerTimesOutQueuedWrite(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    SilentTransport transport;
    ParameterRuntimeStore store;
    ernc::domain::ArrayRuntimeStore arrayStore;
    ernc::presentation::DeviceRuntimeWorker worker(catalog, layout, store, arrayStore, &transport);
    const ParameterId anc = ParameterId::parse("ANC_OFF").value();
    store.stage(anc, ScalarValue::fromFloat(1.0F), std::chrono::system_clock::now());
    if (!worker.doConnect()) {
        return fail("serial_worker_times_out_queued_write: connect");
    }
    if (worker.startScalarRead(QStringLiteral("ANC_OFF")) != 0 || worker.startBatchWrite(QStringLiteral("ALL")) != 0
        || transport.submitted().size() != 1U || !worker.isWriteBusy()) {
        worker.doDisconnect();
        return fail("serial_worker_times_out_queued_write: enqueue");
    }

    // 两条命令各等待 1 秒，测试上限留出调度余量，也能在回归时有界退出。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    auto report = worker.doPump();
    while (!report.writeFinished && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
        report = worker.doPump();
    }
    const bool busy = worker.isWriteBusy();
    worker.doDisconnect();
    const auto& frames = transport.submitted();
    const auto* state = store.find(anc);
    if (!report.writeFinished || report.writeSuccess || report.writerBusy || report.writeInflight || busy
        || frames.size() != 2U || countFunction(frames, 0x03) != 1 || countFunction(frames, 0x10) != 1
        || state == nullptr || !state->dirty || !state->stagedValue || state->deviceValue
        || state->error != ernc::domain::ErrorCode::ResponseTimeout) {
        return fail("serial_worker_times_out_queued_write: result");
    }
    for (const auto& item : report.writeItems) {
        if (item.id == anc && item.kind == ernc::application::BatchWriteKind::Failed
            && item.error == ernc::domain::ErrorCode::ResponseTimeout) {
            return true;
        }
    }
    return fail("serial_worker_times_out_queued_write: missing item result");
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, const ParameterCatalog& catalog, const FirmwareLayout& layout, bool& recognized) {
    recognized = true;
    if (name == "flag_preview_before_write") {
        return flagPreviewBeforeWrite(catalog, layout);
    }
    if (name == "rnc_time_and_step") {
        return rncTimeAndStep(catalog, layout);
    }
    if (name == "all_page_includes_dirty") {
        return allPageIncludesDirty(catalog, layout);
    }
    if (name == "can_msg_is_not_sent") {
        return canMsgIsNotSent(catalog, layout);
    }
    if (name == "ack_then_verified") {
        return ackThenVerified(catalog, layout);
    }
    if (name == "mismatch_shows_diagnostic") {
        return mismatchShowsDiagnostic(catalog, layout);
    }
    if (name == "timeout_does_not_resend") {
        return timeoutDoesNotResend(catalog, layout);
    }
    if (name == "serial_worker_times_out_queued_write") {
        return serialWorkerTimesOutQueuedWrite(catalog, layout);
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个写入预览用例。
/// @param argc 参数个数，包含程序名。必须等于 4。
/// @param argv argv[1] 是用例名，argv[2] 是目录 JSON，argv[3] 是 layout manifest。三段文本只在本次进程内借用。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    if (argc != 4 || argv[1] == nullptr || argv[2] == nullptr || argv[3] == nullptr) {
        std::cerr << "usage: ernc_write_plan_dialog_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
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
