#include "ernc/application/region_reader.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/region_catalog.hpp"

#include "faults/fault_sequence.hpp"
#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

namespace {

using ernc::application::CommandId;
using ernc::application::CommandOutcome;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::ReadRetryLimit;
using ernc::application::RegionReadProgress;
using ernc::application::RegionReader;
using ernc::application::RegionSnapshot;
using ernc::application::RetryTime;
using ernc::domain::ErrorCode;
using ernc::domain::FirmwareLayoutLoader;
using ernc::domain::LayoutEvidence;
using ernc::domain::LayoutLoad;
using ernc::domain::RegionCatalog;
using ernc::domain::RegionDefinition;
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
    /// 实际回答 FC03 的从站端口。
    MemorySlaveTransport inner_;

    /// 调用方提交过的整帧副本。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// @brief 已连接的读台。镜像、端口、会话和调度器的引用都指向本对象的成员。
struct Bench {
    /// 从站镜像。slot 0 在字节 0。
    SlaveImage image;

    /// 记录请求的端口。
    RecordingTransport transport;

    /// 与 transport 绑定的会话。
    DeviceSession session;

    /// 与 session 使用同一条端口的调度器。
    CommandScheduler scheduler;

    /// @brief 用初始字节和可选故障组装读台。
    /// @param bytes 从站初始字节。长度必须是 4 的倍数。
    /// @param faults 故障来源。为空时正常应答。必须比本对象活得久。
    Bench(std::vector<std::uint8_t> bytes, FaultSource* faults = nullptr)
        : image{std::move(bytes)}, transport{image, faults}, session{transport}, scheduler{session, transport} {}
};

/// @brief 做成一块测试用的已核验区域。起点是 slot 0。
/// @param path 区域路径。
/// @param slotCount 连续 slot 数。字节数是它乘 4。
/// @return 证据等级为 Verified 的区域。调用方拥有。
RegionDefinition verifiedRegion(std::string path, std::uint32_t slotCount) {
    RegionDefinition region;
    region.path = std::move(path);
    region.slot = 0;
    region.slotCount = slotCount;
    region.size = slotCount * 4U;
    region.evidence = LayoutEvidence::Verified;
    return region;
}

/// @brief 读出请求帧的功能码和寄存器数量。
/// @param frame 带长度前缀的请求。
/// @param function 期望的功能码。FC03 为 0x03。
/// @param registers 成功时写入大端寄存器数量。
/// @return 从站和功能码匹配时为 true。
bool registerCount(const std::vector<std::uint8_t>& frame, std::uint8_t function, std::uint16_t& registers) {
    if (frame.size() < 7U || frame[1] != 0x01 || frame[2] != function) {
        return false;
    }
    registers = static_cast<std::uint16_t>((static_cast<std::uint16_t>(frame[5]) << 8U) | frame[6]);
    return true;
}

/// @brief 把已经结束的命令送进读对象，直到不再忙碌或次数用尽。
/// @param reader 区域读。
/// @param scheduler 与 reader 使用的同一个调度器。
/// @param name 用例名。
/// @return 不再忙碌时为 true。
bool drive(RegionReader& reader, CommandScheduler& scheduler, const char* name) {
    for (int turn = 0; turn < 40 && reader.busy(); ++turn) {
        if (const std::optional<CommandId> id = reader.inFlight()) {
            if (std::optional<CommandOutcome> outcome = scheduler.takeResult(*id)) {
                reader.apply(*outcome);
                continue;
            }
        }
        scheduler.pump();
    }
    if (const std::optional<CommandId> id = reader.inFlight()) {
        if (std::optional<CommandOutcome> outcome = scheduler.takeResult(*id)) {
            reader.apply(*outcome);
        }
    }
    if (reader.busy()) {
        return fail(name);
    }
    return true;
}

/// @brief 正式目录里的 ERNC_TF 证据不足，不入队。
/// @param manifestPath 正式 manifest。
/// @return 错误码为 InvalidParameter 且没有请求时为 true。
bool pack4RegionDoesNotEnqueue(const char* manifestPath) {
    constexpr const char* name = "pack4_region_does_not_enqueue";
    const LayoutLoad loaded = FirmwareLayoutLoader::loadFile(manifestPath);
    if (!loaded.ok()) {
        return fail(name);
    }
    const Result<RegionCatalog> catalog = RegionCatalog::load(loaded.layout());
    if (!catalog.ok() || catalog.tryValue() == nullptr) {
        return fail(name);
    }
    const RegionDefinition* transfer = catalog.value().find("ERNC_TF");
    if (transfer == nullptr) {
        return fail(name);
    }
    Bench bench{std::vector<std::uint8_t>(4U, 0)};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{bench.session, bench.scheduler};
    const Result<void> started = reader.start(*transfer);
    if (started.ok() || started.tryError() == nullptr || *started.tryError() != ErrorCode::InvalidParameter || reader.busy()
        || reader.snapshot() != nullptr || !bench.transport.submitted().empty()) {
        return fail(name);
    }
    return true;
}

/// @brief 长度为 0 的已核验区域直接完成，不访问设备。
/// @return 快照完整且字节为空、提交数为 0 时为 true。
bool emptyRegionIsComplete() {
    constexpr const char* name = "empty_region_is_complete";
    Bench bench{std::vector<std::uint8_t>(4U, 0)};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{bench.session, bench.scheduler};
    const RegionDefinition region = verifiedRegion("lab.empty", 0);
    if (!reader.start(region).ok() || reader.busy()) {
        return fail(name);
    }
    const RegionReadProgress progress = reader.progress();
    const RegionSnapshot* snapshot = reader.snapshot();
    if (progress.plannedChunks != 0U || progress.filledChunks != 0U || progress.plannedBytes != 0U || progress.filledBytes != 0U
        || snapshot == nullptr || !snapshot->complete || !snapshot->bytes.empty() || snapshot->path != "lab.empty"
        || !bench.transport.submitted().empty()) {
        return fail(name);
    }
    return true;
}

/// @brief 130 个 slot 分成 60、60、10，快照与镜像字节相同。
/// @return 三帧寄存器数量为 120、120、20 且首尾字节保留时为 true。
bool tailSnapshotIsComplete() {
    constexpr const char* name = "tail_snapshot_is_complete";
    std::vector<std::uint8_t> bytes(130U * 4U, 0);
    bytes.front() = 0x11;
    bytes.back() = 0x22;
    Bench bench{bytes};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{bench.session, bench.scheduler};
    if (!reader.start(verifiedRegion("lab.tail", 130)).ok() || !drive(reader, bench.scheduler, name)) {
        return false;
    }
    std::vector<std::uint16_t> reads;
    for (const std::vector<std::uint8_t>& frame : bench.transport.submitted()) {
        std::uint16_t count = 0;
        if (registerCount(frame, 0x03, count)) {
            reads.push_back(count);
        }
    }
    const RegionReadProgress progress = reader.progress();
    const RegionSnapshot* snapshot = reader.snapshot();
    if (reads.size() != 3U || reads[0] != 120U || reads[1] != 120U || reads[2] != 20U || progress.plannedChunks != 3U
        || progress.filledChunks != 3U || progress.plannedBytes != bytes.size() || progress.filledBytes != bytes.size()
        || snapshot == nullptr || !snapshot->complete || snapshot->bytes != bytes) {
        return fail(name);
    }
    return true;
}

/// @brief 第二包被丢掉后不标完整，也不发送第三包。
/// @return 只收下 60 个 slot、提交两帧时为 true。
bool missingChunkIsNotComplete() {
    constexpr const char* name = "missing_chunk_is_not_complete";
    ScriptedFaults faults{{FaultKind::None, FaultKind::Drop}};
    Bench bench{std::vector<std::uint8_t>(130U * 4U, 0), &faults};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{bench.session, bench.scheduler};
    if (!reader.start(verifiedRegion("lab.missing", 130)).ok()) {
        return fail(name);
    }
    bench.scheduler.pump();
    if (const std::optional<CommandId> command = reader.inFlight()) {
        if (std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(*command)) {
            reader.apply(*outcome);
        }
    }
    const ReadRetryLimit once{std::chrono::milliseconds{100}, 1};
    const RetryTime start{std::chrono::milliseconds{0}};
    bench.scheduler.pump(start, once);
    bench.scheduler.pump(start + once.timeout, once);
    if (const std::optional<CommandId> command = reader.inFlight()) {
        if (std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(*command)) {
            reader.apply(*outcome);
        }
    }
    const RegionReadProgress progress = reader.progress();
    if (reader.busy() || reader.snapshot() != nullptr || progress.plannedChunks != 3U || progress.filledChunks != 1U
        || progress.filledBytes != 240U || progress.error != ErrorCode::ResponseTimeout || bench.transport.submitted().size() != 2U) {
        return fail(name);
    }
    return true;
}

/// @brief 同一条成功结果再送进来时，完成包数不加。
/// @return 10 个 slot 仍只计一包，快照字节不变时为 true。
bool duplicateResultDoesNotAdvance() {
    constexpr const char* name = "duplicate_result_does_not_advance";
    std::vector<std::uint8_t> bytes(10U * 4U, 0x5A);
    Bench bench{bytes};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{bench.session, bench.scheduler};
    if (!reader.start(verifiedRegion("lab.once", 10)).ok()) {
        return fail(name);
    }
    bench.scheduler.pump();
    const std::optional<CommandId> command = reader.inFlight();
    if (!command.has_value()) {
        return fail(name);
    }
    const std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(*command);
    if (!outcome.has_value()) {
        return fail(name);
    }
    reader.apply(*outcome);
    const RegionSnapshot* first = reader.snapshot();
    if (first == nullptr || reader.progress().filledChunks != 1U) {
        return fail(name);
    }
    const std::vector<std::uint8_t> saved = first->bytes;
    reader.apply(*outcome);
    const RegionSnapshot* second = reader.snapshot();
    if (reader.progress().filledChunks != 1U || second == nullptr || second->bytes != saved || second->bytes != bytes) {
        return fail(name);
    }
    return true;
}

/// @brief 不是当前包的命令号不写入。
/// @return 假命令之后仍能读全 10 个 slot 时为 true。
bool otherCommandIsIgnored() {
    constexpr const char* name = "other_command_is_ignored";
    Bench bench{std::vector<std::uint8_t>(10U * 4U, 0x7E)};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{bench.session, bench.scheduler};
    if (!reader.start(verifiedRegion("lab.other", 10)).ok()) {
        return fail(name);
    }
    CommandOutcome foreign;
    foreign.id = CommandId::fromRaw(999);
    foreign.session = bench.session.currentId();
    reader.apply(foreign);
    if (!reader.busy() || reader.progress().filledChunks != 0U || reader.snapshot() != nullptr) {
        return fail(name);
    }
    if (!drive(reader, bench.scheduler, name)) {
        return false;
    }
    const RegionSnapshot* snapshot = reader.snapshot();
    if (snapshot == nullptr || !snapshot->complete || snapshot->bytes.size() != 40U || snapshot->bytes.front() != 0x7E) {
        return fail(name);
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。
/// @param manifestPath 正式 manifest。合成区域的用例不读取它。
/// @return 用例通过时为 true。
bool runCase(std::string_view name, const char* manifestPath) {
    if (name == "pack4_region_does_not_enqueue") {
        return pack4RegionDoesNotEnqueue(manifestPath);
    }
    if (name == "empty_region_is_complete") {
        return emptyRegionIsComplete();
    }
    if (name == "tail_snapshot_is_complete") {
        return tailSnapshotIsComplete();
    }
    if (name == "missing_chunk_is_not_complete") {
        return missingChunkIsNotComplete();
    }
    if (name == "duplicate_result_does_not_advance") {
        return duplicateResultDoesNotAdvance();
    }
    if (name == "other_command_is_ignored") {
        return otherCommandIsIgnored();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_region_reader_tests <case> <layout_manifest.json>\n";
        return 2;
    }
    return runCase(argv[1], argv[2]) ? 0 : 1;
}
