#pragma once

#include "ernc/application/write_parameter.hpp"
#include "ernc/domain/write_plan.hpp"

#include <optional>
#include <vector>

namespace ernc::application {

/// 批量写里一项的结局。Excluded 和 NotOneSlot 没有发出 FC16。
enum class BatchWriteKind {
    /// WritePlan 排除了这项。Step 和 RNC_refresh 属于这一类。
    Excluded,

    /// 计划里有它，但它不是 1 个标量 slot。Can_Msg 属于这一类。
    NotOneSlot,

    /// 标量写：设备已对 FC16 回 ACK（readBack=false，不表示已读回 Verified）。
    /// 读回对照路径：读回字节和写入字节相同。
    Verified,

    /// 读回字节和写入字节不同。标量写默认不发读回，通常不出现。
    Mismatch,

    /// 超时、取消、断连或其它失败。没有再次入队。
    Failed
};

/// 一个参数在这批写里的结果。对象拥有参数标识副本。
struct BatchWriteItem {
    /// 参数标识。
    ernc::domain::ParameterId id;

    /// 这项的结局。
    BatchWriteKind kind{BatchWriteKind::Failed};

    /// kind 为 Excluded 时的排除原因。其它结局为空。
    std::optional<ernc::domain::WriteExclusion> exclusion;

    /// Mismatch 或 Failed 时的错误类别。Verified 为空。
    std::optional<ernc::domain::ErrorCode> error;
};

/// @brief 按 WritePlan 的顺序逐个写入脏的标量。
///
/// 借用目录、布局、Store、会话和调度器，不拥有它们。这些对象必须比本对象活得久，并且只在调度器所属线程上调用。
/// 不调用 poll()，不创建线程，也不是 QObject。
/// 一次只发出一条 readBack 为 false 的 FC16。当前项 ACK 结束后才发送下一项。不把多个参数拼进同一帧。
/// 失败项不会再次入队。后面的其它参数仍会写。
class ParameterBatchWriter {
public:
    /// @brief 绑定这批写要用的目录、布局、状态、会话和调度器。
    /// @param catalog 参数目录，只借用。
    /// @param layout 固件布局，只借用。
    /// @param store 运行状态，只借用。内部的 ParameterWriter 在 ACK 或读回后更新它。
    /// @param session 设备会话，只借用。必须和 scheduler 使用同一条端口。
    /// @param scheduler 命令调度器，只借用。同一时刻只交给它一条写命令。
    ParameterBatchWriter(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                         ernc::domain::ParameterRuntimeStore& store, DeviceSession& session, CommandScheduler& scheduler);

    /// @brief 禁止复制。内部写对象只有一份。
    ParameterBatchWriter(const ParameterBatchWriter&) = delete;

    /// @brief 禁止复制赋值。
    ParameterBatchWriter& operator=(const ParameterBatchWriter&) = delete;

    /// @brief 禁止移动。引用成员和写对象不能改绑。
    ParameterBatchWriter(ParameterBatchWriter&&) = delete;

    /// @brief 禁止移动赋值。
    ParameterBatchWriter& operator=(ParameterBatchWriter&&) = delete;

    /// @brief 生成 WritePlan，记下不写的项，并入队第一项。
    ///
    /// 不在 included 里的排除项、Can_Msg 和多 slot 字段不入队。
    /// 返回时至多有一条写命令在途。这不是设备 ACK，也不是读回 Verified。
    /// @param scope 选中列表或分类名。只在本次调用期间借用。顺序决定提交顺序。
    /// @return 计划已经建立时成功。上一批还没结束时为 InvalidParameter。
    /// 第一项需要发送但会话未连接时为 DeviceDisconnected，并且不再发送后面的项。
    ernc::domain::Result<void> start(const ernc::domain::WriteScope& scope);

    /// @brief 把一条已经取走的写结果记到当前参数，并在结束后提交下一项。
    ///
    /// 会话不接受该 SessionId 时，不把这次结果当成 Verified，也不再提交后面的项。
    /// 命令号不属于当前写时不改记录。
    /// @param outcome 调度器取走的结果，只在本次调用期间借用。
    /// @param readAt 读回写入 Store 的时刻。标量 ACK 路径不使用。调用方给出，本函数不读取系统钟。
    void apply(const CommandOutcome& outcome, ernc::domain::RuntimeTime readAt);

    /// @brief 当前是否还有未写完的参数。
    /// @return 有在途写，或队列里还有下一项时为 true。
    bool busy() const noexcept;

    /// @brief 当前已经入队、还没有最终结果的命令号。
    /// @return 有在途写时为该命令号。队列里还没提交的下一项不在这里。
    std::optional<CommandId> inFlight() const noexcept;

    /// @brief 按记录顺序借出每一项的结果。
    /// @return 内部向量的常量引用。生存期维持到下一次 start 或本对象销毁。
    const std::vector<BatchWriteItem>& items() const noexcept;

private:
    /// @brief 判断这条计划项是不是可以逐个写入的 1 slot 标量。
    /// @param item WritePlan 里的一项，只在本次调用期间借用。
    /// @return 路径不是 Can_Msg，且 locate 得到 1 个标量 slot 时为 true。
    bool isOneSlot(const ernc::domain::PlannedWrite& item) const;

    /// @brief 入队队列中的下一项。失败且不是断连时记下该项并继续。
    /// @return 下一项已入队，或队列已经空时成功。会话断开时为 DeviceDisconnected。
    ernc::domain::Result<void> submitNext();

    /// @brief 把当前结局追加到结果，并清掉在途标记。
    /// @param id 当前参数。按值复制进结果。
    /// @param kind 结局种类。
    /// @param error Mismatch 或 Failed 时的错误类别。没有时为空。
    void record(const ernc::domain::ParameterId& id, BatchWriteKind kind, std::optional<ernc::domain::ErrorCode> error);

    /// 参数目录。本对象不拥有。
    const ernc::domain::ParameterCatalog& catalog_;

    /// 固件布局。本对象不拥有。
    const ernc::domain::FirmwareLayout& layout_;

    /// 运行状态。本对象不拥有。实际写入由 writer_ 完成。
    ernc::domain::ParameterRuntimeStore& store_;

    /// 设备会话。本对象不拥有。
    DeviceSession& session_;

    /// 单参数写。和本对象使用同一条调度器。
    ParameterWriter writer_;

    /// 还没提交的 1 slot 参数。顺序与 WritePlan 的 included 一致。
    std::vector<ernc::domain::ParameterId> queue_;

    /// 下一待提交项在 queue_ 中的下标。
    std::size_t next_{0};

    /// 已经入队、还没有最终结果的参数。空表示当前没有在途写。
    std::optional<ernc::domain::ParameterId> current_;

    /// 与 current_ 对应的命令号。current_ 为空时也为空。
    std::optional<CommandId> command_;

    /// 已经确定的结果。排除项在开始时写入，写结果在结束时追加。
    std::vector<BatchWriteItem> items_;
};

}  // namespace ernc::application
