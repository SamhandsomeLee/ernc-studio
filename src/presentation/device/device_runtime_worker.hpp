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
    };

    Q_INVOKABLE PumpReport doPump();
    Q_INVOKABLE bool isWriteBusy() const;

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
    bool startScalarRead(const QString& parameterId);
    int startRegionRead(const QString& path);
    void cancelRegionRead();
    int startRegionWrite(const QString& path, const QByteArray& bytes);
    void cancelRegionWrite();
    int startBatchWrite(const QString& pageId);

signals:
    void stackReady();

private:
    using RuntimeTime = ernc::domain::RuntimeTime;

    void pumpBatchRead();
    void pumpScalarRead();
    void pumpBatchWrite(PumpReport& report);
    void finishBatchReadIfIdle(PumpReport& report);
    void finishScalarReadIfIdle(PumpReport& report);
    void pumpRegionRead(PumpReport& report);
    void pumpRegionWrite(PumpReport& report);
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
    std::string lastReadCategory_;
    QStringList parametersTouchedThisRead_;
    bool wasWriteBusy_{false};
};

}  // namespace ernc::presentation

Q_DECLARE_METATYPE(ernc::presentation::DeviceRuntimeWorker::PumpReport)
