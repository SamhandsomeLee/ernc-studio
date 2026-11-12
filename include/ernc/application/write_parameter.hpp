#pragma once

#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "scheduler.hpp"

#include <optional>

namespace ernc::application {

/// @brief 把一个已暂存的标量写入设备，并在 FC16 ACK 后结束。
///
/// 借用目录、布局、Store、会话和调度器，不拥有它们。这些对象必须比本对象活得久，并且只在调度器所属线程上调用。
/// 不调用 poll()，不创建线程，也不是 QObject。一次只保留一笔未完成的写。
/// 入队的是一条 readBack 为 false 的 FC16。调度器在 ACK 后以 WriteCommandResult 结束，不自动发 FC03。
/// 本对象不把 ACK 当成读回 Verified；标量写成功时只 markClean，不 observe。
/// 若 outcome 仍为 WriteVerification，则沿用读回对照路径（供调度器测试或其它调用方保留）。
/// Can_Msg 不入队。数组和结构体整段不入队。
class ParameterWriter {
public:
    /// @brief 绑定这次写要用的目录、布局、状态、会话和调度器。
    /// @param catalog 参数目录，只借用。
    /// @param layout 固件布局，只借用。地址来自 locate()。
    /// @param store 运行状态，只借用。ACK 或读回结束后由 apply 更新。
    /// @param session 设备会话，只借用。必须和 scheduler 使用同一条端口。
    /// @param scheduler 命令调度器，只借用。本对象通过它入队 FC16。
    ParameterWriter(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                    ernc::domain::ParameterRuntimeStore& store, DeviceSession& session, CommandScheduler& scheduler);

    /// @brief 禁止复制。借用的对象只有一份。
    ParameterWriter(const ParameterWriter&) = delete;

    /// @brief 禁止复制赋值。
    ParameterWriter& operator=(const ParameterWriter&) = delete;

    /// @brief 禁止移动。引用成员不能改绑。
    ParameterWriter(ParameterWriter&&) = delete;

    /// @brief 禁止移动赋值。
    ParameterWriter& operator=(ParameterWriter&&) = delete;

    /// @brief 为这一个参数入队写命令（默认不要求读回）。
    ///
    /// 先做只含该 ID 的 WritePlan。不在 included 里、路径是 Can_Msg，或 locate 不是 1 个标量 slot 时不入队。
    /// 入队时记下 stagedAt 和 4 字节目标。返回的命令号只表示已交给调度器。
    /// @param id 参数标识，只在本次调用期间借用。它必须已经有 stagedValue。
    /// @return 命令已入队时，结果里是 CommandId。这不是设备 ACK，也不是读回 Verified。
    /// 会话未连接时为 DeviceDisconnected。只读为 ReadOnlyViolation。其余拒绝为 InvalidParameter。
    ernc::domain::Result<CommandId> write(const ernc::domain::ParameterId& id);

    /// @brief 把一条已经取走的写结果并入 Store。
    ///
    /// 会话不接受该 SessionId，或命令号不属于当前写时，不修改 Store。
    /// WriteCommandResult：stagedAt 没变时 markClean；不改 deviceValue、readAt。
    /// WriteVerification 且 Verified：更新 deviceValue 并视 stagedAt 清 dirty（读回对照路径，标量写默认不走）。
    /// WriteVerification 且 Mismatch：deviceValue 改为读回值，dirty 和 stagedValue 保留，错误为 ReadBackMismatch。
    /// 超时、取消或异常时不改 deviceValue，也不再入队。
    /// @param outcome 调度器取走的结果，只在本次调用期间借用。
    /// @param readAt 读回写入 Store 的时刻。只在 Verified 路径使用。ACK 路径忽略。
    void apply(const CommandOutcome& outcome, ernc::domain::RuntimeTime readAt);

    /// @brief 当前是否还有未完成的写。
    /// @return 写命令还没得到最终结果时为 true。Sent、Acked 和 Verifying 都算未完成。
    bool busy() const noexcept;

private:
    /// 一笔已入队、尚未结束的写。对象拥有参数标识副本。
    struct ActiveWrite {
        /// 调度器分配的命令号。
        CommandId id{CommandId::none()};

        /// 参数标识的副本。
        ernc::domain::ParameterId parameter;

        /// 入队时的会话代号。
        ernc::domain::SessionId session{ernc::domain::SessionId::none()};

        /// 入队时的暂存时刻。空表示当时没有 stagedAt。
        std::optional<ernc::domain::RuntimeTime> stagedAt;

        /// 写入字节要按这个种类解码。读回对照路径用来解码 actual。
        ernc::domain::ScalarValue::Kind kind{ernc::domain::ScalarValue::Kind::Float32};
    };

    /// @brief 判断固件路径是不是 Can_Msg 整段或它的子路径。
    /// @param path 目录中的固件路径，只在本次调用期间借用。
    /// @return 路径等于 Can_Msg，或以 Can_Msg. 开头时为 true。
    static bool isCanMsg(std::string_view path) noexcept;

    /// @brief 结束当前写并记录观察失败。不改 stagedValue 和 dirty。
    /// @param code 失败类别。Unknown 出现在这里时也是失败。
    void failActive(ernc::domain::ErrorCode code);

    /// 参数目录。本对象不拥有。
    const ernc::domain::ParameterCatalog& catalog_;

    /// 固件布局。本对象不拥有。
    const ernc::domain::FirmwareLayout& layout_;

    /// 运行状态。apply 按 ACK 或读回路径修改。
    ernc::domain::ParameterRuntimeStore& store_;

    /// 设备会话。本对象不拥有。
    DeviceSession& session_;

    /// 命令调度器。本对象不拥有。
    CommandScheduler& scheduler_;

    /// 未完成的写。空表示可以接受下一笔。
    std::optional<ActiveWrite> active_;
};

}  // namespace ernc::application
