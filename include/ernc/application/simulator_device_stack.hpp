#pragma once

#include "ernc/application/batch_read.hpp"
#include "ernc/application/batch_write.hpp"
#include "scheduler.hpp"
#include "session.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace ernc::infrastructure {
class DiagnosticLog;
}

namespace ernc::simulator {
class MemorySlaveTransport;
class SlaveImage;
}  // namespace ernc::simulator

namespace ernc::application {

/// @brief 桌面调试用的内存从站设备栈。P1 只在 UI 所属线程上使用。
///
/// 连接时重建镜像和端口。disconnect 后栈为空。不创建 QSerialPort。
class SimulatorDeviceStack {
public:
    /// @param catalog 参数目录，只借用。必须比本对象活得久。
    /// @param layout 固件布局，只借用。必须比本对象活得久。
    /// @param store 运行状态，只借用。读/写成功后由批量读写更新它。
    SimulatorDeviceStack(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                         ernc::domain::ParameterRuntimeStore& store);

    ~SimulatorDeviceStack();

    SimulatorDeviceStack(const SimulatorDeviceStack&) = delete;
    SimulatorDeviceStack& operator=(const SimulatorDeviceStack&) = delete;

    /// @brief 按目录覆盖范围分配镜像并 connect。
    /// @return 成功时为新会话代号。镜像分配或 open 失败时带回错误码。
    ernc::domain::Result<ernc::domain::SessionId> connectSimulator();

    /// @brief 取消在途命令并关闭端口。
    ernc::domain::Result<void> disconnect();

    /// @brief 当前是否已连接。
    bool connected() const noexcept;

    DeviceSession& session();
    const DeviceSession& session() const;
    CommandScheduler& scheduler();
    ParameterBatchReader& batchReader();
    ParameterBatchWriter& batchWriter();
    const ParameterBatchWriter& batchWriter() const;

    void attachDiagnosticLog(ernc::infrastructure::DiagnosticLog* log) noexcept;

private:
    void reset();

    const ernc::domain::ParameterCatalog& catalog_;
    const ernc::domain::FirmwareLayout& layout_;
    ernc::domain::ParameterRuntimeStore& store_;

    std::unique_ptr<ernc::simulator::SlaveImage> image_;
    std::unique_ptr<ernc::simulator::MemorySlaveTransport> transport_;
    std::unique_ptr<DeviceSession> session_;
    std::unique_ptr<CommandScheduler> scheduler_;
    std::unique_ptr<ParameterBatchReader> batchReader_;
    std::unique_ptr<ParameterBatchWriter> batchWriter_;

    ernc::infrastructure::DiagnosticLog* diagnosticLog_{nullptr};
};

/// @brief 为虚拟从站镜像计算需要的逻辑 slot 上界（不含）。
std::uint32_t maxLogicalSlotForCatalog(const ernc::domain::ParameterCatalog& catalog,
                                        const ernc::domain::FirmwareLayout& layout);

/// @brief 按 slot 数构造零初始化镜像字节。slot 为 0 时返回空。
std::vector<std::uint8_t> makeZeroSimulatorImage(std::uint32_t slotCount);

/// @brief 写入少量演示原值，便于 UI 肉眼验证读回。
void seedSimulatorDemoBytes(std::span<std::uint8_t> bytes);

}  // namespace ernc::application
