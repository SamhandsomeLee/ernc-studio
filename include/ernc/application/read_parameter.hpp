#pragma once

#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "scheduler.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace ernc::application {

/// @brief 一次逻辑读的代号和它入队的命令。
///
/// 标量对应一条命令。同质数组对应多条按 slot 顺序排列的 FC03。
/// 对象拥有命令号副本。它不表示设备已经返回数据。
class ReadTicket {
    friend class ParameterReader;

public:
    /// @brief 报告这是不是一次已入队的读。
    /// @return raw() 非 0 时为 true。
    bool valid() const noexcept { return raw_ != 0; }

    /// @brief 逻辑读代号。
    /// @return 无单位整数。0 表示没有这次读。它不是 CommandId，也不是 SessionId。
    std::uint64_t raw() const noexcept { return raw_; }

    /// @brief 按入队顺序借出命令号。
    /// @return 内部向量的常量引用。生存期与本对象相同。
    const std::vector<CommandId>& commands() const noexcept { return commands_; }

private:
    /// @brief 保存一次已经入队的读。
    /// @param raw 非 0 的逻辑读代号。
    /// @param commands 按 slot 顺序排列的命令号。函数接管这份向量。
    ReadTicket(std::uint64_t raw, std::vector<CommandId> commands)
        : raw_{raw}, commands_{std::move(commands)} {}

    /// 逻辑读代号。0 表示没有这次读。
    std::uint64_t raw_{0};

    /// 这次读入队的命令。下标 0 是起始 slot 最低的那一包。
    std::vector<CommandId> commands_;
};

/// @brief 一次已经完成的逻辑读。
///
/// 标量结果同时写进 RuntimeStore。数组结果只放在 elements 里，不写入 deviceValue。
/// 对象拥有参数标识和值的副本。
struct ParameterReadImage {
    /// 被读取的参数标识。
    ernc::domain::ParameterId id;

    /// 为 true 时这是 1 个 slot 的标量，deviceScalar 有值。
    bool scalar{false};

    /// 标量读回值。数组读为空。它是 deviceValue 的同一份副本，不是用户暂存值。
    std::optional<ernc::domain::ScalarValue> deviceScalar;

    /// 按 slot 顺序排列的元素。标量读为空。Can_Msg 成功时为 72 个 float。
    std::vector<ernc::domain::ScalarValue> elements;
};

/// @brief 把一个参数 ID 读进运行状态或数组结果。
///
/// 借用目录、布局、Store、会话和调度器，不拥有它们。这些对象必须比本对象活得久，并且只在调度器所属线程上调用。
/// 不调用 poll()，不创建线程，也不是 QObject。不入队 FC16。
/// 一次只保留一笔未完成的逻辑读。Can_Msg 按 float[72] 读取。没有元素标量类型的结构体不入队。
class ParameterReader {
public:
    /// @brief 绑定这次读要用的目录、布局、状态、会话和调度器。
    /// @param catalog 参数目录，只借用。
    /// @param layout 固件布局，只借用。地址来自 locate()。
    /// @param store 运行状态，只借用。标量成功时由 apply 调用 observe。
    /// @param session 设备会话，只借用。必须和 scheduler 使用同一条端口。
    /// @param scheduler 命令调度器，只借用。本对象通过它入队 FC03。
    ParameterReader(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                    ernc::domain::ParameterRuntimeStore& store, DeviceSession& session, CommandScheduler& scheduler);

    /// @brief 禁止复制。借用的对象只有一份。
    ParameterReader(const ParameterReader&) = delete;

    /// @brief 禁止复制赋值。
    ParameterReader& operator=(const ParameterReader&) = delete;

    /// @brief 禁止移动。引用成员不能改绑。
    ParameterReader(ParameterReader&&) = delete;

    /// @brief 禁止移动赋值。
    ParameterReader& operator=(ParameterReader&&) = delete;

    /// @brief 按参数 ID 入队一笔逻辑读。
    ///
    /// 标量入队一条 FC03。同质标量数组按默认 60 个 slot 切块后按顺序入队。
    /// 找不到参数、没有固件路径、locate 失败、没有元素标量类型、目录类型和布局类型冲突，
    /// 或者上一笔读还没结束时，不新增入队。
    /// @param id 参数标识，只在本次调用期间借用。
    /// @return 命令已经入队时，结果里是新的 ReadTicket。这不是设备响应。
    /// 会话未连接时为 DeviceDisconnected。其他拒绝为 InvalidParameter。
    /// 切块失败时带回 planSlotChunks 的错误码。
    ernc::domain::Result<ReadTicket> read(const ernc::domain::ParameterId& id);

    /// @brief 把一条已经取走的命令结果并入当前逻辑读。
    ///
    /// 会话不接受该 SessionId，或命令号不属于当前读时，不修改 Store，也不公布数组。
    /// 会话拒绝时同时丢掉当前这笔未完成的读。
    /// 标量全部成功后 observe。数组全部成功后只更新 image()。任一块失败时不公布半截数组。
    /// @param outcome 调度器取走的结果，只在本次调用期间借用。
    /// @param readAt 成功读回记入 Store 的时刻。调用方给出，本函数不读取系统钟。数组读不使用它写 deviceValue。
    void apply(const CommandOutcome& outcome, ernc::domain::RuntimeTime readAt);

    /// @brief 当前是否还有未完成的逻辑读。
    /// @return 有未收齐的命令时为 true。
    bool busy() const noexcept;

    /// @brief 最近一次成功完成的逻辑读。
    /// @return 有成功结果时指向内部副本，不转移所有权。还没有成功读，或最近一笔失败后为空。
    const ParameterReadImage* image() const noexcept;

private:
    /// 一包已经入队、可能尚未收回的数据。
    struct PendingChunk {
        /// 这包的命令号。
        CommandId id{CommandId::none()};

        /// 这包的 slot 数。用来核对响应长度。
        std::uint16_t slotCount{0};

        /// 已经收回的数据区。空表示这包还没成功。
        std::optional<std::vector<std::uint8_t>> payload;
    };

    /// 一笔未完成的逻辑读。对象拥有参数标识和各包载荷。
    struct ActiveRead {
        /// 逻辑读代号。和返回给调用方的 ReadTicket::raw() 相同。
        std::uint64_t raw{0};

        /// 参数标识的副本。
        ernc::domain::ParameterId id;

        /// 入队时的会话代号。
        ernc::domain::SessionId session{ernc::domain::SessionId::none()};

        /// 元素种类。float、int32、uint32 不混用。
        ernc::domain::ScalarValue::Kind kind{ernc::domain::ScalarValue::Kind::Float32};

        /// 为 true 时整段只有 1 个 slot，成功后写入 deviceValue。
        bool scalar{false};

        /// 按 slot 顺序排列的各包。
        std::vector<PendingChunk> chunks;
    };

    /// @brief 结束当前读并记录观察失败。不公布数组结果。
    /// @param code 失败类别。Unknown 出现在这里时也是失败。
    void failActive(ernc::domain::ErrorCode code);

    /// @brief 各包都已收回时解码并公布结果。
    /// @param readAt 标量成功时写入 Store 的时刻。
    void finish(ernc::domain::RuntimeTime readAt);

    /// 参数目录。本对象不拥有。
    const ernc::domain::ParameterCatalog& catalog_;

    /// 固件布局。本对象不拥有。
    const ernc::domain::FirmwareLayout& layout_;

    /// 运行状态。标量读成功时修改它。
    ernc::domain::ParameterRuntimeStore& store_;

    /// 设备会话。本对象不拥有。
    DeviceSession& session_;

    /// 命令调度器。本对象不拥有。
    CommandScheduler& scheduler_;

    /// 下一笔逻辑读的代号。从 1 开始。变为 0 表示数值已经用尽。
    std::uint64_t nextRaw_{1};

    /// 未完成的读。空表示可以接受下一笔。
    std::optional<ActiveRead> active_;

    /// 最近一次成功的结果。失败后清空。
    std::optional<ParameterReadImage> completed_;
};

}  // namespace ernc::application
