#pragma once

#include "ernc/domain/region_catalog.hpp"
#include "scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ernc::application {

/// 一次区域读现在处在哪一步。完成和失败都不再忙碌。
enum class RegionReadStatus {
    /// 还没有开始，或上一次开始在入队前就被拒绝。
    Idle,

    /// 还有一包在途，或成功后还要继续提交下一包。
    Running,

    /// 计划中的每一包都各成功了一次，快照可以发布。
    Completed,

    /// 某一包失败后停住。已写下的字节不算完整快照。
    Failed
};

/// @brief 一次区域读的进度。
///
/// 计数按包和字节分开。对象按值返回，调用方拥有这份副本。
/// 重复收下同一包、或命令号不是当前包时，这些计数不变。
struct RegionReadProgress {
    /// planSlotChunks 给出的包数。默认每包 60 个 slot，最后一包可以更短。
    std::size_t plannedChunks{0};

    /// 已经按 slot 位置写下、并且只计一次的包数。
    std::size_t filledChunks{0};

    /// 区域的字节长度。等于 slot 数乘 4。
    std::size_t plannedBytes{0};

    /// 已经写入缓冲的字节数。
    std::size_t filledBytes{0};

    /// Failed 时的错误类别。其他状态为空。
    std::optional<ernc::domain::ErrorCode> error;
};

/// 一块已经读全的区域字节。对象拥有这份副本。
struct RegionSnapshot {
    /// 区域路径。
    std::string path;

    /// 按 slot 顺序拼好的原始字节。长度等于区域的 size。
    std::vector<std::uint8_t> bytes;

    /// 为 true 时每一包都各到了一次。不完整的读不会发布这份快照。
    bool complete{false};
};

/// @brief 按默认 60 个 slot 把一块区域读成一份快照。
///
/// 借用会话和调度器，不拥有它们。二者必须使用同一条端口，并且只在调度器所属线程上调用。
/// 不调用 poll()，不创建线程，也不是 QObject。不在这里组帧，也不解码 float。
/// 一次只提交下一包 FC03。证据等级不是 Verified 时不入队。
/// 快照只在每一包都按自己的 slot 位置成功一次、且总字节数等于区域长度时发布。
class RegionReader {
public:
    /// @brief 绑定这次读要用的会话和调度器。
    /// @param session 设备会话，只借用。必须和 scheduler 使用同一条端口。
    /// @param scheduler 命令调度器，只借用。本对象通过它入队 FC03。
    RegionReader(DeviceSession& session, CommandScheduler& scheduler);

    /// @brief 禁止复制。借用的会话只有一份。
    RegionReader(const RegionReader&) = delete;

    /// @brief 禁止复制赋值。
    RegionReader& operator=(const RegionReader&) = delete;

    /// @brief 禁止移动。引用成员不能改绑。
    RegionReader(RegionReader&&) = delete;

    /// @brief 禁止移动赋值。
    RegionReader& operator=(RegionReader&&) = delete;

    /// @brief 为这块区域建立读计划，并入队第一包。
    ///
    /// 区域字节数必须等于 slot 数乘 4。realReadPermitted 必须为真。
    /// 长度为 0 的已核验区域成功并完成，不访问设备。
    /// @param region 目录中的区域，只在本次调用期间借用。地址是逻辑 slot。
    /// @return 计划已经建立时成功。这不是设备响应。
    /// 证据不足、长度不符或上一笔还没结束时为 InvalidParameter，不入队。
    /// 会话未连接时为 DeviceDisconnected。
    ernc::domain::Result<void> start(const ernc::domain::RegionDefinition& region);

    /// @brief 把一条已经取走的读结果按包的 slot 位置写入缓冲。
    ///
    /// 命令号不是当前包时不写。已经收下的命令再送进来时不覆盖，完成包数也不增加。
    /// 当前包失败、或字节数不等于该包 slot 数乘 4 时，停住并且不再提交后续包。
    /// @param outcome 调度器取走的结果，只在本次调用期间借用。
    void apply(const CommandOutcome& outcome);

    /// @brief 当前是否还要处理这一包或提交下一包。
    /// @return 状态是 Running 时为 true。
    bool busy() const noexcept;

    /// @brief 当前进度的副本。
    /// @return 调用方拥有的计数。没有开始过时全是 0。
    RegionReadProgress progress() const noexcept;

    /// @brief 当前已经入队、还没有最终结果的命令号。
    /// @return 有在途包时为该命令号。
    std::optional<CommandId> inFlight() const noexcept;

    /// @brief 借出已经读全的快照。
    /// @return 完整时指向内部快照，不转移所有权。指针在下一次 start 或本对象销毁后失效。
    /// 还没读全、失败或尚未开始时为空。
    const RegionSnapshot* snapshot() const noexcept;

private:
    /// 计划中的一包。起始 slot 已经确认能放进 16 位。
    struct PlannedChunk {
        /// 这一包的起始逻辑 slot。
        std::uint16_t firstSlot{0};

        /// 这一包的 slot 数。1 到 62。最后一包可以短于 60。
        std::uint16_t slotCount{0};

        /// 这一包已经成功写下一次。
        bool filled{false};

        /// 写下这一包的命令号。还没写下时为空。
        std::optional<CommandId> command;
    };

    /// 一笔未结束或刚结束的区域读。
    struct Active {
        /// 区域路径的副本。
        std::string path;

        /// 入队时的会话代号。
        ernc::domain::SessionId session{ernc::domain::SessionId::none()};

        /// 区域起点。后续包的字节偏移从这里算。
        std::uint32_t origin{0};

        /// 按计划顺序排列的包。
        std::vector<PlannedChunk> chunks;

        /// 下一包在 chunks 中的下标。
        std::size_t next{0};

        /// 当前在途包的下标。没有在途包时不使用。
        std::size_t current{0};

        /// 已经入队、还没有最终结果的命令号。
        std::optional<CommandId> command;

        /// 按 slot 顺序预留的字节。未写下的位置保持 0，不能据此当成完整。
        std::vector<std::uint8_t> buffer;

        /// 已经成功写下的包数。
        std::size_t filledChunks{0};

        /// 已经写入的字节数。
        std::size_t filledBytes{0};

        /// 当前状态。
        RegionReadStatus status{RegionReadStatus::Idle};

        /// 失败时的错误类别。
        std::optional<ernc::domain::ErrorCode> error;

        /// 只在 Completed 时有值。
        std::optional<RegionSnapshot> snapshot;
    };

    /// @brief 把当前下标的包交给调度器。
    /// @param chunkIndex chunks 中的下标。调用前已经确认没有越界。
    /// @return 命令已入队时成功。这不是设备响应。
    ernc::domain::Result<void> submitChunk(std::size_t chunkIndex);

    /// @brief 在当前包成功后提交下一包；队列空时发布快照。
    void advance();

    /// @brief 停住，不再提交后续包，也不发布快照。
    /// @param code 失败类别。
    void fail(ernc::domain::ErrorCode code);

    /// 设备会话。本对象不拥有。
    DeviceSession& session_;

    /// 命令调度器。本对象不拥有。
    CommandScheduler& scheduler_;

    /// 当前这笔读。空表示还没有成功开始过。
    std::optional<Active> active_;
};

}  // namespace ernc::application
