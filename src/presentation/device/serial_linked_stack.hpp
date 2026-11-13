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

namespace ernc::serial {
class SerialTransport;
}

namespace ernc::presentation {

/// 真实串口上的参数读写栈。transport 由调用方拥有，且须先 start()。
/// 方法只能在 transport 所属 Worker 线程上调用。
class SerialLinkedStack {
public:
    SerialLinkedStack(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                      ernc::domain::ParameterRuntimeStore& store, ernc::domain::ArrayRuntimeStore& arrayStore,
                      ernc::serial::SerialTransport& transport);

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
    ernc::serial::SerialTransport& transport_;

    std::unique_ptr<ernc::application::DeviceSession> session_;
    std::unique_ptr<ernc::application::CommandScheduler> scheduler_;
    std::unique_ptr<ernc::application::ParameterBatchReader> batchReader_;
    std::unique_ptr<ernc::application::ParameterBatchWriter> batchWriter_;
    std::unique_ptr<ernc::application::ParameterReader> parameterReader_;

    ernc::infrastructure::DiagnosticLog* diagnosticLog_{nullptr};
};

}  // namespace ernc::presentation
