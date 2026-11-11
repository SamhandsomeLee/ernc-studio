#include "scenario.hpp"

#include "faults/fault_apply.hpp"
#include "faults/fault_sequence.hpp"
#include "memory_transport.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "slave_image.hpp"

#include <cstdint>
#include <vector>

namespace ernc::lab {
namespace {

using ernc::application::CommandId;
using ernc::application::CommandOp;
using ernc::application::CommandRequest;
using ernc::application::CommandScheduler;
using ernc::application::DeviceSession;
using ernc::application::ReadCommandResult;
using ernc::application::WriteCommandResult;
using ernc::simulator::FaultKind;
using ernc::simulator::FaultReply;
using ernc::simulator::FaultSequence;
using ernc::simulator::MemorySlaveTransport;
using ernc::simulator::SlaveImage;

/// 内置一槽初始值。期望读回就是这四个字节，不由编码器生成。
constexpr std::uint8_t kPlanted[] = {0x78, 0x56, 0x34, 0x12};

/// bulk 每一轮写入的字面量。随后的读回必须等于它。
constexpr std::uint8_t kReplacement[] = {0x11, 0x22, 0x33, 0x44};

/// 读 slot 0 的一帧请求。长度和 CRC 都是字面量。
constexpr std::uint8_t kReadSlot0[] = {0x08, 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};

/// @brief 做成一槽从站镜像。
/// @return 初始内容为 kPlanted 的镜像。调用方拥有它。
SlaveImage plantedImage() {
    return SlaveImage{std::vector<std::uint8_t>(kPlanted, kPlanted + 4)};
}

/// @brief 把四字节写成十六进制。
/// @param bytes 恰好四个字节，只在本次调用期间借用。
/// @return 小写十六进制。长度不是 4 时为空。
std::string hex4(const std::uint8_t* bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    if (bytes == nullptr) {
        return {};
    }
    std::string text;
    text.resize(8);
    for (int index = 0; index < 4; ++index) {
        text[static_cast<std::size_t>(index * 2)] = kDigits[bytes[index] >> 4U];
        text[static_cast<std::size_t>(index * 2 + 1)] = kDigits[bytes[index] & 0x0FU];
    }
    return text;
}

/// @brief 两个四字节序列是否相同。
/// @param actual 读到的数据，只在本次调用期间借用。
/// @param expected 字面量期望。只在本次调用期间借用。
/// @return 长度都是 4 且逐字节相同时为 true。
bool same4(const std::vector<std::uint8_t>& actual, const std::uint8_t* expected) {
    return actual.size() == 4 && actual[0] == expected[0] && actual[1] == expected[1] &&
           actual[2] == expected[2] && actual[3] == expected[3];
}

/// @brief 在一块新镜像上读一个 slot。
/// @param expected 期望的四字节，只在本次调用期间借用。
/// @param stats 成功时增加 reads 和 passed，并记下 data。失败时增加 failed。
/// @return 读回与期望相同时为 true。
bool readSlot(const std::uint8_t* expected, ScenarioStats& stats) {
    SlaveImage image = plantedImage();
    MemorySlaveTransport port{image};
    DeviceSession session{port};
    const auto connected = session.connect();
    if (!connected.ok() || connected.tryError() != nullptr) {
        ++stats.failed;
        return false;
    }
    CommandScheduler scheduler{session, port};
    CommandRequest request;
    request.op = CommandOp::Read;
    request.firstSlot = 0;
    request.slotCount = 1;
    request.slaveAddress = 0x01;
    const auto enqueued = scheduler.enqueue(request);
    const CommandId* id = enqueued.tryValue();
    if (!enqueued.ok() || id == nullptr) {
        ++stats.failed;
        return false;
    }
    scheduler.pump();
    const auto outcome = scheduler.takeResult(*id);
    const ReadCommandResult* read = outcome.has_value() ? std::get_if<ReadCommandResult>(&outcome->body) : nullptr;
    if (read == nullptr || !same4(read->payload, expected)) {
        ++stats.failed;
        return false;
    }
    ++stats.reads;
    ++stats.passed;
    stats.data = hex4(expected);
    return true;
}

/// @brief 在同一会话里写入字面量后再读回。
/// @param stats 写入和读回分别计数。任一步不符则增加 failed。
/// @return 写入被接受且读回等于 kReplacement 时为 true。
bool writeThenRead(ScenarioStats& stats) {
    SlaveImage image = plantedImage();
    MemorySlaveTransport port{image};
    DeviceSession session{port};
    const auto connected = session.connect();
    if (!connected.ok() || connected.tryError() != nullptr) {
        ++stats.failed;
        return false;
    }
    CommandScheduler scheduler{session, port};
    CommandRequest write;
    write.op = CommandOp::Write;
    write.firstSlot = 0;
    write.slotCount = 1;
    write.slaveAddress = 0x01;
    write.readBack = false;
    write.payload.assign(kReplacement, kReplacement + 4);
    const auto written = scheduler.enqueue(write);
    const CommandId* writeId = written.tryValue();
    if (!written.ok() || writeId == nullptr) {
        ++stats.failed;
        return false;
    }
    scheduler.pump();
    const auto writeOutcome = scheduler.takeResult(*writeId);
    if (!writeOutcome.has_value() || std::get_if<WriteCommandResult>(&writeOutcome->body) == nullptr) {
        ++stats.failed;
        return false;
    }
    ++stats.writes;
    ++stats.passed;

    CommandRequest read;
    read.op = CommandOp::Read;
    read.firstSlot = 0;
    read.slotCount = 1;
    read.slaveAddress = 0x01;
    const auto readEnqueued = scheduler.enqueue(read);
    const CommandId* readId = readEnqueued.tryValue();
    if (!readEnqueued.ok() || readId == nullptr) {
        ++stats.failed;
        return false;
    }
    scheduler.pump();
    const auto readOutcome = scheduler.takeResult(*readId);
    const ReadCommandResult* payload =
        readOutcome.has_value() ? std::get_if<ReadCommandResult>(&readOutcome->body) : nullptr;
    if (payload == nullptr || !same4(payload->payload, kReplacement)) {
        ++stats.failed;
        return false;
    }
    ++stats.reads;
    ++stats.passed;
    stats.data = hex4(kReplacement);
    return true;
}

/// @brief 判断一帧正常读响应的数据区是不是内置四字节。
/// @param frame 带长度前缀的响应，只在本次调用期间借用。
/// @return 下标 4 起的四字节等于 kPlanted 时为 true。
bool plantedResponse(const std::vector<std::uint8_t>& frame) {
    return frame.size() >= 8 && frame[4] == kPlanted[0] && frame[5] == kPlanted[1] && frame[6] == kPlanted[2] &&
           frame[7] == kPlanted[3];
}

/// @brief 把一种故障计入对应计数。
/// @param kind 这次 applyFault 使用的种类。
/// @param stats 对应字段加一。
void countKind(FaultKind kind, ScenarioStats& stats) {
    switch (kind) {
    case FaultKind::None:
        ++stats.none;
        break;
    case FaultKind::Drop:
        ++stats.drop;
        break;
    case FaultKind::Delay:
        ++stats.delay;
        break;
    case FaultKind::BadFrame:
        ++stats.badFrame;
        break;
    case FaultKind::BadCrc:
        ++stats.badCrc;
        break;
    case FaultKind::Disconnect:
        ++stats.disconnect;
        break;
    }
}

/// @brief 跑一次固定读。
/// @param stats 成功时 reads、passed 和 data 被填写。
void runSmoke(ScenarioStats& stats) {
    stats.ok = readSlot(kPlanted, stats);
}

/// @brief 每一轮从初始镜像读、写、再读。
/// @param iterations 轮数，至少为 1。
/// @param stats 累计读写和失败。全部轮次成功时 ok 为 true。
void runBulk(int iterations, ScenarioStats& stats) {
    bool allOk = true;
    for (int round = 0; round < iterations; ++round) {
        if (!readSlot(kPlanted, stats) || !writeThenRead(stats)) {
            allOk = false;
        }
    }
    stats.ok = allOk;
}

/// @brief 按种子对同一条读请求施加故障并计数。
///
/// 不睡眠。Delay 直接查看暂存的正常响应。坏结果只计入 failed。
/// @param seed 传给 FaultSequence 的种子。
/// @param iterations 取故障并读一次的次数。
/// @param stats 故障计数。跑完即 ok 为 true。
void runFaults(std::uint32_t seed, int iterations, ScenarioStats& stats) {
    SlaveImage image = plantedImage();
    FaultSequence sequence{seed};
    for (int round = 0; round < iterations; ++round) {
        const FaultKind kind = sequence.next();
        const FaultReply reply = applyFault(image, kReadSlot0, kind);
        countKind(kind, stats);
        const bool delayedSuccess = kind == FaultKind::Delay && reply.delayPolls == 1 && plantedResponse(reply.frame);
        const bool immediateSuccess = reply.countsAsSuccess && plantedResponse(reply.frame);
        if (delayedSuccess || immediateSuccess) {
            ++stats.reads;
            ++stats.passed;
            stats.data = hex4(kPlanted);
            continue;
        }
        ++stats.failed;
    }
    stats.ok = true;
}

}  // namespace

ScenarioStats runScenario(const std::string& scenario, std::uint32_t seed, int iterations) {
    ScenarioStats stats;
    if (scenario == "smoke") {
        runSmoke(stats);
        return stats;
    }
    if (scenario == "bulk") {
        runBulk(iterations, stats);
        return stats;
    }
    if (scenario == "fault-injection") {
        runFaults(seed, iterations, stats);
        return stats;
    }
    return stats;
}

}  // namespace ernc::lab
