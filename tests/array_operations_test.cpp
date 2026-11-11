#include "ernc/application/array_operations.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/protocol/crc16.hpp"

#include "faults/fault_sequence.hpp"
#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ernc::application::ArrayTransfer;
using ernc::application::ArrayTransferProgress;
using ernc::application::ArrayTransferStatus;
using ernc::application::CommandId;
using ernc::application::CommandOutcome;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::ReadRetryLimit;
using ernc::application::RetryTime;
using ernc::domain::ArrayElement;
using ernc::domain::ArrayState;
using ernc::domain::CatalogLoad;
using ernc::domain::ErrorCode;
using ernc::domain::FieldLayout;
using ernc::domain::FirmwareLayout;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutLoad;
using ernc::domain::LayoutResolve;
using ernc::domain::ParameterCatalog;
using ernc::domain::ParameterCatalogLoader;
using ernc::domain::ParameterId;
using ernc::domain::Result;
using ernc::domain::ScalarValue;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;
using ernc::simulator::FaultKind;
using ernc::simulator::FaultSource;
using ernc::simulator::MemorySlaveTransport;
using ernc::simulator::SlaveImage;

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 按固定顺序给出故障。序列用完之后保持正常响应。
class ScriptedFaults final : public FaultSource {
public:
    /// @brief 保存要依次施加的故障。
    /// @param kinds 调用方给出的顺序。函数保存自己的副本。
    explicit ScriptedFaults(std::vector<FaultKind> kinds) : kinds_{std::move(kinds)} {}

    /// @brief 取出下一种故障。
    /// @return 序列里的下一种。用完后为 None。
    FaultKind next() override {
        if (index_ >= kinds_.size()) {
            return FaultKind::None;
        }
        return kinds_[index_++];
    }

private:
    /// 还没取走的故障顺序。
    std::vector<FaultKind> kinds_;

    /// 下一次 next 使用的下标。
    std::size_t index_{0};
};

/// @brief 记录提交帧并转交给内存从站的端口。
class RecordingTransport final : public IDeviceTransport {
public:
    /// @brief 绑定从站内存。可选故障只借用。
    /// @param image 从站内存，只借用。调用方保证它在本对象存活期间有效。
    /// @param faults 故障来源。为空时每帧都正常交换。调用方拥有它。
    explicit RecordingTransport(SlaveImage& image, FaultSource* faults = nullptr) : inner_{image, faults} {}

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
    /// 实际回答 FC03 和 FC16 的从站端口。
    MemorySlaveTransport inner_;

    /// 调用方提交过的整帧副本。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// @brief 第二帧 FC03 的第一个数据字节被改掉，并重新计算 CRC。
class PatchSecondReadback final : public IDeviceTransport {
public:
    /// @brief 绑定从站内存。
    /// @param image 从站内存，只借用。调用方保证它在本对象存活期间有效。
    explicit PatchSecondReadback(SlaveImage& image) : inner_{image} {}

    /// @brief 打开内部端口。
    /// @return 内部端口的打开结果。
    Result<void> open() override { return inner_.open(); }

    /// @brief 关闭内部端口。
    /// @return 内部端口的关闭结果。
    Result<void> close() override { return inner_.close(); }

    /// @brief 记下请求。第二帧 FC03 的响应会在 poll 时被改写。
    /// @param bytes 带长度前缀的请求，只在本次调用期间借用。
    /// @return 内部端口的提交结果。
    Result<void> submit(std::span<const std::uint8_t> bytes) override {
        submitted_.emplace_back(bytes.begin(), bytes.end());
        const bool isRead = bytes.size() > 2U && bytes[2] == 0x03;
        const Result<void> submitted = inner_.submit(bytes);
        if (submitted.ok() && isRead) {
            ++readSubmits_;
            if (readSubmits_ == 2) {
                patchNext_ = true;
            }
        }
        return submitted;
    }

    /// @brief 取走响应。需要改写时翻转第一个数据字节并重算 CRC。
    /// @return 可能被改写过的步骤。调用方拥有其中的字节。
    TransportStep poll() override {
        TransportStep step = inner_.poll();
        if (patchNext_ && step.kind == TransportStep::Kind::Bytes) {
            patchNext_ = false;
            patch(step.bytes);
        }
        return step;
    }

    /// @brief 按提交顺序借出已记录的请求。
    /// @return 内部向量的常量引用。
    const std::vector<std::vector<std::uint8_t>>& submitted() const noexcept { return submitted_; }

private:
    /// @brief 翻转 FC03 响应的第一个数据字节，并让 CRC 与新字节一致。
    /// @param frame 带长度前缀的响应。长度不够时不改。
    static void patch(std::vector<std::uint8_t>& frame) {
        if (frame.size() < 7U || frame[2] != 0x03 || frame.size() != static_cast<std::size_t>(frame[0]) + 1U || frame[0] < 5U) {
            return;
        }
        frame[4] ^= 0xFFU;
        const std::span<const std::uint8_t> covered{frame.data() + 1, static_cast<std::size_t>(frame[0]) - 2U};
        const std::vector<std::uint8_t> modbus = ernc::protocol::appendCrc16(covered);
        frame.clear();
        frame.push_back(static_cast<std::uint8_t>(modbus.size()));
        frame.insert(frame.end(), modbus.begin(), modbus.end());
    }

    /// 正常从站端口。
    MemorySlaveTransport inner_;

    /// 已经提交的 FC03 次数。第二帧读回要被改写。
    int readSubmits_{0};

    /// 下一次 Bytes 要改写。
    bool patchNext_{false};

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
    /// @return 成功。关闭不是设备 ACK。
    Result<void> close() override {
        open_ = false;
        return Result<void>::success();
    }

    /// @brief 记下请求，不产生响应字节。
    /// @param bytes 带长度前缀的请求，只在本次调用期间借用。
    /// @return 已打开且字节非空时成功。成功只表示字节已被收下。
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

    /// @brief 没有字节可交还。
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

/// @brief 已连接的传输台。镜像、端口、会话和调度器的引用都指向本对象的成员。
struct Bench {
    /// 从站镜像。slot 0 在字节 0。
    SlaveImage image;

    /// 记录请求的端口。
    RecordingTransport transport;

    /// 与 transport 绑定的会话。
    DeviceSession session;

    /// 与 session 使用同一条端口的调度器。
    CommandScheduler scheduler;

    /// @brief 用初始字节和可选故障组装传输台。
    /// @param bytes 从站初始字节。长度必须是 4 的倍数。
    /// @param faults 故障来源。为空时正常应答。必须比本对象活得久。
    Bench(std::vector<std::uint8_t> bytes, FaultSource* faults = nullptr)
        : image{std::move(bytes)}, transport{image, faults}, session{transport}, scheduler{session, transport} {}
};

/// @brief 取出 float 元素。
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

/// @brief 解析一个参数 ID。
/// @param text 标识文本。
/// @param name 用例名。
/// @param id 成功时写入解析结果。
/// @return 文本非空且解析成功时为 true。
bool parseId(std::string_view text, const char* name, ParameterId& id) {
    const Result<ParameterId> parsed = ParameterId::parse(text);
    if (!parsed.ok() || parsed.tryValue() == nullptr) {
        return fail(name);
    }
    id = *parsed.tryValue();
    return true;
}

/// @brief 按固件路径取出 locate 几何，并做成覆盖到字段末尾的零镜像。
/// @param layout 正式布局。
/// @param path 目录里的固件路径。
/// @param name 用例名。
/// @param field 成功时写入几何副本。
/// @param bytes 成功时写入镜像字节。调用方拥有。
/// @return 路径能 locate，且长度是 slot 数乘 4 时为 true。
bool imageFor(const FirmwareLayout& layout, std::string_view path, const char* name, FieldLayout& field,
              std::vector<std::uint8_t>& bytes) {
    const LayoutResolve located = layout.locate(path);
    if (!located.ok()) {
        return fail(name);
    }
    field = located.field();
    if (field.slotCount > (std::numeric_limits<std::size_t>::max() / 4U) - field.slot) {
        return fail(name);
    }
    bytes.assign(static_cast<std::size_t>(field.slot + field.slotCount) * 4U, 0);
    return true;
}

/// @brief 把一个 float 写成该 slot 的小端 4 字节。
/// @param bytes 镜像字节。长度必须盖住这个 slot。
/// @param slot 逻辑 slot。不是寄存器地址。
/// @param value 要写入的 float。不调用协议编码器。
/// @return 范围落在镜像内时为 true。
bool storeFloat(std::vector<std::uint8_t>& bytes, std::uint32_t slot, float value) {
    const std::size_t at = static_cast<std::size_t>(slot) * 4U;
    if (at + 4U > bytes.size()) {
        return false;
    }
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    bytes[at] = static_cast<std::uint8_t>(bits & 0xFFU);
    bytes[at + 1U] = static_cast<std::uint8_t>((bits >> 8U) & 0xFFU);
    bytes[at + 2U] = static_cast<std::uint8_t>((bits >> 16U) & 0xFFU);
    bytes[at + 3U] = static_cast<std::uint8_t>((bits >> 24U) & 0xFFU);
    return true;
}

/// @brief 读出请求帧的功能码和寄存器数量。
/// @param frame 带长度前缀的请求。
/// @param function 期望的功能码。FC03 为 0x03，FC16 为 0x10。
/// @param registers 成功时写入大端寄存器数量。
/// @return 从站和功能码匹配时为 true。
bool registerCount(const std::vector<std::uint8_t>& frame, std::uint8_t function, std::uint16_t& registers) {
    if (frame.size() < 7U || frame[1] != 0x01 || frame[2] != function) {
        return false;
    }
    registers = static_cast<std::uint16_t>((static_cast<std::uint16_t>(frame[5]) << 8U) | frame[6]);
    return true;
}

/// @brief 把已经结束的命令送进传输对象，直到不再忙碌或次数用尽。
/// @param transfer 数组传输。
/// @param scheduler 与 transfer 使用的同一个调度器。
/// @param name 用例名。
/// @return 不再忙碌时为 true。
bool drive(ArrayTransfer& transfer, CommandScheduler& scheduler, const char* name) {
    for (int turn = 0; turn < 80 && transfer.busy(); ++turn) {
        if (const std::optional<CommandId> id = transfer.inFlight()) {
            if (std::optional<CommandOutcome> outcome = scheduler.takeResult(*id)) {
                transfer.apply(*outcome);
                continue;
            }
        }
        scheduler.pump();
    }
    if (const std::optional<CommandId> id = transfer.inFlight()) {
        if (std::optional<CommandOutcome> outcome = scheduler.takeResult(*id)) {
            transfer.apply(*outcome);
        }
    }
    if (transfer.busy()) {
        return fail(name);
    }
    return true;
}

/// @brief Can_Msg 分成 60 和 12 两包，Set 和 dirty 保持不变。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 下标 61 的 Read 来自尾包，下标 2 的 Set 仍是 4 时为 true。
bool canMsgReadTailKeepsSet(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    constexpr const char* name = "can_msg_read_tail_keeps_set";
    FieldLayout field;
    std::vector<std::uint8_t> bytes;
    if (!imageFor(layout, "Can_Msg", name, field, bytes) || field.slotCount != 72U) {
        return fail(name);
    }
    if (!storeFloat(bytes, field.slot, 0.25F) || !storeFloat(bytes, field.slot + 61U, 7.5F)) {
        return fail(name);
    }
    Bench bench{std::move(bytes)};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    ParameterId id{ParameterId::parse("x").value()};
    if (!parseId("Can_Msg", name, id)) {
        return false;
    }
    ArrayState state{72, ScalarValue::Kind::Float32};
    if (!state.setValue(2, ScalarValue::fromFloat(4.0F)).ok()) {
        return fail(name);
    }
    ArrayTransfer transfer{catalog, layout, state, bench.session, bench.scheduler};
    const Result<void> started = transfer.read(id);
    const Result<void> second = transfer.read(id);
    if (!started.ok() || second.ok() || second.tryError() == nullptr || *second.tryError() != ErrorCode::InvalidParameter) {
        return fail(name);
    }
    if (!drive(transfer, bench.scheduler, name)) {
        return false;
    }
    const ArrayTransferProgress progress = transfer.progress();
    const ArrayElement* head = state.element(0);
    const ArrayElement* edited = state.element(2);
    const ArrayElement* tail = state.element(61);
    float headRead = 0.0F;
    float editedSet = 0.0F;
    float tailRead = 0.0F;
    std::uint16_t firstCount = 0;
    std::uint16_t secondCount = 0;
    const std::vector<std::vector<std::uint8_t>>& frames = bench.transport.submitted();
    if (progress.status != ArrayTransferStatus::Completed || progress.plannedElements != 72U || progress.completedElements != 72U
        || progress.plannedChunks != 2U || progress.completedChunks != 2U || progress.failedIndex.has_value() || progress.error.has_value()
        || head == nullptr || edited == nullptr || tail == nullptr || !head->read || edited->dirty == false || !edited->set || !tail->read
        || !asFloat(*head->read, headRead) || headRead != 0.25F || !asFloat(*edited->set, editedSet) || editedSet != 4.0F
        || !asFloat(*tail->read, tailRead) || tailRead != 7.5F || frames.size() != 2U
        || !registerCount(frames[0], 0x03, firstCount) || firstCount != 120U || !registerCount(frames[1], 0x03, secondCount)
        || secondCount != 24U) {
        return fail(name);
    }
    return true;
}

/// @brief 第二包被丢掉后停在下标 60，已经读入的前缀保留。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return failedIndex 为 60，且没有第三帧时为 true。
bool readStopsAtFailedIndex(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    constexpr const char* name = "read_stops_at_failed_index";
    FieldLayout field;
    std::vector<std::uint8_t> bytes;
    if (!imageFor(layout, "Can_Msg", name, field, bytes)) {
        return fail(name);
    }
    if (!storeFloat(bytes, field.slot, 0.25F)) {
        return fail(name);
    }
    ScriptedFaults faults{{FaultKind::None, FaultKind::Drop}};
    Bench bench{std::move(bytes), &faults};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    ParameterId id{ParameterId::parse("Can_Msg").value()};
    ArrayState state{72, ScalarValue::Kind::Float32};
    ArrayTransfer transfer{catalog, layout, state, bench.session, bench.scheduler};
    if (!transfer.read(id).ok()) {
        return fail(name);
    }
    bench.scheduler.pump();
    if (const std::optional<CommandId> command = transfer.inFlight()) {
        if (std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(*command)) {
            transfer.apply(*outcome);
        }
    }
    const ReadRetryLimit once{std::chrono::milliseconds{100}, 1};
    const RetryTime start{std::chrono::milliseconds{0}};
    bench.scheduler.pump(start, once);
    bench.scheduler.pump(start + once.timeout, once);
    if (const std::optional<CommandId> command = transfer.inFlight()) {
        if (std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(*command)) {
            transfer.apply(*outcome);
        }
    }
    const ArrayTransferProgress progress = transfer.progress();
    const ArrayElement* head = state.element(0);
    const ArrayElement* boundary = state.element(60);
    float headRead = 0.0F;
    if (transfer.busy() || progress.status != ArrayTransferStatus::Failed || progress.failedIndex != 60U
        || progress.error != ErrorCode::ResponseTimeout || progress.completedElements != 60U || progress.completedChunks != 1U
        || progress.plannedChunks != 2U || head == nullptr || !head->read || !asFloat(*head->read, headRead) || headRead != 0.25F
        || boundary == nullptr || boundary->read.has_value() || bench.transport.submitted().size() != 2U) {
        return fail(name);
    }
    return true;
}

/// @brief 第一包已经读完后取消，不再发送第二包。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 完成 60 个元素、状态为 Cancelled，且只有一帧时为 true。
bool cancelKeepsCompletedPrefix(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    constexpr const char* name = "cancel_keeps_completed_prefix";
    FieldLayout field;
    std::vector<std::uint8_t> bytes;
    if (!imageFor(layout, "Can_Msg", name, field, bytes)) {
        return fail(name);
    }
    Bench bench{std::move(bytes)};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    ParameterId id{ParameterId::parse("Can_Msg").value()};
    ArrayState state{72, ScalarValue::Kind::Float32};
    ArrayTransfer transfer{catalog, layout, state, bench.session, bench.scheduler};
    if (!transfer.read(id).ok()) {
        return fail(name);
    }
    bench.scheduler.pump();
    const std::optional<CommandId> command = transfer.inFlight();
    if (!command.has_value()) {
        return fail(name);
    }
    const std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(*command);
    if (!outcome.has_value()) {
        return fail(name);
    }
    transfer.cancel();
    transfer.apply(*outcome);
    const ArrayTransferProgress progress = transfer.progress();
    const ArrayElement* head = state.element(0);
    const ArrayElement* later = state.element(60);
    if (transfer.busy() || progress.status != ArrayTransferStatus::Cancelled || progress.error != ErrorCode::OperationCancelled
        || progress.completedElements != 60U || progress.completedChunks != 1U || progress.failedIndex.has_value() || head == nullptr
        || !head->read || later == nullptr || later->read.has_value() || bench.transport.submitted().size() != 1U) {
        return fail(name);
    }
    return true;
}

/// @brief 中间的干净元素把一次写拆成两包。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 下标 0 到 2 为一包，下标 5 单独一包，下标 4 没有 Read 时为 true。
bool dirtyGapSplitsWrite(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    constexpr const char* name = "dirty_gap_splits_write";
    FieldLayout field;
    std::vector<std::uint8_t> bytes;
    if (!imageFor(layout, "ENC_param.Amp1", name, field, bytes) || field.slotCount != 10U) {
        return fail(name);
    }
    Bench bench{std::move(bytes)};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    ParameterId id{ParameterId::parse("ENC_param.Amp1").value()};
    ArrayState state{10, ScalarValue::Kind::Float32};
    ArrayTransfer transfer{catalog, layout, state, bench.session, bench.scheduler};
    const Result<void> empty = transfer.write(id);
    const ArrayTransferProgress idle = transfer.progress();
    if (!empty.ok() || transfer.busy() || idle.status != ArrayTransferStatus::Completed || idle.plannedChunks != 0U
        || !bench.transport.submitted().empty()) {
        return fail(name);
    }
    if (!state.setValue(0, ScalarValue::fromFloat(1.0F)).ok() || !state.setValue(1, ScalarValue::fromFloat(2.0F)).ok()
        || !state.setValue(2, ScalarValue::fromFloat(3.0F)).ok() || !state.setValue(5, ScalarValue::fromFloat(9.0F)).ok()) {
        return fail(name);
    }
    if (!transfer.write(id).ok() || !drive(transfer, bench.scheduler, name)) {
        return false;
    }
    const ArrayElement* first = state.element(0);
    const ArrayElement* gap = state.element(4);
    const ArrayElement* last = state.element(5);
    float firstRead = 0.0F;
    float lastRead = 0.0F;
    std::uint16_t wide = 0;
    std::uint16_t narrow = 0;
    const std::vector<std::vector<std::uint8_t>>& frames = bench.transport.submitted();
    std::vector<std::uint16_t> writes;
    for (const std::vector<std::uint8_t>& frame : frames) {
        std::uint16_t count = 0;
        if (registerCount(frame, 0x10, count)) {
            writes.push_back(count);
        }
    }
    if (writes.size() != 2U) {
        return fail(name);
    }
    wide = writes[0];
    narrow = writes[1];
    const ArrayTransferProgress progress = transfer.progress();
    if (progress.status != ArrayTransferStatus::Completed || progress.plannedElements != 4U || progress.completedElements != 4U
        || progress.plannedChunks != 2U || first == nullptr || gap == nullptr || last == nullptr || first->dirty || !first->read
        || !asFloat(*first->read, firstRead) || firstRead != 1.0F || gap->read.has_value() || gap->dirty || last->dirty || !last->read
        || !asFloat(*last->read, lastRead) || lastRead != 9.0F || wide != 6U || narrow != 2U) {
        return fail(name);
    }
    return true;
}

/// @brief 250 个 dirty 元素的最后一包是 10 个 slot。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 五包 FC16 的寄存器数量依次是 120、120、120、120、20 时为 true。
bool hornWriteTailIsTenSlots(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    constexpr const char* name = "horn_write_tail_is_ten_slots";
    FieldLayout field;
    std::vector<std::uint8_t> bytes;
    if (!imageFor(layout, "check_data.HornRefCheck_F", name, field, bytes) || field.slotCount != 250U) {
        return fail(name);
    }
    Bench bench{std::move(bytes)};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    ParameterId id{ParameterId::parse("check_data.HornRefCheck_F").value()};
    ArrayState state{250, ScalarValue::Kind::Float32};
    for (std::size_t index = 0; index < state.size(); ++index) {
        if (!state.setValue(index, ScalarValue::fromFloat(1.5F)).ok()) {
            return fail(name);
        }
    }
    ArrayTransfer transfer{catalog, layout, state, bench.session, bench.scheduler};
    if (!transfer.write(id).ok() || !drive(transfer, bench.scheduler, name)) {
        return false;
    }
    std::vector<std::uint16_t> writes;
    for (const std::vector<std::uint8_t>& frame : bench.transport.submitted()) {
        std::uint16_t count = 0;
        if (registerCount(frame, 0x10, count)) {
            writes.push_back(count);
        }
    }
    const ArrayTransferProgress progress = transfer.progress();
    const ArrayElement* tail = state.element(249);
    float tailRead = 0.0F;
    if (progress.status != ArrayTransferStatus::Completed || progress.plannedElements != 250U || progress.completedElements != 250U
        || progress.plannedChunks != 5U || progress.completedChunks != 5U || progress.failedIndex.has_value() || writes.size() != 5U
        || writes[0] != 120U || writes[4] != 20U || tail == nullptr || tail->dirty || !tail->read || !asFloat(*tail->read, tailRead)
        || tailRead != 1.5F) {
        return fail(name);
    }
    return true;
}

/// @brief 第二包读回不一致时停住，已经对上的前缀清掉 dirty。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return failedIndex 为 60，且没有第三包 FC16 时为 true。
bool mismatchStopsLaterChunks(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    constexpr const char* name = "mismatch_stops_later_chunks";
    FieldLayout field;
    std::vector<std::uint8_t> bytes;
    if (!imageFor(layout, "ErrPowRNC", name, field, bytes) || field.slotCount != 120U) {
        return fail(name);
    }
    SlaveImage image{std::move(bytes)};
    PatchSecondReadback transport{image};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    if (!session.connect().ok()) {
        return fail(name);
    }
    ParameterId id{ParameterId::parse("ErrPowRNC").value()};
    ArrayState state{120, ScalarValue::Kind::Float32};
    for (std::size_t index = 0; index < state.size(); ++index) {
        if (!state.setValue(index, ScalarValue::fromFloat(2.0F)).ok()) {
            return fail(name);
        }
    }
    ArrayTransfer transfer{catalog, layout, state, session, scheduler};
    if (!transfer.write(id).ok() || !drive(transfer, scheduler, name)) {
        return false;
    }
    std::size_t writeFrames = 0;
    for (const std::vector<std::uint8_t>& frame : transport.submitted()) {
        std::uint16_t count = 0;
        if (registerCount(frame, 0x10, count)) {
            ++writeFrames;
        }
    }
    const ArrayTransferProgress progress = transfer.progress();
    const ArrayElement* done = state.element(59);
    const ArrayElement* failed = state.element(60);
    float doneRead = 0.0F;
    float failedRead = 0.0F;
    float failedSet = 0.0F;
    if (progress.status != ArrayTransferStatus::Failed || progress.error != ErrorCode::ReadBackMismatch || progress.failedIndex != 60U
        || progress.completedElements != 60U || progress.completedChunks != 1U || progress.plannedChunks != 2U || writeFrames != 2U
        || done == nullptr || failed == nullptr || done->dirty || !done->read || !asFloat(*done->read, doneRead) || doneRead != 2.0F
        || !failed->dirty || !failed->set || !failed->read || !asFloat(*failed->set, failedSet) || failedSet != 2.0F
        || !asFloat(*failed->read, failedRead) || failedRead == 2.0F) {
        return fail(name);
    }
    return true;
}

/// @brief ACK 超时后不重发 FC16，也不改数组。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 只提交一帧，下标 3 的 Set 仍是 8 且没有 Read 时为 true。
bool ackTimeoutDoesNotResend(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    constexpr const char* name = "ack_timeout_does_not_resend";
    SilentTransport transport;
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    if (!session.connect().ok()) {
        return fail(name);
    }
    ParameterId id{ParameterId::parse("ENC_param.Amp1").value()};
    ArrayState state{10, ScalarValue::Kind::Float32};
    if (!state.setValue(3, ScalarValue::fromFloat(8.0F)).ok()) {
        return fail(name);
    }
    ArrayTransfer transfer{catalog, layout, state, session, scheduler};
    if (!transfer.write(id).ok()) {
        return fail(name);
    }
    const ReadRetryLimit once{std::chrono::milliseconds{100}, 1};
    const RetryTime start{std::chrono::milliseconds{0}};
    scheduler.pump(start, once);
    scheduler.pump(start + once.timeout, once);
    if (const std::optional<CommandId> command = transfer.inFlight()) {
        if (std::optional<CommandOutcome> outcome = scheduler.takeResult(*command)) {
            transfer.apply(*outcome);
        }
    }
    const ArrayTransferProgress progress = transfer.progress();
    const ArrayElement* element = state.element(3);
    float set = 0.0F;
    std::uint16_t count = 0;
    if (transfer.busy() || progress.status != ArrayTransferStatus::Failed || progress.error != ErrorCode::ResponseTimeout
        || progress.failedIndex != 3U || element == nullptr || element->read.has_value() || !element->dirty || !element->set
        || !asFloat(*element->set, set) || set != 8.0F || transport.submitted().size() != 1U
        || !registerCount(transport.submitted()[0], 0x10, count) || count != 2U) {
        return fail(name);
    }
    return true;
}

/// @brief Can_Msg 和 Step 在入队前被拒绝。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 两者都不产生请求，Step 的错误是 ReadOnlyViolation 时为 true。
bool canMsgAndStepDoNotEnqueue(const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    constexpr const char* name = "can_msg_and_step_do_not_enqueue";
    Bench bench{std::vector<std::uint8_t>(4U, 0)};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    ParameterId stepId{ParameterId::parse("Step").value()};
    ArrayState stepState{1, ScalarValue::Kind::Float32};
    if (!stepState.setValue(0, ScalarValue::fromFloat(1.0F)).ok()) {
        return fail(name);
    }
    ArrayTransfer stepTransfer{catalog, layout, stepState, bench.session, bench.scheduler};
    const Result<void> step = stepTransfer.write(stepId);
    ParameterId canId{ParameterId::parse("Can_Msg").value()};
    ArrayState canState{72, ScalarValue::Kind::Float32};
    if (!canState.setValue(0, ScalarValue::fromFloat(1.0F)).ok()) {
        return fail(name);
    }
    ArrayTransfer canTransfer{catalog, layout, canState, bench.session, bench.scheduler};
    const Result<void> can = canTransfer.write(canId);
    if (step.ok() || step.tryError() == nullptr || *step.tryError() != ErrorCode::ReadOnlyViolation || can.ok()
        || can.tryError() == nullptr || *can.tryError() != ErrorCode::InvalidParameter || !bench.transport.submitted().empty()) {
        return fail(name);
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。
/// @param catalog 正式目录。
/// @param layout 正式布局。
/// @return 用例通过时为 true。未知用例为 false。
bool runCase(std::string_view name, const ParameterCatalog& catalog, const FirmwareLayout& layout) {
    if (name == "can_msg_read_tail_keeps_set") {
        return canMsgReadTailKeepsSet(catalog, layout);
    }
    if (name == "read_stops_at_failed_index") {
        return readStopsAtFailedIndex(catalog, layout);
    }
    if (name == "cancel_keeps_completed_prefix") {
        return cancelKeepsCompletedPrefix(catalog, layout);
    }
    if (name == "dirty_gap_splits_write") {
        return dirtyGapSplitsWrite(catalog, layout);
    }
    if (name == "horn_write_tail_is_ten_slots") {
        return hornWriteTailIsTenSlots(catalog, layout);
    }
    if (name == "mismatch_stops_later_chunks") {
        return mismatchStopsLaterChunks(catalog, layout);
    }
    if (name == "ack_timeout_does_not_resend") {
        return ackTimeoutDoesNotResend(catalog, layout);
    }
    if (name == "can_msg_and_step_do_not_enqueue") {
        return canMsgAndStepDoNotEnqueue(catalog, layout);
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 4 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_array_operations_tests <case> <parameter_catalog.json> <layout_manifest.json>\n";
        return 2;
    }
    const CatalogLoad catalog = ParameterCatalogLoader::loadFile(argv[2]);
    const LayoutLoad layout = FirmwareLayoutLoader::loadFile(argv[3]);
    if (!catalog.ok() || !layout.ok()) {
        std::cerr << "failed to load catalog or layout\n";
        return 2;
    }
    return runCase(argv[1], catalog.catalog(), layout.layout()) ? 0 : 1;
}
