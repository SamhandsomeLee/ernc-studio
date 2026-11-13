#pragma once

#include "ernc/application/batch_read.hpp"
#include "ernc/application/batch_write.hpp"
#include "device/serial_linked_stack.hpp"
#include "write_verification.hpp"
#include "ernc/domain/catalog.hpp"
#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/runtime_store.hpp"
#include "ernc/domain/region_catalog.hpp"
#include "ernc/domain/types.hpp"

#include <QObject>
#include <QString>
#include <QStringList>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace ernc::infrastructure {
class DiagnosticLog;
}
namespace ernc::application {
class RegionReader;
class RegionWriter;
}

namespace ernc::serial {
class SerialTransport;
}

namespace ernc::presentation {

/// 运行在 SerialTransport Worker 线程上的设备运行时。
class DeviceRuntimeWorker final : public QObject {
    Q_OBJECT
public:
    struct PumpReport {
        QStringList parametersUpdated;
        bool scalarReadFinished{false};
        bool scalarReadSuccess{false};
        QString scalarReadParameterId;
        std::string scalarReadDetailJson;
        bool readFinished{false};
        bool readSuccess{false};
        std::string readDetailJson;
        bool writeFinished{false};
        bool writeSuccess{false};
        std::string writeDetailJson;
        bool writerBusy{false};
        std::vector<ernc::application::BatchWriteItem> writeItems;
        std::optional<ernc::application::CommandId> writeInflight;
        std::optional<ernc::application::WritePhase> writePhase;
        std::string regionPath;
        std::size_t regionFilledBytes{0};
        std::size_t regionPlannedBytes{0};
        bool regionFinished{false};
        bool regionIsWrite{false};
        bool regionSuccess{false};
        int regionErrorCode{static_cast<int>(ernc::domain::ErrorCode::Unknown)};
        std::vector<std::uint8_t> regionBytes;
        bool canMsgReadFinished{false};
        bool canMsgReadSuccess{false};
        /// Can_Msg float[72]；仅 canMsgReadFinished 时有效。
        std::vector<float> canMsgElements;
    };

    Q_INVOKABLE PumpReport doPump();
    Q_INVOKABLE bool isWriteBusy() const;
    /// @return 已入队 Can_Msg 读且尚未在 pump 中结束时为 true。
    Q_INVOKABLE bool canMsgReadBusy() const;

    DeviceRuntimeWorker(const ernc::domain::ParameterCatalog& catalog, const ernc::domain::FirmwareLayout& layout,
                        ernc::domain::ParameterRuntimeStore& store, ernc::domain::ArrayRuntimeStore& arrayStore,
                        ernc::serial::SerialTransport* transport, QObject* parent = nullptr);

    ~DeviceRuntimeWorker() override;

    void attachDiagnosticLog(ernc::infrastructure::DiagnosticLog* log) noexcept;

public slots:
    bool doConnect();
    void doDisconnect();
    bool isConnected() const;
    quint64 sessionIdRaw() const;
    bool startBatchRead(const QString& pageId);
    bool startArrayRead(const QString& parameterId);
    /// @brief 在 Device Worker 线程校验单标量读取条件并尝试入队，不代表设备已响应或读回已验证。
    /// @param parameterId 参数目录中的稳定 ID；本函数只在调用期间读取该字符串。
    /// @return 0 表示命令已入队；非 0 为拒绝码，供 UI 线程记录为 worker_gate。
    /// @note 调用方当前通过 BlockingQueuedConnection 等待入队结果；本函数不得同步回调 UI 线程。
    int startScalarRead(const QString& parameterId);
    /// @brief 入队读固件 Can_Msg（72×float32）。0 表示已入队。
    /// @return 0 成功入队；非 0 为拒绝码（与 startScalarRead 同类门控）。
    int startCanMsgRead();
    int startRegionRead(const QString& path);
    void cancelRegionRead();
    int startRegionWrite(const QString& path, const QByteArray& bytes);
    void cancelRegionWrite();
    int startBatchWrite(const QString& pageId);

signals:
    void stackReady();

    /// @brief 发布 Worker 线程上的诊断快照，不表示命令 ACK 或读回 Verified。
    /// @param action 稳定动作名；按值跨线程交给接收方。
    /// @param detail JSON 对象文本；按值跨线程交给接收方。
    /// @param success 当前诊断步骤是否成功，不代表整个读取完成。
    /// @note 接收方必须使用 QueuedConnection 在其所属线程处理，避免与同步 Worker 调用形成双向阻塞。
    void traceUiAction(QString action, QString detail, bool success);

private:
    using RuntimeTime = ernc::domain::RuntimeTime;

    void pumpBatchRead();
    void pumpScalarRead();
    void pumpCanMsgRead();
    void pumpBatchWrite(PumpReport& report);
    void finishBatchReadIfIdle(PumpReport& report);
    void finishScalarReadIfIdle(PumpReport& report);
    void finishCanMsgReadIfIdle(PumpReport& report);
    void pumpRegionRead(PumpReport& report);
    void pumpRegionWrite(PumpReport& report);
    void traceScalarWorkerGate(const QString& parameterId, int gate);

    RuntimeTime now() const noexcept;

    const ernc::domain::ParameterCatalog& catalog_;
    const ernc::domain::FirmwareLayout& layout_;
    ernc::domain::ParameterRuntimeStore& store_;
    ernc::domain::ArrayRuntimeStore& arrayStore_;
    ernc::serial::SerialTransport* transport_;

    std::unique_ptr<SerialLinkedStack> stack_;
    std::optional<ernc::domain::RegionCatalog> regionCatalog_;
    std::unique_ptr<ernc::application::RegionReader> regionReader_;
    std::unique_ptr<ernc::application::RegionWriter> regionWriter_;
    bool activeRegionIsWrite_{false};
    std::string activeRegionPath_;
    ernc::infrastructure::DiagnosticLog* diagnosticLog_{nullptr};

    std::optional<ernc::application::BatchReadTicket> activeRead_;
    std::optional<ernc::application::ReadTicket> activeScalarRead_;
    QString activeScalarReadId_;
    std::optional<ernc::application::ReadTicket> activeCanMsgRead_;
    std::string lastReadCategory_;
    QStringList parametersTouchedThisRead_;
    bool wasWriteBusy_{false};
};

}  // namespace ernc::presentation

Q_DECLARE_METATYPE(ernc::presentation::DeviceRuntimeWorker::PumpReport)
