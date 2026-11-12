#pragma once

#include "ernc/application/array_operations.hpp"
#include "ernc/application/batch_read.hpp"
#include "ernc/application/batch_write.hpp"
#include "ernc/application/read_parameter.hpp"
#include "ernc/application/simulator_device_stack.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"

namespace ernc::domain {
class ArrayRuntimeStore;
}
#include "ernc/domain/types.hpp"

#include <QObject>
#include <QString>
#include <QTimer>

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
                     ernc::domain::ParameterRuntimeStore& store, ernc::domain::ArrayRuntimeStore& arrayStore,
                     QObject* parent = nullptr);

    /// 单数组 FC03 整段读（模拟器同步泵；串口经 Worker 批量读覆盖）。
    void readArrayParameter(const QString& parameterId);

    /// 单数组写 dirty 元素（当前仅模拟器）。
    void writeArrayParameter(const QString& parameterId);

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

    /// @brief 批量读目录标量。大块区域（check_data、ERNC_TF）按布局跳过，不触发区域传函流程。
    void readAllScalars();

    /// @brief 读固件 Can_Msg float[72]。仅模拟器路径；与批量读不要并发。
    void readCanMsg();

    /// @brief 最近一次成功读回的 Can_Msg 元素；长度应为 72。
    const std::vector<float>& canMsgSnapshot() const noexcept { return canMsgSnapshot_; }

    [[nodiscard]] bool canMsgSnapshotValid() const noexcept { return canMsgSnapshotValid_; }

    [[nodiscard]] bool canMsgReadBusy() const noexcept;

    void pumpTick();

    void setWritePlanDialog(WritePlanDialog* dialog) noexcept;

    /// 串口模式下由写入预览调用，在 Worker 上 start 批量写。
    ernc::domain::ErrorCode startWriteCategory(const QString& pageId);

    /// @brief 批量写全部 dirty 标量。区域参数只进排除列表，不打开 BIN 传函传输。
    ernc::domain::ErrorCode startWriteAllDirty();

    /// 串口模式下刷新写入预览。模拟器路径仍用 WritePlanDialog::noteProgress。
    void refreshWritePlanFromWorker(WritePlanDialog* dialog);

    bool writeBusy() const noexcept;

signals:
    void connectionChanged(bool connected, ernc::domain::SessionId sessionId);
    void parametersUpdated(ernc::domain::SessionId sessionId, const QStringList& parameterIds);
    void readPageFinished(bool success, QString detail);
    void canMsgUpdated(bool success);
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
    void pumpParameterReadSimulator();
    void finishBatchReadIfIdleSimulator();
    void finishCanMsgReadIfIdleSimulator();
    void pumpArrayIoSimulator();

    void startCanMsgSimulationPulse();
    void stopCanMsgSimulationPulse();
    void onCanMsgPulseTick();

    RuntimeTime now() const noexcept;

    const ernc::domain::ParameterCatalog& catalog_;
    const ernc::domain::FirmwareLayout& layout_;
    ernc::domain::ParameterRuntimeStore& store_;
    ernc::domain::ArrayRuntimeStore& arrayStore_;

    LinkKind linkKind_{LinkKind::None};
    ernc::application::SimulatorDeviceStack stack_;
    UiRecorder uiRecorder_;
    ernc::infrastructure::DiagnosticLog* diagnosticLog_{nullptr};

    std::unique_ptr<ernc::serial::SerialTransport> serialTransport_;
    DeviceRuntimeWorker* serialWorker_{nullptr};

    std::optional<ernc::application::BatchReadTicket> activeRead_;
    std::optional<ernc::application::ReadTicket> activeCanMsgRead_;
    std::vector<float> canMsgSnapshot_;
    bool canMsgSnapshotValid_{false};
    std::string lastReadCategory_;
    QStringList parametersTouchedThisRead_;

    WritePlanDialog* writePlanDialog_{nullptr};

    bool wasWriteBusy_{false};

    struct ActiveArrayIo {
        ernc::domain::ParameterId id;
        bool reading{true};
        std::unique_ptr<ernc::application::ArrayTransfer> transfer;
    };
    std::optional<ActiveArrayIo> activeArrayIo_;

    QTimer canMsgPulseTimer_;
    bool canMsgPulseHigh_{true};
};

}  // namespace ernc::presentation
