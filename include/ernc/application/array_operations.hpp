#pragma once

#include "ernc/domain/array_state.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "scheduler.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ernc::application {

/// 一次数组传输现在处在哪一步。完成、失败和取消都不再忙碌。
enum class ArrayTransferStatus {
    /// 还没有开始，或上一次开始在入队前就被拒绝。
    Idle,

    /// 还有一包在途，或成功后还要继续提交下一包。
    Running,

    /// 计划中的每一包都已经按真实结果收完。0 包也是完成。
    Completed,

    /// 某一包失败后停住。failedIndex 是第一个没有完成的元素下标。
    Failed,

    /// 调用方取消后不再提交后续包。已经完成的元素保留。
    Cancelled
};

/// @brief 一次数组读或写的进度。
///
/// 计数按元素和包分开。对象按值返回，调用方拥有这份副本。
/// failedIndex 是数组里的元素下标，从 0 开始，不是 Modbus 寄存器地址。
struct ArrayTransferProgress {
    /// 这次计划要处理的元素个数。读是整个数组。写只计 dirty 元素。
    std::size_t plannedElements{0};

    /// 已经读入或读回对上的元素个数。失败包里对不上的元素不计。
    std::size_t completedElements{0};

    /// 计划中的包数。最后一包可以短于 60 个 slot。
    std::size_t plannedChunks{0};

    /// 已经完整成功的包数。失败的那一包不计。
    std::size_t completedChunks{0};

    /// 第一个没有完成的元素下标。还没有失败时为空。
    std::optional<std::size_t> failedIndex;

    /// 当前状态。Idle 不是设备成功。
    ArrayTransferStatus status{ArrayTransferStatus::Idle};

    /// Failed 或 Cancelled 时的错误类别。完成时为空。
    std::optional<ernc::domain::ErrorCode> error;
};

/// @brief 把一个同质数组按块读入或写出。
///
/// 借用目录、布局、数组、会话和调度器，不拥有它们。这些对象必须比本对象活得久，并且只在调度器所属线程上调用。
/// 不调用 poll()，不创建线程，也不是 QObject。不在这里组 Modbus 帧。
/// 读和写都用 planSlotChunks 的默认 60 个 slot。一次只把下一包交给调度器。
/// 读成功只调用 setRead，不改 Set、dirty，也不写 RuntimeStore。
/// 写只发送 dirty 且带 Set 的连续下标。读回字节一致后 setRead，并只对那些下标 markClean。
/// 超时、取消和异常都不重发已经提交的 FC16。
class ArrayTransfer {
public:
    /// @brief 绑定这次传输要用的目录、布局、数组、会话和调度器。
    /// @param catalog 参数目录，只借用。
    /// @param layout 固件布局，只借用。地址来自 locate()。
    /// @param state 元素的 Read、Set 和 dirty，只借用。调用方保证它在本对象存活期间有效。
    /// @param session 设备会话，只借用。必须和 scheduler 使用同一条端口。
    /// @param scheduler 命令调度器，只借用。本对象通过它入队，不用它的 cancel()。
    ArrayTransfer(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                  ernc::domain::ArrayState& state, DeviceSession& session, CommandScheduler& scheduler);

    /// @brief 禁止复制。借用的数组只有一份。
    ArrayTransfer(const ArrayTransfer&) = delete;

    /// @brief 禁止复制赋值。
    ArrayTransfer& operator=(const ArrayTransfer&) = delete;

    /// @brief 禁止移动。引用成员不能改绑。
    ArrayTransfer(ArrayTransfer&&) = delete;

    /// @brief 禁止移动赋值。
    ArrayTransfer& operator=(ArrayTransfer&&) = delete;

    /// @brief 按参数 ID 入队第一包 FC03。
    ///
    /// 数组长度必须等于 locate() 的 slot 数，种类必须和布局标量一致。
    /// 没有元素标量类型、上一笔还没结束，或起点放不进 16 位 slot 时不入队。
    /// 长度为 0 的数组成功并完成，不访问设备。
    /// @param id 参数标识，只在本次调用期间借用。
    /// @return 计划已经建立时成功。这不是设备响应。
    /// 会话未连接时为 DeviceDisconnected。几何或种类不符时为 InvalidParameter。
    ernc::domain::Result<void> read(const ernc::domain::ParameterId& id);

    /// @brief 把 dirty 元素按连续区间入队第一包带读回的 FC16。
    ///
    /// 没有任何 dirty 元素时成功并完成，不访问设备。
    /// Can_Msg、Step 和 RNC_refresh 在入队前拒绝。中间的干净元素会把写入拆开，避免写进同一帧。
    /// @param id 参数标识，只在本次调用期间借用。它的几何必须和 state 一致。
    /// @return 计划已经建立时成功。这不是设备 ACK，也不是读回 Verified。
    /// 只读参数为 ReadOnlyViolation。Can_Msg、命令参数和几何不符为 InvalidParameter。
    /// 会话未连接时为 DeviceDisconnected。
    ernc::domain::Result<void> write(const ernc::domain::ParameterId& id);

    /// @brief 把一条已经取走的命令结果记到数组，并在可以继续时提交下一包。
    ///
    /// 命令号不属于当前包，或会话不接受该 SessionId 时，不把结果当成 Verified。
    /// 会话拒绝时不再提交后续包。读成功只改对应下标的 Read。
    /// 写读回一致时更新 Read 并清掉这些下标的 dirty，Set 保留。
    /// 读回不一致时把 Read 改成设备值，dirty 和 Set 保留，并停在第一个对不上的下标。
    /// @param outcome 调度器取走的结果，只在本次调用期间借用。
    void apply(const CommandOutcome& outcome);

    /// @brief 不再提交后续包。
    ///
    /// 不调用调度器的 cancel()，也不清空数组。已经在途的那一包仍等它的真实结果。
    /// 当前没有在途传输时不改变状态。
    void cancel() noexcept;

    /// @brief 当前是否还要处理命令结果或提交下一包。
    /// @return 状态是 Running 时为 true。完成、失败、取消和空闲都是 false。
    bool busy() const noexcept;

    /// @brief 当前进度的副本。
    /// @return 调用方拥有的计数。没有开始过时状态是 Idle。
    ArrayTransferProgress progress() const noexcept;

    /// @brief 当前已经入队、还没有最终结果的命令号。
    /// @return 有在途包时为该命令号。还没提交的后续包不在这里。
    std::optional<CommandId> inFlight() const noexcept;

private:
    /// 计划中的一包。下标是数组元素，不是寄存器地址。
    struct PlannedChunk {
        /// 这一包的起始逻辑 slot。已经确认能放进 uint16。
        std::uint16_t firstSlot{0};

        /// 这一包的 slot 数。1 到该方向的上限。最后一包可以更短。
        std::uint16_t slotCount{0};

        /// 这一包第一个元素在数组里的下标。
        std::size_t firstIndex{0};
    };

    /// 一笔未结束或刚结束的传输。对象拥有参数标识和包计划。
    struct Active {
        /// 参数标识的副本。
        ernc::domain::ParameterId id;

        /// 开始时的会话代号。
        ernc::domain::SessionId session{ernc::domain::SessionId::none()};

        /// 为 true 时这一笔是 FC03。为 false 时是带读回的 FC16。
        bool reading{true};

        /// 元素种类。float、int32、uint32 不混用。
        ernc::domain::ScalarValue::Kind kind{ernc::domain::ScalarValue::Kind::Float32};

        /// 按发送顺序排列的包。
        std::vector<PlannedChunk> chunks;

        /// 下一包在 chunks 中的下标。等于 size() 表示已经没有后续包。
        std::size_t next{0};

        /// 当前在途包在 chunks 中的下标。没有在途包时不使用。
        std::size_t current{0};

        /// 已经入队、还没有最终结果的命令号。空表示当前没有在途包。
        std::optional<CommandId> command;

        /// 计划处理的元素个数。
        std::size_t plannedElements{0};

        /// 已经完成的元素个数。
        std::size_t completedElements{0};

        /// 已经完整成功的包数。
        std::size_t completedChunks{0};

        /// 第一个没有完成的元素下标。
        std::optional<std::size_t> failedIndex;

        /// 失败或取消时的错误类别。
        std::optional<ernc::domain::ErrorCode> error;

        /// 当前状态。
        ArrayTransferStatus status{ArrayTransferStatus::Idle};

        /// 为 true 时，当前包结束后不再提交后续包。
        bool cancel{false};
    };

    /// @brief 检查目录和布局，确认 state 就是这一段同质数组。
    /// @param id 参数标识，只在本次调用期间借用。
    /// @param field 成功时写入 locate() 的几何副本。
    /// @param definition 成功时指向目录记录，不转移所有权。
    /// @return 几何、种类和长度都一致时成功。否则为 InvalidParameter，不入队。
    ernc::domain::Result<void> prepare(const ernc::domain::ParameterId& id, ernc::domain::FieldLayout& field,
                                       const ernc::domain::ParameterDefinition*& definition) const;

    /// @brief 判断这条路径是不是 Can_Msg 整段或它的子路径。
    /// @param path 目录中的固件路径，只在本次调用期间借用。
    /// @return 路径等于 Can_Msg，或以 Can_Msg. 开头时为 true。
    static bool isCanMsg(std::string_view path) noexcept;

    /// @brief 判断这个参数能不能进入数组写。
    /// @param definition 目录记录，只在本次调用期间借用。
    /// @return 可以写时为空。只读为 ReadOnlyViolation。Can_Msg、命令和区域为 InvalidParameter。
    std::optional<ernc::domain::ErrorCode> writeRefusal(const ernc::domain::ParameterDefinition& definition) const;

    /// @brief 把一包放进调度器。
    /// @param chunkIndex chunks 中的下标。调用前已经确认没有越界。
    /// @return 命令已入队时成功。这不是设备响应。失败时不改数组。
    ernc::domain::Result<void> submitChunk(std::size_t chunkIndex);

    /// @brief 在当前包成功或开始时提交下一包；取消或队列空时结束这笔传输。
    void advance();

    /// @brief 停在一个元素下标上，不再提交后续包。
    /// @param index 第一个没有完成的元素下标。
    /// @param code 失败类别。
    void failAt(std::size_t index, ernc::domain::ErrorCode code);

    /// @brief 把读到的标量写入对应 Read。
    /// @param chunk 当前包，只在本次调用期间借用。
    /// @param values 按元素顺序排列的标量，只在本次调用期间借用。个数必须等于包的 slot 数。
    /// @param clean 为 true 时，每个成功写入的下标再 markClean。Set 不变。
    /// @return 全部写入成功时为 true。中途失败时已经停在 failAt。
    bool storeValues(const PlannedChunk& chunk, const std::vector<ernc::domain::ScalarValue>& values, bool clean);

    /// @brief 把一段连续 slot 追加进包计划。
    /// @param firstSlot 这段的起始逻辑 slot。
    /// @param slotCount 这段的 slot 数。0 追加出空计划并成功。
    /// @param writing 为 true 时按写上限切包。为 false 时按读上限切包。
    /// @param originSlot 数组元素 0 对应的逻辑 slot。用来把包起点换回元素下标。
    /// @param chunks 成功时在尾部追加。失败时不保证只追加了一部分；调用方应丢掉它。
    /// @return 每个包的起点都能放进 16 位 slot，且没有越出数组时成功。
    ernc::domain::Result<void> appendChunks(std::uint32_t firstSlot, std::uint32_t slotCount, bool writing,
                                            std::uint32_t originSlot, std::vector<PlannedChunk>& chunks) const;

    /// @brief 记下这笔传输并提交第一包。空计划直接完成。
    /// @param id 参数标识，按值复制。
    /// @param reading 为 true 时后续包是 FC03。
    /// @param kind 数组种类。
    /// @param chunks 已经切好的包。函数接管这份向量。
    /// @param plannedElements 这次计划处理的元素个数。
    void begin(const ernc::domain::ParameterId& id, bool reading, ernc::domain::ScalarValue::Kind kind,
               std::vector<PlannedChunk> chunks, std::size_t plannedElements);

    /// @brief 把 begin 之后的状态变成 start 的返回值。
    /// @return 第一包已入队，或这次没有包要发时成功。第一包入队失败时带回该错误码。
    ernc::domain::Result<void> finishStart();

    /// @brief 处理读回字节和写入字节不一致的一包。
    /// @param chunk 当前写包，只在本次调用期间借用。
    /// @param actual 读回数据区，只在本次调用期间借用。
    /// @note 对得上的前缀会 markClean。第一个对不上的下标停住，后续包不发。Set 保留。
    void applyMismatch(const PlannedChunk& chunk, const std::vector<std::uint8_t>& actual);

    /// 参数目录。本对象不拥有。
    const ernc::domain::ParameterCatalog& catalog_;

    /// 固件布局。本对象不拥有。
    const ernc::domain::FirmwareLayout& layout_;

    /// 数组元素。本对象不拥有。
    ernc::domain::ArrayState& state_;

    /// 设备会话。本对象不拥有。
    DeviceSession& session_;

    /// 命令调度器。本对象不拥有。
    CommandScheduler& scheduler_;

    /// 当前这笔传输。空表示还没有成功开始过。
    std::optional<Active> active_;
};

}  // namespace ernc::application
