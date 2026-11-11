#include "ernc/application/region_reader.hpp"
#include "ernc/application/region_writer.hpp"
#include "ernc/application/transfer_cancellation.hpp"

#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

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
using ernc::application::RegionReader;
using ernc::application::RegionTransferKind;
using ernc::application::RegionTransferRecord;
using ernc::application::RegionWriter;
using ernc::domain::ErrorCode;
using ernc::domain::LayoutEvidence;
using ernc::domain::RegionDefinition;
using ernc::domain::Result;
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
    /// 实际回答请求的从站端口。
    MemorySlaveTransport inner_;

    /// 调用方提交过的整帧副本。
    std::vector<std::vector<std::uint8_t>> submitted_;
};

/// @brief 已连接的传输台。
struct Bench {
    /// 从站镜像。
    SlaveImage image;

    /// 记录请求的端口。
    RecordingTransport transport;

    /// 与 transport 绑定的会话。
    DeviceSession session;

    /// 与 session 使用同一条端口的调度器。
    CommandScheduler scheduler;

    /// @brief 用指定 slot 数的镜像组装传输台。
    /// @param slots 镜像 slot 数。
    /// @param fill 每个字节的初值。
    Bench(std::size_t slots, std::uint8_t fill)
        : image{std::vector<std::uint8_t>(slots * 4U, fill)}, transport{image}, session{transport}, scheduler{session, transport} {}
};

/// @brief 做成一块测试用的可读写区域。起点是 slot 0。
/// @param path 区域路径。
/// @param slotCount 连续 slot 数。
/// @return 证据等级为 Verified 且 writable 为 true 的区域。
RegionDefinition labRegion(std::string path, std::uint32_t slotCount) {
    RegionDefinition region;
    region.path = std::move(path);
    region.slot = 0;
    region.slotCount = slotCount;
    region.size = slotCount * 4U;
    region.evidence = LayoutEvidence::Verified;
    region.writable = true;
    return region;
}

/// @brief 统计某种功能码的帧数。
/// @param frames 已提交的请求。
/// @param function 功能码。
/// @return 匹配的帧数。
std::size_t countFunction(const std::vector<std::vector<std::uint8_t>>& frames, std::uint8_t function) {
    std::size_t count = 0;
    for (const std::vector<std::uint8_t>& frame : frames) {
        if (frame.size() >= 3U && frame[1] == 0x01 && frame[2] == function) {
            ++count;
        }
    }
    return count;
}

/// @brief 130 个 slot 的读在第一包结果应用前取消，第二包不再入队。
/// @return 前缀为 240 字节、快照为空、记录不能当完整 BIN 时为 true。
bool readCancelKeepsPrefix() {
    constexpr const char* name = "read_cancel_keeps_prefix";
    Bench bench{130, 0x11};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{bench.session, bench.scheduler};
    if (!reader.start(labRegion("lab.cancel-read", 130)).ok()) {
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
    reader.cancel();
    reader.apply(*outcome);
    const std::optional<RegionTransferRecord> record = ernc::application::noteCancelledRead(reader);
    if (reader.busy() || reader.snapshot() != nullptr || !record.has_value() || record->canSaveAsCompleteBin()
        || record->kind != RegionTransferKind::Read || record->deviceMayBePartial || record->diagnosis != ErrorCode::OperationCancelled
        || record->completedSlots != 60U || record->prefix.size() != 240U || record->prefix.front() != 0x11
        || reader.progress().filledChunks != 1U || countFunction(bench.transport.submitted(), 0x03) != 1U) {
        return fail(name);
    }
    return true;
}

/// @brief 130 个 slot 的写在第一包读回应用前取消，不再发第二包 FC16。
/// @return 已 Verified 前缀是 60 个 slot，记录说明设备可能只更新了这段时为 true。
bool writeCancelKeepsVerifiedPrefix() {
    constexpr const char* name = "write_cancel_keeps_verified_prefix";
    Bench bench{130, 0};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionWriter writer{bench.session, bench.scheduler};
    const RegionDefinition region = labRegion("lab.cancel-write", 130);
    const std::vector<std::uint8_t> bytes(region.size, 0x5A);
    if (!writer.start(region, bytes).ok()) {
        return fail(name);
    }
    bench.scheduler.pump();
    bench.scheduler.pump();
    const std::optional<CommandId> command = writer.inFlight();
    if (!command.has_value()) {
        return fail(name);
    }
    const std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(*command);
    if (!outcome.has_value()) {
        return fail(name);
    }
    writer.cancel();
    writer.apply(*outcome);
    const std::optional<RegionTransferRecord> record = ernc::application::noteCancelledWrite(writer);
    if (writer.busy() || writer.snapshot() != nullptr || !record.has_value() || record->canSaveAsCompleteBin()
        || record->kind != RegionTransferKind::Write || !record->deviceMayBePartial || record->diagnosis != ErrorCode::OperationCancelled
        || record->completedSlots != 60U || record->prefix.size() != 240U || record->prefix.front() != 0x5A
        || writer.progress().verifiedSlots != 60U || countFunction(bench.transport.submitted(), 0x10) != 1U) {
        return fail(name);
    }
    return true;
}

/// @brief 还没开始时取消不产生记录。
/// @return 读和写的记录都为空时为 true。
bool idleCancelHasNoRecord() {
    constexpr const char* name = "idle_cancel_has_no_record";
    Bench bench{4, 0};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{bench.session, bench.scheduler};
    RegionWriter writer{bench.session, bench.scheduler};
    reader.cancel();
    writer.cancel();
    if (ernc::application::noteCancelledRead(reader).has_value() || ernc::application::noteCancelledWrite(writer).has_value()
        || !bench.transport.submitted().empty()) {
        return fail(name);
    }
    return true;
}

/// @brief 已经读全之后取消，不拆掉完整快照。
/// @return 快照仍在，且没有取消记录时为 true。
bool finishedReadStaysComplete() {
    constexpr const char* name = "finished_read_stays_complete";
    Bench bench{10, 0x22};
    if (!bench.session.connect().ok()) {
        return fail(name);
    }
    RegionReader reader{bench.session, bench.scheduler};
    if (!reader.start(labRegion("lab.done", 10)).ok()) {
        return fail(name);
    }
    bench.scheduler.pump();
    if (const std::optional<CommandId> command = reader.inFlight()) {
        if (std::optional<CommandOutcome> outcome = bench.scheduler.takeResult(*command)) {
            reader.apply(*outcome);
        }
    }
    if (reader.snapshot() == nullptr || reader.busy()) {
        return fail(name);
    }
    reader.cancel();
    if (reader.snapshot() == nullptr || !reader.snapshot()->complete || ernc::application::noteCancelledRead(reader).has_value()) {
        return fail(name);
    }
    return true;
}

/// @brief 按用例名运行。
/// @param name 用例名。
/// @return 用例通过时为 true。
bool runCase(std::string_view name) {
    if (name == "read_cancel_keeps_prefix") {
        return readCancelKeepsPrefix();
    }
    if (name == "write_cancel_keeps_verified_prefix") {
        return writeCancelKeepsVerifiedPrefix();
    }
    if (name == "idle_cancel_has_no_record") {
        return idleCancelHasNoRecord();
    }
    if (name == "finished_read_stays_complete") {
        return finishedReadStaysComplete();
    }
    std::cerr << "unknown case: " << name << '\n';
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2 || argv[1] == nullptr) {
        std::cerr << "usage: ernc_region_cancel_tests <case>\n";
        return 2;
    }
    return runCase(argv[1]) ? 0 : 1;
}
