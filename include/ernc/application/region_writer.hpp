#pragma once

#include "ernc/domain/region_catalog.hpp"
#include "scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ernc::application {

/// 一次区域写现在处在哪一步。完成、失败和取消都不再忙碌。
enum class RegionWriteStatus {
    /// 还没有开始，或上一次开始在入队前就被拒绝。
    Idle,

    /// 还有一包在途，或读回对上后还要继续提交下一包。
    Running,

    /// 每一包的读回都和写入字节一致。整块区域是 Verified。
    Completed,

    /// 某一包失败后停住。已 Verified 的前缀不是整块 Verified。
    Failed,

    /// 调用方取消后停住。已 Verified 的前缀还在，但不能当整块 Verified。
    Cancelled
};

/// 预览里的一包。这是逻辑 slot 区间，不是已经发出的帧。
struct RegionWriteChunk {
    /// 这一包的起始逻辑 slot。
    std::uint32_t firstSlot{0};

    /// 这一包的 slot 数。1 到 61。默认计划里每包最多 60，最后一包可以更短。
    std::uint16_t slotCount{0};
};

/// @brief 写之前的范围。对象拥有路径和各包副本。
///
/// 生成预览不入队。commitPermitted 为 false 时，这次不能开始写。
struct RegionWritePreview {
    /// 区域路径。
    std::string path;

    /// 区域起始逻辑 slot。不是 Modbus 寄存器地址。
    std::uint32_t slot{0};

    /// 区域的 slot 数。
    std::uint32_t slotCount{0};

    /// 区域的字节长度。等于 slotCount 乘 4。
    std::uint32_t size{0};

    /// 按发送顺序排列的包。
    std::vector<RegionWriteChunk> chunks;

    /// 证据等级为 Verified 且 writable 为 true 时为 true。这不是已经发出的 FC16。
    bool commitPermitted{false};
};

/// @brief 一次区域写的进度。
///
/// 已 Verified 的区间从区域起点连续计算。失败包和还没读回的包不在里面。
/// 对象按值返回，调用方拥有这份副本。
struct RegionWriteProgress {
    /// 计划中的包数。
    std::size_t plannedChunks{0};

    /// 读回已经对上的包数。
    std::size_t verifiedChunks{0};

    /// 已 Verified 的字节数。
    std::size_t verifiedBytes{0};

    /// 区域起始逻辑 slot。
    std::uint32_t originSlot{0};

    /// 从起点连续 Verified 的 slot 数。等于 verifiedBytes 除以 4。
    std::uint32_t verifiedSlots{0};

    /// Failed 或 Cancelled 时的错误类别。其他状态为空。
    std::optional<ernc::domain::ErrorCode> error;
};

/// 整块区域都读回一致后的字节。对象拥有这份副本。
struct RegionWriteSnapshot {
    /// 区域路径。
    std::string path;

    /// 与写入字节相同的副本。长度等于区域的 size。
    std::vector<std::uint8_t> bytes;

    /// 为 true 时每一包都读回对上。ACK 本身不会让它变成 true。
    bool verified{false};
};

/// @brief 按写计划把一块区域写入设备，并等每一包读回。
///
/// 借用会话和调度器，不拥有它们。二者必须使用同一条端口，并且只在调度器所属线程上调用。
/// 不调用 poll()，不创建线程，也不是 QObject。不在这里组帧，也不解码 float。
/// preview 不入队。start 一次只提交下一包带读回的 FC16。
/// ACK 不是 Verified。超时不重发这一包。整块 Verified 只在每一包读回都一致时发布。
class RegionWriter {
public:
    /// @brief 绑定这次写要用的会话和调度器。
    /// @param session 设备会话，只借用。必须和 scheduler 使用同一条端口。
    /// @param scheduler 命令调度器，只借用。本对象通过它入队 FC16。
    RegionWriter(DeviceSession& session, CommandScheduler& scheduler);

    /// @brief 禁止复制。借用的会话只有一份。
    RegionWriter(const RegionWriter&) = delete;

    /// @brief 禁止复制赋值。
    RegionWriter& operator=(const RegionWriter&) = delete;

    /// @brief 禁止移动。引用成员不能改绑。
    RegionWriter(RegionWriter&&) = delete;

    /// @brief 禁止移动赋值。
    RegionWriter& operator=(RegionWriter&&) = delete;

    /// @brief 算出写范围，不提交命令。
    ///
    /// 字节长度必须等于区域的 size。写方向使用默认 60 个 slot，单包不超过 61。
    /// @param region 区域定义，只在本次调用期间借用。
    /// @param bytes 准备写入的原始字节，只在本次调用期间借用。
    /// @return 成功时调用方拥有预览。长度不符时为 InvalidParameter。
    ernc::domain::Result<RegionWritePreview> preview(const ernc::domain::RegionDefinition& region,
                                                     std::span<const std::uint8_t> bytes) const;

    /// @brief 入队第一包带读回的 FC16。
    ///
    /// 条件和 preview 相同，并且 realWritePermitted 必须为真。长度为 0 的可写区域成功并完成，不访问设备。
    /// @param region 区域定义，只在本次调用期间借用。
    /// @param bytes 准备写入的原始字节，只在本次调用期间借用。函数保存自己的副本。
    /// @return 计划已经建立时成功。这不是设备 ACK，也不是读回 Verified。
    /// 证据不足、不可写、长度不符或上一笔还没结束时为 InvalidParameter，不入队。
    /// 会话未连接时为 DeviceDisconnected。
    ernc::domain::Result<void> start(const ernc::domain::RegionDefinition& region, std::span<const std::uint8_t> bytes);

    /// @brief 把一条已经取走的写结果记到已 Verified 区间。
    ///
    /// 命令号不是当前包时不记账。已经 Verified 的命令再送进来时不把区间加长。
    /// ACK 回显本身不把这一包标成 Verified。读回不一致、超时或失败时停住，不再提交后续包，也不重发这一包 FC16。
    /// @param outcome 调度器取走的结果，只在本次调用期间借用。
    void apply(const CommandOutcome& outcome);

    /// @brief 当前是否还要处理这一包或提交下一包。
    /// @return 状态是 Running 时为 true。Sent、Acked 和 Verifying 都算在途。
    bool busy() const noexcept;

    /// @brief 当前进度的副本。
    /// @return 调用方拥有的计数。没有开始过时全是 0。
    RegionWriteProgress progress() const noexcept;

    /// @brief 当前已经入队、还没有最终结果的命令号。
    /// @return 有在途包时为该命令号。
    std::optional<CommandId> inFlight() const noexcept;

    /// @brief 借出整块都已 Verified 的快照。
    /// @return 全部读回对上时指向内部快照，不转移所有权。指针在下一次 start 或本对象销毁后失效。
    /// 还没写完、失败、取消或尚未开始时为空。
    const RegionWriteSnapshot* snapshot() const noexcept;

    /// 取消后保留的已 Verified 前缀。对象按值返回，调用方拥有字节副本。
    struct RegionWritePrefix {
        /// 区域路径。
        std::string path;

        /// 区域起始逻辑 slot。
        std::uint32_t originSlot{0};

        /// 从起点连续读回对上的 slot 数。
        std::uint32_t slots{0};

        /// 只覆盖这段前缀的写入字节。
        std::vector<std::uint8_t> bytes;
    };

    /// @brief 不再提交后续包。
    ///
    /// 不调用调度器的 cancel()，也不清掉已经 Verified 的字节。当前在途的那一包仍等它的真实结果。
    /// 已经整块 Verified、失败或尚未开始时不改变状态。
    void cancel() noexcept;

    /// @brief 借出取消后留下的已 Verified 前缀。
    /// @return 状态是 Cancelled 时，调用方拥有路径和前缀字节。其他状态为空。
    /// 这不是整块 Verified，不能当完整 BIN。设备上可能只更新了这段。
    std::optional<RegionWritePrefix> partial() const;

private:
    /// 计划中的一包。起始 slot 已经确认能放进 16 位。
    struct PlannedChunk {
        /// 这一包的起始逻辑 slot。
        std::uint16_t firstSlot{0};

        /// 这一包的 slot 数。1 到 61。
        std::uint16_t slotCount{0};

        /// 这一包的读回已经对上。
        bool verified{false};

        /// 写下这一包的命令号。还没 Verified 时为空。
        std::optional<CommandId> command;
    };

    /// 一笔未结束或刚结束的区域写。
    struct Active {
        /// 区域路径的副本。
        std::string path;

        /// 入队时的会话代号。
        ernc::domain::SessionId session{ernc::domain::SessionId::none()};

        /// 区域起点。字节切片从这里算。
        std::uint32_t origin{0};

        /// 按计划顺序排列的包。
        std::vector<PlannedChunk> chunks;

        /// 下一包在 chunks 中的下标。
        std::size_t next{0};

        /// 当前在途包的下标。
        std::size_t current{0};

        /// 已经入队、还没有最终结果的命令号。
        std::optional<CommandId> command;

        /// 为 true 时，当前包结束后不再提交后续包。
        bool cancel{false};

        /// 调用方交给 start 的写入字节。本对象拥有。
        std::vector<std::uint8_t> target;

        /// 读回已经对上的包数。
        std::size_t verifiedChunks{0};

        /// 已 Verified 的字节数。
        std::size_t verifiedBytes{0};

        /// 当前状态。
        RegionWriteStatus status{RegionWriteStatus::Idle};

        /// 失败时的错误类别。
        std::optional<ernc::domain::ErrorCode> error;

        /// 只在 Completed 时有值。
        std::optional<RegionWriteSnapshot> snapshot;
    };

    /// @brief 把区域切成写包。几何不符时失败，不产生半份计划。
    /// @param region 区域定义，只在本次调用期间借用。
    /// @param bytes 准备写入的字节，只在本次调用期间借用。
    /// @param chunks 成功时写入各包。
    /// @return 长度和每一包的范围都合法时成功。
    ernc::domain::Result<void> plan(const ernc::domain::RegionDefinition& region, std::span<const std::uint8_t> bytes,
                                    std::vector<PlannedChunk>& chunks) const;

    /// @brief 把当前下标的包交给调度器。
    /// @param chunkIndex chunks 中的下标。调用前已经确认没有越界。
    /// @return 命令已入队时成功。这不是设备 ACK，也不是读回 Verified。
    ernc::domain::Result<void> submitChunk(std::size_t chunkIndex);

    /// @brief 在当前包 Verified 后提交下一包；取消时留下已 Verified 的前缀。
    void advance();

    /// @brief 停住，不再提交后续包，也不发布整块 Verified。
    /// @param code 失败类别。
    void fail(ernc::domain::ErrorCode code);

    /// @brief 把这次写记成取消。已 Verified 的字节保留，整块快照不发布。
    void stopCancelled();

    /// 设备会话。本对象不拥有。
    DeviceSession& session_;

    /// 命令调度器。本对象不拥有。
    CommandScheduler& scheduler_;

    /// 当前这笔写。空表示还没有成功开始过。
    std::optional<Active> active_;
};

}  // namespace ernc::application
