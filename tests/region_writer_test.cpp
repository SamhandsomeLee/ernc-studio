#include "ernc/application/region_writer.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"
#include "ernc/protocol/crc16.hpp"

#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"
#include "faults/fault_sequence.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

using ernc::application::CommandId;
using ernc::application::CommandOutcome;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::ReadRetryLimit;
using ernc::application::RegionWritePreview;
using ernc::application::RegionWriteProgress;
using ernc::application::RegionWriteSnapshot;
using ernc::application::RegionWriter;
using ernc::application::RetryTime;
using ernc::application::WritePhase;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutEvidence;
using ernc::domain::LayoutLoad;
using ernc::domain::RegionCatalog;
using ernc::domain::Result;
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
    /// 实际回答 FC16 和 FC03 的从站端口。
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

/// @brief 已连接的写台。
struct Bench {
    /// 从站镜像。slot 0 在字节 0。
    SlaveImage image;

    /// 记录请求的端口。
    RecordingTransport transport;

    /// 与 transport 绑定的会话。
    DeviceSession session;

    /// 与 session 使用同一条端口的调度器。
    CommandScheduler scheduler;

    /// @brief 用足够覆盖写入范围的零镜像组装写台。
    /// @param slots 镜像 slot 数。
    explicit Bench(std::size_t slots)
        : image{std::vector<std::uint8_t>(slots * 4U, 0)}, transport{image}, session{transport}, scheduler{session, transport} {}
};

/// @brief 做成一块测试用的可写区域。起点是 slot 0。
/// @param path 区域路径。
/// @param slotCount 连续 slot 数。
/// @param writable 字段是否允许真实写入。
/// @return 证据等级为 Verified 的区域。调用方拥有。
ernc::domain::RegionDefinition writableRegion(std::string path, std::uint32_t slotCount, bool writable) {
    ernc::domain::RegionDefinition region;
    region.path = std::move(path);
    region.slot = 0;
    region.slotCount = slotCount;
    region.size = slotCount * 4U;
    region.evidence = LayoutEvidence::Verified;
    region.writable = writable;
    return region;
}

/// @brief 读出请求帧的功能码和寄存器数量。
/// @param frame 带长度前缀的请求。
/// @param function 期望的功能码。FC16 为 0x10，FC03 为 0x03。
/// @param registers 成功时写入大端寄存器数量。
/// @return 从站和功能码匹配时为 true。
bool registerCount(const std::vector<std::uint8_t>& frame, std::uint8_t function, std::uint16_t& registers) {
    if (frame.size() < 7U || frame[1] != 0x01 || frame[2] != function) {
        return false;
    }
    registers = static_cast<std::uint16_t>((static_cast<std::uint16_t>(frame[5]) << 8U) | frame[6]);
    return true;
}

/// @brief 统计某种功能码的帧数。
/// @param frames 已提交的请求。
/// @param function 功能码。
/// @return 匹配的帧数。
std::size_t countFunction(const std::vector<std::vector<std::uint8_t>>& frames, std::uint8_t function) {
    std::size_t count = 0;
    for (const std::vector<std::uint8_t>& frame : frames) {
        std::uint16_t registers = 0;
        if (registerCount(frame, function, registers)) {
            ++count;
        }
    }
    return count;
}

/// @brief 把已经结束的命令送进写对象，直到不再忙碌或次数用尽。
/// @param writer 区域写。
/// @param scheduler 与 writer 使用的同一个调度器。
/// @param name 用例名。
/// @return 不再忙碌时为 true。
bool drive(RegionWriter& writer, CommandScheduler& scheduler, const char* name) {
    for (int turn = 0; turn < 40 && writer.busy(); ++turn) {
        if (const std::optional<CommandId> id = writer.inFlight()) {
            if (std::optional<CommandOutcome> outcome = scheduler.takeResult(*id)) {
                writer.apply(*outcome);
                continue;
            }
        }
        scheduler.pump();
    }
    if (const std::optional<CommandId> id = writer.inFlight()) {
        if (std::optional<CommandOutcome> outcome = scheduler.takeResult(*id)) {
            writer.apply(*outcome);
        }
    }
    if (writer.busy()) {
        return fail(name);
    }
    return true;
}

/// @brief 正式目录里的 ERNC_TF 可以预览并开始基线写入。
/// @param manifestPath 正式 manifest。
/// @return commitPermitted 为 true 且 start 入队时为 true。
bool pack4PreviewCommits(const char* manifestPath) {
    constexpr const char* name = "pack4_preview_commits";
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail(name);
    }
    const Result<RegionCatalog> catalog = RegionCatalog::load(loaded.layout());
    if (!catalog.ok() || catalog.tryValue() == nullptr) {
        return fail(name);
    }
    const ernc::domain::RegionDefinition* transfer = catalog.value().find("ERNC_TF");
    if (transfer == nullptr || transfer->size == 0U) {
        return fail(name);
    }
    Bench bench{1};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionWriter writer{bench.session, bench.scheduler};
    const std::vector<std::uint8_t> bytes(transfer->size, 0);
    const Result<RegionWritePreview> preview = writer.preview(*transfer, bytes);
    const Result<void> started = writer.start(*transfer, bytes);
    if (!preview.ok() || preview.tryValue() == nullptr || !preview.value().commitPermitted || preview.value().chunks.empty()
        || !started.ok() || !writer.busy() || bench.transport.submitted().empty()) {
        return fail(name);
    }
    return true;
}

/// @brief 第一包 ACK 之后、读回之前，整块还不是 Verified。
/// @return 阶段是 Acked、已 Verified 包数为 0，随后读回才发布快照时为 true。
bool ackIsNotVerified() {
    constexpr const char* name = "ack_is_not_verified";
    Bench bench{10};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionWriter writer{bench.session, bench.scheduler};
    const ernc::domain::RegionDefinition region = writableRegion("lab.ack", 10, true);
    const std::vector<std::uint8_t> bytes(region.size, 0x3C);
    if (!writer.start(region, bytes).ok()) {
        return fail(name);
    }
    bench.scheduler.pump();
    const std::optional<CommandId> command = writer.inFlight();
    const std::optional<WritePhase> phase = command.has_value() ? bench.scheduler.phaseOf(*command) : std::nullopt;
    if (!writer.busy() || writer.progress().verifiedChunks != 0U || writer.snapshot() != nullptr || !phase.has_value()
        || *phase != WritePhase::Acked) {
        return fail(name);
    }
    if (!drive(writer, bench.scheduler, name)) {
        return false;
    }
    const RegionWriteSnapshot* snapshot = writer.snapshot();
    if (snapshot == nullptr || !snapshot->verified || snapshot->bytes != bytes || writer.progress().verifiedChunks != 1U) {
        return fail(name);
    }
    return true;
}

/// @brief 130 个 slot 写成 60、60、10，全部读回后快照与写入字节相同。
/// @return 三帧 FC16 的寄存器数量为 120、120、20 时为 true。
bool tailWriteIsVerified() {
    constexpr const char* name = "tail_write_is_verified";
    Bench bench{130};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionWriter writer{bench.session, bench.scheduler};
    const ernc::domain::RegionDefinition region = writableRegion("lab.tail", 130, true);
    std::vector<std::uint8_t> bytes(region.size, 0xA5);
    bytes.front() = 0x11;
    bytes.back() = 0x22;
    const Result<RegionWritePreview> preview = writer.preview(region, bytes);
    if (!preview.ok() || preview.tryValue() == nullptr || !preview.value().commitPermitted || preview.value().chunks.size() != 3U
        || preview.value().chunks[0].slotCount != 60U || preview.value().chunks[2].slotCount != 10U
        || !bench.transport.submitted().empty()) {
        return fail(name);
    }
    if (!writer.start(region, bytes).ok() || !drive(writer, bench.scheduler, name)) {
        return false;
    }
    std::vector<std::uint16_t> writes;
    for (const std::vector<std::uint8_t>& frame : bench.transport.submitted()) {
        std::uint16_t count = 0;
        if (registerCount(frame, 0x10, count)) {
            writes.push_back(count);
        }
    }
    const RegionWriteProgress progress = writer.progress();
    const RegionWriteSnapshot* snapshot = writer.snapshot();
    if (writes.size() != 3U || writes[0] != 120U || writes[1] != 120U || writes[2] != 20U || progress.verifiedChunks != 3U
        || progress.verifiedBytes != bytes.size() || progress.verifiedSlots != 130U || snapshot == nullptr || !snapshot->verified
        || snapshot->bytes != bytes) {
        return fail(name);
    }
    return true;
}

/// @brief 第二包 ACK 超时后保留第一包的已写区间，并且不重发 FC16。
/// @return 已 Verified 的 slot 数是 60、FC16 只有两帧时为 true。
bool timeoutKeepsVerifiedPrefix() {
    constexpr const char* name = "timeout_keeps_verified_prefix";
    ScriptedFaults faults{{FaultKind::None, FaultKind::None, FaultKind::Drop}};
    SlaveImage image{std::vector<std::uint8_t>(130U * 4U, 0)};
    RecordingTransport transport{image, &faults};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    if (!session.connect().ok()) {
        return fail(name);
    }
    RegionWriter writer{session, scheduler};
    const ernc::domain::RegionDefinition region = writableRegion("lab.timeout", 130, true);
    const std::vector<std::uint8_t> bytes(region.size, 0x44);
    if (!writer.start(region, bytes).ok()) {
        return fail(name);
    }
    scheduler.pump();
    scheduler.pump();
    if (const std::optional<CommandId> command = writer.inFlight()) {
        if (std::optional<CommandOutcome> outcome = scheduler.takeResult(*command)) {
            writer.apply(*outcome);
        }
    }
    if (writer.progress().verifiedChunks != 1U || !writer.busy()) {
        return fail(name);
    }
    const ReadRetryLimit once{std::chrono::milliseconds{100}, 1};
    const RetryTime start{std::chrono::milliseconds{0}};
    scheduler.pump(start, once);
    scheduler.pump(start + once.timeout, once);
    if (const std::optional<CommandId> command = writer.inFlight()) {
        if (std::optional<CommandOutcome> outcome = scheduler.takeResult(*command)) {
            writer.apply(*outcome);
        }
    }
    const RegionWriteProgress progress = writer.progress();
    if (writer.busy() || writer.snapshot() != nullptr || progress.verifiedChunks != 1U || progress.verifiedSlots != 60U
        || progress.verifiedBytes != 240U || progress.error != ErrorCode::ResponseTimeout || countFunction(transport.submitted(), 0x10) != 2U) {
        return fail(name);
    }
    return true;
}

/// @brief 第二包读回不一致时停住，已写区间只有第一包。
/// @return 没有第三帧 FC16，且整块快照为空时为 true。
bool mismatchStopsLaterChunks() {
    constexpr const char* name = "mismatch_stops_later_chunks";
    SlaveImage image{std::vector<std::uint8_t>(130U * 4U, 0)};
    PatchSecondReadback transport{image};
    DeviceSession session{transport};
    CommandScheduler scheduler{session, transport};
    if (!session.connect().ok()) {
        return fail(name);
    }
    RegionWriter writer{session, scheduler};
    const ernc::domain::RegionDefinition region = writableRegion("lab.mismatch", 130, true);
    const std::vector<std::uint8_t> bytes(region.size, 0x55);
    if (!writer.start(region, bytes).ok() || !drive(writer, scheduler, name)) {
        return false;
    }
    const RegionWriteProgress progress = writer.progress();
    if (writer.snapshot() != nullptr || progress.verifiedChunks != 1U || progress.verifiedSlots != 60U
        || progress.error != ErrorCode::ReadBackMismatch || countFunction(transport.submitted(), 0x10) != 2U) {
        return fail(name);
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。
/// @param manifestPath 正式 manifest。合成区域的用例不读取它。
/// @return 用例通过时为 true。
bool runCase(std::string_view name, const char* manifestPath) {
    if (name == "pack4_preview_commits") {
        return pack4PreviewCommits(manifestPath);
    }
    if (name == "ack_is_not_verified") {
        return ackIsNotVerified();
    }
    if (name == "tail_write_is_verified") {
        return tailWriteIsVerified();
    }
    if (name == "timeout_keeps_verified_prefix") {
        return timeoutKeepsVerifiedPrefix();
    }
    if (name == "mismatch_stops_later_chunks") {
        return mismatchStopsLaterChunks();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_region_writer_tests <case> <layout_manifest.json>\n";
        return 2;
    }
    return runCase(argv[1], argv[2]) ? 0 : 1;
}
