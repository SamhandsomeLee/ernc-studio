#pragma once

#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

namespace ernc::domain {
class ArrayRuntimeStore;
}
#include "scheduler.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ernc::application {

/// 批量读的范围。All 不是目录里的分类名。
enum class BatchReadScopeKind {
    /// 目录中的全部参数，随后按 slot 合并。
    All,

    /// category 与目录分类完全相同的参数。
    Category,

    /// selected 里的参数。线上仍按 slot 排序，不按点选顺序。
    Selected
};

/// 调用方给出的批量读范围。目录和布局都不由这里拥有。
struct BatchReadScope {
    /// 范围种类。
    BatchReadScopeKind kind{BatchReadScopeKind::All};

    /// kind 为 Category 时使用的分类名。空字符串不匹配任何参数。
    std::string category;

    /// kind 为 Selected 时使用的参数标识。调用方拥有这份列表。
    std::vector<ernc::domain::ParameterId> selected;
};

/// 一个参数没有进入任何帧的原因。
enum class BatchSkipReason {
    /// 选中的 ID 不在目录里。
    NotInCatalog,

    /// 目录记录没有固件路径。
    MissingPath,

    /// locate 失败，或 slot 放不进一帧的 16 位起点。
    Unlocated,

    /// 没有元素标量类型，或长度不是 slot 数乘 4。结构体整段属于这一类。
    NotScalar,

    /// 与已经接收的字段占用了重叠的 slot。
    Overlap
};

/// 一条被排除的参数。对象拥有参数标识副本。
struct BatchSkip {
    /// 被排除的参数标识。
    ernc::domain::ParameterId id;

    /// 排除原因。
    BatchSkipReason reason{BatchSkipReason::Unlocated};
};

/// 一帧 FC03 的计划和它覆盖的参数。对象拥有参数标识副本。
struct BatchFramePlan {
    /// 这一帧的起始逻辑 slot。
    std::uint16_t firstSlot{0};

    /// 这一帧的 slot 数。读上限是 62。最后一帧可以更短。
    std::uint16_t slotCount{0};

    /// 这一帧切到的参数，按 slot 从小到大。
    std::vector<ernc::domain::ParameterId> parameters;
};

/// 批量读的进度。完成和失败都按参数计，不按帧计。
struct BatchReadProgress {
    /// 会进入帧的参数个数。
    std::size_t planned{0};

    /// 范围里被排除的参数个数。
    std::size_t skipped{0};

    /// 已经完整解码的参数个数。
    std::size_t completed{0};

    /// 因帧失败或解码失败而放弃的参数个数。
    std::size_t failed{0};
};

/// 一个参数在这批读里的结果。标量同时写进 Store。数组只放在 elements 里。
struct BatchParameterImage {
    /// 参数标识的副本。
    ernc::domain::ParameterId id;

    /// 为 true 时这是 1 个 slot 的标量，deviceScalar 有值。
    bool scalar{false};

    /// 标量读回值。数组读为空。
    std::optional<ernc::domain::ScalarValue> deviceScalar;

    /// 按 slot 顺序排列的元素。标量读为空。
    std::vector<ernc::domain::ScalarValue> elements;
};

/// @brief 一次批量读的代号、命令和帧计划。
///
/// 对象拥有命令号和帧计划的副本。命令号只表示已经入队，不是设备已经回答。
class BatchReadTicket {
    friend class ParameterBatchReader;

public:
    /// @brief 报告这是不是一次已建立的批量读。
    /// @return raw() 非 0 时为 true。空范围的成功批次也是 true。
    bool valid() const noexcept { return raw_ != 0; }

    /// @brief 批量读代号。
    /// @return 无单位整数。0 表示没有这次批量读。它不是 CommandId。
    std::uint64_t raw() const noexcept { return raw_; }

    /// @brief 按发送顺序借出命令号。
    /// @return 与 frames() 一一对应。空向量表示没有帧。
    const std::vector<CommandId>& commands() const noexcept { return commands_; }

    /// @brief 按发送顺序借出帧计划。
    /// @return 内部向量的常量引用。生存期与本对象相同。
    const std::vector<BatchFramePlan>& frames() const noexcept { return frames_; }

private:
    /// @brief 保存一次已经入队的批量读。
    /// @param raw 非 0 的代号。
    /// @param commands 按帧顺序排列的命令号。函数接管这份向量。
    /// @param frames 与命令号对应的帧计划。函数接管这份向量。
    BatchReadTicket(std::uint64_t raw, std::vector<CommandId> commands, std::vector<BatchFramePlan> frames)
        : raw_{raw}, commands_{std::move(commands)}, frames_{std::move(frames)} {}

    /// 批量读代号。0 表示没有这次批量读。
    std::uint64_t raw_{0};

    /// 已入队的命令。下标与 frames_ 相同。
    std::vector<CommandId> commands_;

    /// 每一帧的 slot 范围和覆盖的参数。
    std::vector<BatchFramePlan> frames_;
};

/// @brief 按连续地址把一批参数读入满帧。
///
/// 借用目录、布局、Store、会话和调度器，不拥有它们。这些对象必须比本对象活得久，并且只在调度器所属线程上调用。
/// 不调用 poll()，不创建线程，也不是 QObject。不入队 FC16。
/// 连续字段合成一条 FC03，一帧最多 62 个 slot。中间有空洞时断开，不读空洞里的字节。
/// 一次只保留一批未完成的读。不要和 ParameterReader 同时使用同一个调度器。
class ParameterBatchReader {
public:
    /// @brief 绑定这次批量读要用的目录、布局、状态、会话和调度器。
    /// @param catalog 参数目录，只借用。分组来自每条参数的 category。
    /// @param layout 固件布局，只借用。地址来自 locate()。
    /// @param store 运行状态，只借用。标量成功时由 apply 调用 observe。
    /// @param session 设备会话，只借用。必须和 scheduler 使用同一条端口。
    /// @param scheduler 命令调度器，只借用。本对象通过它入队 FC03。
    ParameterBatchReader(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                         ernc::domain::ParameterRuntimeStore& store, DeviceSession& session, CommandScheduler& scheduler,
                         ernc::domain::ArrayRuntimeStore* arrayStore = nullptr);

    /// @brief 禁止复制。借用的对象只有一份。
    ParameterBatchReader(const ParameterBatchReader&) = delete;

    /// @brief 禁止复制赋值。
    ParameterBatchReader& operator=(const ParameterBatchReader&) = delete;

    /// @brief 禁止移动。引用成员不能改绑。
    ParameterBatchReader(ParameterBatchReader&&) = delete;

    /// @brief 禁止移动赋值。
    ParameterBatchReader& operator=(ParameterBatchReader&&) = delete;

    /// @brief 按范围组帧并入队 FC03。
    ///
    /// 先排除无路径、定位失败和结构体整段，再按 slot 合并连续字段。每帧最多 62 个 slot。
    /// 返回时命令已入队或等待队列中。这不是设备已经回答。
    /// @param scope 读范围。只在本次调用期间借用。Selected 的顺序不决定帧顺序。
    /// @return 成功时包含帧计划。上一批还没结束时为 InvalidParameter，不新增入队。
    /// 需要发送但会话未连接时为 DeviceDisconnected。
    ernc::domain::Result<BatchReadTicket> start(const BatchReadScope& scope);

    /// @brief 把一条已经取走的读结果切回各个参数。
    ///
    /// 会话不接受该 SessionId，或命令号不属于这批读时，不修改 Store。
    /// 一个参数被切到多帧时，全部帧都成功才解码。某一帧失败时，该帧覆盖且尚未完成的参数记失败。
    /// 标量写入 deviceValue。数组只进入 imageOf。
    /// @param outcome 调度器取走的结果，只在本次调用期间借用。
    /// @param readAt 标量成功时写入 Store 的时刻。调用方给出，本函数不读取系统钟。
    void apply(const CommandOutcome& outcome, ernc::domain::RuntimeTime readAt);

    /// @brief 当前是否还有未结束的帧。
    /// @return 还有帧没收到最终结果时为 true。空批次为 false。
    bool busy() const noexcept;

    /// @brief 当前进度。
    /// @return 按值返回计数。调用方拥有这份副本。
    BatchReadProgress progress() const noexcept;

    /// @brief 借出本批被排除的参数。
    /// @return 内部向量的常量引用。生存期维持到下一次 start 或本对象销毁。
    const std::vector<BatchSkip>& skipped() const noexcept;

    /// @brief 查找一个已经完成的参数结果。
    /// @param id 参数标识，只在本次调用期间借用。
    /// @return 找到时指向内部结果，不转移所有权。尚未完成或失败时为空。
    const BatchParameterImage* imageOf(const ernc::domain::ParameterId& id) const noexcept;

private:
    /// 一个已接受、可能被切到多帧的字段。
    struct PendingField {
        /// 参数标识的副本。
        ernc::domain::ParameterId id;

        /// 起始逻辑 slot。
        std::uint32_t slot{0};

        /// 占用的 slot 数。
        std::uint32_t slotCount{0};

        /// 元素种类。同一字段不混用种类。
        ernc::domain::ScalarValue::Kind kind{ernc::domain::ScalarValue::Kind::Float32};

        /// 为 true 时成功后写入 deviceValue。
        bool scalar{false};

        /// 已经拼上的字节。长度是 slotCount 乘 4。
        std::vector<std::uint8_t> payload;

        /// 每个 slot 是否已经从某一帧写入。1 表示已写入。
        std::vector<char> got;

        /// 这一字段已经失败，后续帧不再公布它。
        bool failed{false};

        /// 这一字段已经解码并公布。
        bool published{false};
    };

    /// 一帧已经入队的读。
    struct ActiveFrame {
        /// 调度器分配的命令号。
        CommandId id{CommandId::none()};

        /// 起始逻辑 slot。已经确认能放进 uint16。
        std::uint32_t firstSlot{0};

        /// 这一帧的 slot 数。1 到 62。
        std::uint16_t slotCount{0};

        /// 与这一帧有交集的字段下标。
        std::vector<std::size_t> fields;

        /// 这一帧已经取过最终结果。
        bool done{false};
    };

    /// @brief 把目录标量类型换成解码种类。
    /// @param type 布局上的标量类型。
    /// @return Float32、Int32 或 UInt32。
    static ernc::domain::ScalarValue::Kind kindOf(ernc::domain::DataType type) noexcept;

    /// @brief 结束一个字段并记录观察失败。不公布数组。
    /// @param field 要放弃的字段。调用前尚未 published。
    /// @param code 失败类别。
    void failField(PendingField& field, ernc::domain::ErrorCode code);

    /// @brief 一个字段的全部 slot 都到齐时解码。
    /// @param field 已写满的字段。
    /// @param readAt 标量写入 Store 的时刻。
    void publishField(PendingField& field, ernc::domain::RuntimeTime readAt);

    /// 参数目录。本对象不拥有。
    const ernc::domain::ParameterCatalog& catalog_;

    /// 固件布局。本对象不拥有。
    const ernc::domain::FirmwareLayout& layout_;

    /// 运行状态。标量成功时修改它。
    ernc::domain::ParameterRuntimeStore& store_;

    /// 数组运行状态。可选。数组成功时写入 observeRead。
    ernc::domain::ArrayRuntimeStore* arrayStore_{nullptr};

    /// 设备会话。本对象不拥有。
    DeviceSession& session_;

    /// 命令调度器。本对象不拥有。
    CommandScheduler& scheduler_;

    /// 下一批次的代号。从 1 开始。变为 0 表示数值已经用尽。
    std::uint64_t nextRaw_{1};

    /// 本批入队时的会话代号。
    ernc::domain::SessionId sessionId_{ernc::domain::SessionId::none()};

    /// 被排除的参数。下一次 start 会换掉。
    std::vector<BatchSkip> skipped_;

    /// 会读取的字段。
    std::vector<PendingField> fields_;

    /// 已入队的帧。
    std::vector<ActiveFrame> frames_;

    /// 已经公布的参数结果。
    std::vector<BatchParameterImage> images_;

    /// 已完成的参数个数。
    std::size_t completed_{0};

    /// 已失败的参数个数。
    std::size_t failed_{0};
};

}  // namespace ernc::application
