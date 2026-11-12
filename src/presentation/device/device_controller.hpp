#pragma once

#include "ernc/application/array_operations.hpp"
#include "ernc/application/batch_read.hpp"
#include "ernc/application/batch_write.hpp"
#include "ernc/application/read_parameter.hpp"
#include "ernc/application/simulator_device_stack.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/region_catalog.hpp"

namespace ernc::domain {
class ArrayRuntimeStore;
}
namespace ernc::application {
class RegionReader;
class RegionWriter;
struct RegionWritePreview;
}
#include "ernc/domain/types.hpp"

#include <QObject>
#include <QByteArray>
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

    /// @brief 开始读取一块已核验的大区域。串口执行留在 Worker 线程，模拟器执行留在 UI 泵线程。
    /// @param path ERNC_TF、check_data、SpecRadius 或它们的直接子字段。
    /// @return 入队成功不代表读完；正式布局证据不足时返回 InvalidParameter，且不会发出 FC03。
    ernc::domain::Result<void> startRegionRead(const QString& path);

    /// @brief 请求取消当前区域读。已经到达的完整分包保留，但不会发布成完整快照。
    void cancelRegionRead();

    /// @brief 计算区域写预览，不访问设备。字节长度须等于区域 size。
    ernc::domain::Result<ernc::application::RegionWritePreview> previewRegionWrite(const QString& path,
                                                                                   const QByteArray& bytes);

    /// @brief 在已有预览且 commitPermitted 时开始 FC16 分包写（读回验证）。
    ernc::domain::Result<void> startRegionWrite(const QString& path, const QByteArray& bytes);

    void cancelRegionWrite();

signals:
    void connectionChanged(bool connected, ernc::domain::SessionId sessionId);
    void parametersUpdated(ernc::domain::SessionId sessionId, const QStringList& parameterIds);
    void readPageFinished(bool success, QString detail);
    /// @brief 本批标量写已结束。只在 UI 线程发出。
    /// @param acked 设备回 ACK 的条目数，不代表读回验证。
    /// @param failed 失败条目数，不含计划排除项。
    void writeBatchFinished(int acked, int failed);
    void canMsgUpdated(bool success);
    /// @brief 大区域读进度。字节数总是 4 的倍数，在 UI 线程发出。
    void regionReadProgress(QString path, quint64 filledBytes, quint64 plannedBytes);
    /// @brief 大区域读结束。success 为真时 bytes 是完整快照；失败或取消时 bytes 为空。
    void regionReadFinished(bool success, QString path, QByteArray bytes, int errorCode);
    void regionWriteProgress(QString path, quint64 verifiedBytes, quint64 plannedBytes);
    void regionWriteFinished(bool success, QString path, int errorCode);
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

    /// @brief 刷新本批结果所涉及的参数行和常驻任务状态。
    /// @param items 已完成批次的结果，借用到函数返回。
    void publishWriteResult(const std::vector<ernc::application::BatchWriteItem>& items);
    void pumpParameterReadSimulator();
    void finishBatchReadIfIdleSimulator();
    void finishCanMsgReadIfIdleSimulator();
    void pumpArrayIoSimulator();
    void pumpRegionReadSimulator();
    void finishRegionReadSimulator();
    void pumpRegionWriteSimulator();
    void finishRegionWriteSimulator();

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
    std::optional<ernc::domain::RegionCatalog> regionCatalog_;
    std::unique_ptr<ernc::application::RegionReader> simulatorRegionReader_;
    std::unique_ptr<ernc::application::RegionWriter> simulatorRegionWriter_;
    std::string activeRegionPath_;
    bool activeRegionIsWrite_{false};
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
