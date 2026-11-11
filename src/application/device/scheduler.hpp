#pragma once

#include "ernc/domain/types.hpp"
#include "ernc/ports/device_transport.hpp"
#include "ernc/protocol/wire_parser.hpp"
#include "retry_policy.hpp"
#include "session.hpp"
#include "write_verification.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <variant>
#include <vector>

namespace ernc::application {

/// 调度器里的一次读或写。读不使用 payload。
enum class CommandOp {
    /// FC03 读。响应只关联数据区，不是参数读回。
    Read,

    /// FC16 写。响应只关联 ACK 回显，不是读回 Verified。
    Write
};

/// 等待队列中的先后。High 不能越过已经交给端口的那一条。
enum class CommandPriority {
    /// 按入队顺序排在等待队列尾部。
    Normal,

    /// 插到等待中的 Normal 之前，同一优先级仍按先来后到。
    High
};

/// @brief 调用方交给调度器的一条命令。
///
/// 本对象持有 payload 的副本。它不包含 SessionId；调度器在入队时自行记录当前会话。
struct CommandRequest {
    /// 读还是写。
    CommandOp op{CommandOp::Read};

    /// 等待队列中的优先级。默认按 FIFO。
    CommandPriority priority{CommandPriority::Normal};

    /// 起始逻辑 slot。0 表示起始寄存器 0。
    std::uint16_t firstSlot{0};

    /// 连续 slot 数。0 和超过协议上限的值会在入队时被拒绝。
    std::uint16_t slotCount{0};

    /// 写命令的数据区。长度必须是 slotCount 乘 4。读命令忽略它。调用方拥有入队前的这份数据。
    std::vector<std::uint8_t> payload;

    /// 为 true 时，匹配 ACK 之后再读同一范围。默认 false，ACK 本身就结束命令。
    bool readBack{false};

    /// 写入请求帧的从站地址。默认是已确认的 0x01。
    std::uint8_t slaveAddress{0x01};
};

/// @brief 调度器分配的命令代号。
///
/// raw() 为 0 表示没有命令。同一个调度器不复用非 0 代号。它不是 SessionId。
class CommandId {
public:
    /// @brief 做成“没有命令”的代号。
    /// @return raw() 为 0，valid() 为 false。
    static constexpr CommandId none() noexcept { return CommandId{0}; }

    /// @brief 用既有数值做成命令代号。
    /// @param raw 无单位整数。0 与 none() 相同。非 0 不表示命令已经完成。
    /// @return 保存该数值的代号。
    static constexpr CommandId fromRaw(std::uint64_t raw) noexcept { return CommandId{raw}; }

    /// @brief 报告这个代号是不是一条命令。
    /// @return raw() 非 0 时为 true。
    constexpr bool valid() const noexcept { return raw_ != 0; }

    /// @brief 返回底层数值。
    /// @return 无单位整数。0 表示没有命令。
    constexpr std::uint64_t raw() const noexcept { return raw_; }

    /// @brief 两侧数值相同则为相等。
    constexpr bool operator==(const CommandId&) const = default;

private:
    /// @brief 保存调用方给出的数值。
    /// @param raw 无单位整数。0 表示没有命令。
    explicit constexpr CommandId(std::uint64_t raw) noexcept : raw_{raw} {}

    /// 命令代号，无单位。0 表示没有命令。
    std::uint64_t raw_;
};

/// @brief 一条读命令匹配到的数据区。
///
/// 字节保持线上的顺序。没有把它们写成参数的 deviceValue。
struct ReadCommandResult {
    /// 响应里的从站地址。它已经和请求比对过。
    std::uint8_t slaveAddress{0};

    /// 数据区字节。长度是 slotCount 乘 4。调用方在取走结果后拥有这份副本。
    std::vector<std::uint8_t> payload;

    /// payload 里的 slot 数。它等于入队时的 slotCount。
    std::uint16_t slotCount{0};
};

/// @brief 一条写命令匹配到的 FC16 回显。
///
/// 只有从站、起始寄存器和寄存器数量。这不是读回 Verified。
struct WriteCommandResult {
    /// ACK 中的从站地址。
    std::uint8_t slaveAddress{0};

    /// ACK 中的起始寄存器。它等于请求 slot 乘 2。
    std::uint16_t startRegister{0};

    /// ACK 中的寄存器数量。它等于请求 slot 数乘 2。
    std::uint16_t registerCount{0};
};

/// @brief 一条没有成功完成的命令。
struct FailedCommand {
    /// 失败类别。Unknown 出现在这里时也是一次失败。
    ernc::domain::ErrorCode code{ernc::domain::ErrorCode::Unknown};

    /// Modbus 异常码。只有 code 为 ModbusException 且响应里带了异常码时有值。
    std::optional<std::uint8_t> exceptionCode;
};

/// @brief 一条已经结束的命令及其所属会话。
///
/// body 恰好是读数据、写回显、读回对照或失败之一。调用方取走后拥有这份副本。
struct CommandOutcome {
    /// 入队时分配的命令代号。
    CommandId id{CommandId::none()};

    /// 入队时记录的会话代号。断连后它不再被会话接受。
    ernc::domain::SessionId session{ernc::domain::SessionId::none()};

    /// 读数据、写回显、读回对照或失败。
    std::variant<ReadCommandResult, WriteCommandResult, WriteVerification, FailedCommand> body{FailedCommand{}};
};

/// @brief 同一会话上串行提交命令，并只让一条留在端口上。
///
/// 持有会话和端口的引用，不拥有它们。二者必须是同一条端口，并且比本对象活得久。
/// 不创建线程，不加锁。调用必须和端口在同一个线程。
/// 等待队列可以有多条。已经 submit、尚未结束的命令最多一条。
/// readBack 为 false 时，写 ACK 不升级成 Verified。为 true 时，ACK 之后还要读回同一范围。
class CommandScheduler {
public:
    /// @brief 绑定会话和它使用的端口。
    /// @param session 会话引用，本对象不接管所有权。入队时读取它的状态和当前代号。
    /// @param transport 与 session 相同的那条端口，本对象不接管所有权。
    CommandScheduler(DeviceSession& session, ernc::ports::IDeviceTransport& transport);

    /// @brief 禁止复制。会话和端口引用只有一份。
    CommandScheduler(const CommandScheduler&) = delete;

    /// @brief 禁止复制赋值。
    CommandScheduler& operator=(const CommandScheduler&) = delete;

    /// @brief 禁止移动。引用成员不能改绑。
    CommandScheduler(CommandScheduler&&) = delete;

    /// @brief 禁止移动赋值。
    CommandScheduler& operator=(CommandScheduler&&) = delete;

    /// @brief 把一条命令放进等待队列；没有在途命令时立刻提交。
    /// @param request 读或写的范围。只在本次调用期间借用。payload 会被复制。
    /// @return 会话处于 Connected 或 Degraded 且范围合法时，结果里是新的 CommandId。
    /// 这只表示命令已入队或已交给端口，不是设备响应。
    /// 会话未连接时为 DeviceDisconnected。范围或 payload 不合法时带回编码函数的错误码，并且不占用在途名额。
    ernc::domain::Result<CommandId> enqueue(const CommandRequest& request);

    /// @brief 取走端口上已经到达的字节，结束在途命令，并在可以发送时提交下一条。
    ///
    /// 会话不再接受在途命令的代号时，该命令以 DeviceDisconnected 结束。
    /// 半帧留在解析器里，在途数量保持为 1。CRC 被解析器拒绝时，在途命令以 CrcMismatch 结束并且不重发。
    /// 这个重载不读取时钟，因此不会因为等待过久而重发或结束读命令。
    void pump();

    /// @brief 在取走已到达字节之后，按调用方给出的时刻处理在途读命令。
    ///
    /// 第一次对某条读调用时，把 sentAt 记成 now，并把已提交次数记成 1。同一次调用不会因为这个起点而超时。
    /// 写命令不使用 limit。完整读响应、CRC 失败和断连仍按无参 pump 的规则结束，不再重发。
    /// @param now 调用方给出的当前时刻。函数不读取系统钟。
    /// @param limit 这一次判断使用的等待长度和次数。0 值会让在途读以 InvalidParameter 结束。
    void pump(RetryTime now, const ReadRetryLimit& limit);

    /// @brief 已经交给端口、尚未结束的命令数量。
    /// @return 0 或 1。等待队列里的命令不算在内。
    std::size_t inFlightCount() const noexcept;

    /// @brief 查看一条要求读回的写命令现在的阶段。
    /// @param id 入队时返回的代号，按值传入。
    /// @return 命令仍在途或仍在等待，并且 readBack 为 true 时，返回 Sent、Acked 或 Verifying。
    /// 已经结束、不是读回写命令或代号未知时为空。
    std::optional<WritePhase> phaseOf(CommandId id) const;

    /// @brief 取走一条已经结束的命令结果。
    /// @param id 入队时返回的代号，按值传入。
    /// @return 已经结束时，调用方得到结果副本，调度器不再保留它。
    /// 仍在等待、仍在途或代号未知时为空。
    std::optional<CommandOutcome> takeResult(CommandId id);

private:
    /// 一条已接受、尚未结束的命令。wire 是编码后的整帧。
    struct PendingCommand {
        /// 本调度器分配的代号。
        CommandId id{CommandId::none()};

        /// 入队时的会话代号。
        ernc::domain::SessionId session{ernc::domain::SessionId::none()};

        /// 入队请求的副本。
        CommandRequest request{};

        /// 待提交或最近一次提交的长度前缀帧。读回开始后这里换成 FC03，不再保存 FC16。
        std::vector<std::uint8_t> wire;

        /// 要求读回的写命令阶段。普通读和未要求读回的写保持 Sent，不对外报告。
        WritePhase writePhase{WritePhase::Sent};

        /// 最近一次 submit 的时刻。attempt 为 0 时表示带时间的 pump 还没开始计时。
        RetryTime sentAt{};

        /// 当前这一帧已经 submit 的次数，含第一次。进入读回时清回 0，让读回单独计时。
        std::uint8_t attempt{0};
    };

    /// @brief 分配下一个不复用的命令代号。
    /// @return 数值还没用尽时为新代号。用尽时为空，调用方不得入队。
    std::optional<CommandId> allocateId();

    /// @brief 按优先级插入等待队列。
    /// @param command 已编码的命令。函数接管它。
    void enqueueWaiting(PendingCommand command);

    /// @brief 结束在途命令和等待队列里会话已不再接受的命令。
    void dropRejected();

    /// @brief 把当前 poll 能取到的字节送进解析器。
    void readAvailable();

    /// @brief 从解析器取出已经到齐的一帧或 CRC 失败。
    void consumeParser();

    /// @brief 把一帧完整响应关联到当前在途命令。
    /// @param frame 含长度前缀的线字节，只在本次调用期间借用。
    void matchFrame(std::span<const std::uint8_t> frame);

    /// @brief 在没有在途命令时提交等待队列中的下一条可发送命令。
    void startNext();

    /// @brief 对仍没有完整响应的在途读命令，或读回中的 FC03，应用重试判断。
    /// @param now 调用方给出的当前时刻。函数不读取系统钟。
    /// @param limit 等待长度和允许的 submit 次数。只在本次调用期间借用。
    void applyReadRetry(RetryTime now, const ReadRetryLimit& limit);

    /// @brief 对仍在等待 ACK 的读回写命令应用超时。超时不再次提交 FC16。
    /// @param now 调用方给出的当前时刻。函数不读取系统钟。
    /// @param limit 只使用其中的 timeout。maxAttempts 不使 FC16 重发。
    void applyWriteAckWait(RetryTime now, const ReadRetryLimit& limit);

    /// @brief 把已经 Acked 的写命令推进到 Verifying，并提交读回帧。
    void promoteAcked();

    /// @brief 把一条命令记成失败。
    /// @param command 要结束的命令，只在本次调用期间借用。
    /// @param code 失败类别。
    /// @param exceptionCode Modbus 异常码。没有异常码时为空。
    void completeFailure(const PendingCommand& command, ernc::domain::ErrorCode code,
                         std::optional<std::uint8_t> exceptionCode = std::nullopt);

    /// @brief 把当前在途命令记成失败并清掉在途槽。
    /// @param code 失败类别。
    /// @param exceptionCode Modbus 异常码。没有异常码时为空。
    void failInFlight(ernc::domain::ErrorCode code, std::optional<std::uint8_t> exceptionCode = std::nullopt);

    /// 调用方拥有的会话。
    DeviceSession& session_;

    /// 与 session_ 使用的同一条端口。
    ernc::ports::IDeviceTransport& transport_;

    /// 在途命令的流式拆帧器。断连或端口故障时清空。
    ernc::protocol::WireParser parser_;

    /// 尚未交给端口的命令。同一优先级按先来后到。
    std::deque<PendingCommand> waiting_;

    /// 已经 submit、尚未结束的那一条。空表示当前没有在途命令。
    std::optional<PendingCommand> inflight_;

    /// 已经结束、尚未被取走的结果。
    std::vector<CommandOutcome> finished_;

    /// 下一次分配的命令代号。从 1 开始。变为 0 表示数值已经用尽。
    std::uint64_t nextRaw_{1};
};

}  // namespace ernc::application
