#pragma once

#include "ernc/application/batch_read.hpp"
#include "ernc/application/batch_write.hpp"
#include "ernc/application/simulator_device_stack.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/types.hpp"

#include <QObject>
#include <QString>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ernc::infrastructure {
class DiagnosticLog;
}

namespace ernc::serial {
class SerialTransport;
}

namespace ernc::presentation {

class DeviceRuntimeWorker;
class WritePlanDialog;

/// UI 主线程设备门面：内存从站（UI 线程泵）或串口（Worker 线程泵）。
class DeviceController final : public QObject {
    Q_OBJECT
public:
    enum class LinkKind {
        None,
        Simulator,
        Serial,
    };

    struct UiRecorder {
        std::function<void(std::string_view action, std::string_view detail, bool success, ernc::domain::ErrorCode error)>
            record;
    };

    DeviceController(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                     ernc::domain::ParameterRuntimeStore& store, QObject* parent = nullptr);

    ~DeviceController() override;

    void setUiRecorder(UiRecorder recorder);
    void attachSessionDiagnosticLog(ernc::infrastructure::DiagnosticLog* log) noexcept;

    LinkKind linkKind() const noexcept { return linkKind_; }
    bool connected() const noexcept;
    ernc::domain::SessionId sessionId() const noexcept;

    ernc::application::CommandScheduler* scheduler() noexcept;
    ernc::application::ParameterBatchWriter* batchWriter() noexcept;

    void connectSimulator();
    void connectSerial(const QString& portName, int baudRate);

    void disconnectDevice();

    void readCategory(const std::string& pageId);

    void pumpTick();

    void setWritePlanDialog(WritePlanDialog* dialog) noexcept;

    /// 串口模式下由写入预览调用，在 Worker 上 start 批量写。
    ernc::domain::ErrorCode startWriteCategory(const QString& pageId);

    /// 串口模式下刷新写入预览。模拟器路径仍用 WritePlanDialog::noteProgress。
    void refreshWritePlanFromWorker(WritePlanDialog* dialog);

    bool writeBusy() const noexcept;

signals:
    void connectionChanged(bool connected, ernc::domain::SessionId sessionId);
    void parametersUpdated(ernc::domain::SessionId sessionId, const QStringList& parameterIds);
    void readPageFinished(bool success, QString detail);
    void operationLogRefreshRequested();

private:
    using RuntimeTime = ernc::domain::RuntimeTime;

    void recordUi(std::string_view action, std::string_view detail, bool success,
                  ernc::domain::ErrorCode error = ernc::domain::ErrorCode::Unknown) const;

    void tearDownSerial();
    void tearDownAll();

    void pumpSimulator();
    void pumpSerial();

    void pumpBatchReadSimulator();
    void pumpBatchWriteSimulator();
    void finishBatchReadIfIdleSimulator();

    RuntimeTime now() const noexcept;

    const ernc::domain::ParameterCatalog& catalog_;
    const ernc::domain::FirmwareLayout& layout_;
    ernc::domain::ParameterRuntimeStore& store_;

    LinkKind linkKind_{LinkKind::None};
    ernc::application::SimulatorDeviceStack stack_;
    UiRecorder uiRecorder_;
    ernc::infrastructure::DiagnosticLog* diagnosticLog_{nullptr};

    std::unique_ptr<ernc::serial::SerialTransport> serialTransport_;
    DeviceRuntimeWorker* serialWorker_{nullptr};

    std::optional<ernc::application::BatchReadTicket> activeRead_;
    std::string lastReadCategory_;
    QStringList parametersTouchedThisRead_;

    WritePlanDialog* writePlanDialog_{nullptr};

    bool wasWriteBusy_{false};
};

}  // namespace ernc::presentation
