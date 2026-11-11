#pragma once

#include "ernc/domain/types.hpp"
#include "ernc/ports/device_transport.hpp"

#include <cstddef>
#include <cstdint>

namespace ernc::application {

/// 设备会话的生命周期。Disconnecting 只在断开过程中出现。
enum class SessionState {
    /// 没有当前代号。新建会话和断连结束后都在这里。
    Disconnected,

    /// 已分配代号，正在等待端口打开。结果还不能接受。
    Connecting,

    /// 端口已经打开。当前代号可以接受结果。
    Connected,

    /// 端口仍视为打开，代号不变。进入条件由调用方显式标记。
    Degraded,

    /// 正在关闭端口并丢弃旧步骤。当前代号已经作废。
    Disconnecting
};

/// @brief 一次设备连接的代号和五个状态。
///
/// 持有调用方传入的端口引用，不拥有它。端口必须比本对象活得久。
/// 不创建线程，不加锁，也不是 QObject。调用必须和该端口在同一个线程。
/// 本对象只分配 SessionId 并决定它何时失效，不解析 Modbus，也不改 RuntimeStore。
/// 代号从 1 递增，不复用。0 只表示没有会话。
class DeviceSession {
public:
    /// @brief 绑定一个已存在的端口。初始状态是 Disconnected，没有代号。
    /// @param transport 端口引用，本对象不接管所有权。调用方保证它在本对象存活期间有效。
    explicit DeviceSession(ernc::ports::IDeviceTransport& transport);

    /// @brief 禁止复制。端口引用只有一份。
    DeviceSession(const DeviceSession&) = delete;

    /// @brief 禁止复制赋值。
    DeviceSession& operator=(const DeviceSession&) = delete;

    /// @brief 禁止移动。引用成员不能改绑到另一个端口。
    DeviceSession(DeviceSession&&) = delete;

    /// @brief 禁止移动赋值。
    DeviceSession& operator=(DeviceSession&&) = delete;

    /// @brief 当前生命周期状态。
    /// @return 五个状态之一。新建时为 Disconnected。
    SessionState state() const noexcept;

    /// @brief 当前会话代号。
    /// @return 连接过程中和连接成功后为本次代号。断开或打开失败后为 none()。
    ernc::domain::SessionId currentId() const noexcept;

    /// @brief 从 Disconnected 打开端口并建立新代号。
    ///
    /// 调用 transport.open() 之前状态已是 Connecting，代号已经分配。
    /// @return 打开成功时为 Connected，结果里是新代号。
    /// 当前不是 Disconnected 时为 InvalidParameter，状态不变。
    /// 端口打开失败时回到 Disconnected，代号作废，并带回端口的错误码。
    ernc::domain::Result<ernc::domain::SessionId> connect();

    /// @brief 结束当前会话并丢掉端口里还没取走的步骤。
    ///
    /// 从 Connecting、Connected 或 Degraded 进入。已经 Disconnected 时不再关闭端口。
    /// @return 端口关闭成功时成功。关闭失败时状态仍是 Disconnected，代号和 Pending 已经作废，
    /// 结果里是端口的错误码。
    ernc::domain::Result<void> disconnect();

    /// @brief 把 Connected 标成 Degraded，不更换代号。
    /// @return 当前是 Connected 时成功。其他状态为 InvalidParameter，状态不变。
    ernc::domain::Result<void> markDegraded();

    /// @brief 给当前会话登记一次尚未完成的操作。
    /// @return Connected 或 Degraded 时成功，并使 pendingCount 加一。
    /// 其他状态为 InvalidParameter，计数不变。
    ernc::domain::Result<void> notePending();

    /// @brief 判断一个代号的结果能否作用于当前会话。
    /// @param id 结果携带的会话代号，按值传入。
    /// @return 仅当状态是 Connected 或 Degraded，且 id 等于当前代号时为 true。
    bool acceptResult(ernc::domain::SessionId id) const noexcept;

    /// @brief 当前会话上还没清掉的 Pending 个数。
    /// @return 断连后为 0。
    std::size_t pendingCount() const noexcept;

private:
    /// 调用方拥有的端口。本对象只在与它相同的线程上使用。
    ernc::ports::IDeviceTransport& transport_;

    /// 当前状态。新建时为 Disconnected。
    SessionState state_{SessionState::Disconnected};

    /// 当前代号。没有会话时为 none()。
    ernc::domain::SessionId currentId_{ernc::domain::SessionId::none()};

    /// 下一次 connect 使用的代号。从 1 开始。变为 0 表示数值已经用尽。
    std::uint64_t nextRaw_{1};

    /// 当前代号上的 Pending 个数。断连时清零。
    std::size_t pending_{0};
};

}  // namespace ernc::application
