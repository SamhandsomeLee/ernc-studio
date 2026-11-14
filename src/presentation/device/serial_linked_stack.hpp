#pragma once

#include "ernc/application/batch_read.hpp"
#include "ernc/application/batch_write.hpp"
#include "ernc/application/read_parameter.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

namespace ernc::domain {
class ArrayRuntimeStore;
}

#include "scheduler.hpp"
#include "session.hpp"

#include <memory>

namespace ernc::infrastructure {
class DiagnosticLog;
}

namespace ernc::ports {
class IDeviceTransport;
}

namespace ernc::presentation {

/// 串口 Worker 的参数读写栈。生产端口须在构建前启动所属线程。
/// 方法只能在 transport 所属 Worker 线程上调用。
class SerialLinkedStack {
public:
    /// @brief 保存创建参数读写会话所需的依赖，连接时才打开端口。
    /// @param catalog 借用的参数目录，须比栈存活更久。
    /// @param layout 借用的固件布局，须比栈存活更久。
    /// @param store 借用的标量运行状态，须比栈存活更久。
    /// @param arrayStore 借用的数组运行状态，须比栈存活更久。
    /// @param transport 借用的字节端口，须比栈存活更久。只在端口的所属线程调用。
    SerialLinkedStack(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                      ernc::domain::ParameterRuntimeStore& store, ernc::domain::ArrayRuntimeStore& arrayStore,
                      ernc::ports::IDeviceTransport& transport);

    ~SerialLinkedStack();

    ernc::domain::Result<ernc::domain::SessionId> connectLink();
    ernc::domain::Result<void> disconnectLink();

    bool connected() const noexcept;

    ernc::application::DeviceSession& session();
    const ernc::application::DeviceSession& session() const;
    ernc::application::CommandScheduler& scheduler();
    ernc::application::ParameterBatchReader& batchReader();
    ernc::application::ParameterBatchWriter& batchWriter();
    ernc::application::ParameterReader& parameterReader();
    const ernc::application::ParameterReader& parameterReader() const;

    void attachDiagnosticLog(ernc::infrastructure::DiagnosticLog* log) noexcept;

private:
    void resetKeepTransport();

    const ernc::domain::ParameterCatalog& catalog_;
    const ernc::domain::FirmwareLayout& layout_;
    ernc::domain::ParameterRuntimeStore& store_;
    ernc::domain::ArrayRuntimeStore& arrayStore_;
    /// 调用方拥有的字节端口，供会话和调度器共用。
    ernc::ports::IDeviceTransport& transport_;

    std::unique_ptr<ernc::application::DeviceSession> session_;
    std::unique_ptr<ernc::application::CommandScheduler> scheduler_;
    std::unique_ptr<ernc::application::ParameterBatchReader> batchReader_;
    std::unique_ptr<ernc::application::ParameterBatchWriter> batchWriter_;
    std::unique_ptr<ernc::application::ParameterReader> parameterReader_;

    ernc::infrastructure::DiagnosticLog* diagnosticLog_{nullptr};
};

}  // namespace ernc::presentation
