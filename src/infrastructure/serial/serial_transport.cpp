#include "serial_transport.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QSerialPortInfo>
#include <QThread>

#include <limits>
#include <utility>

namespace ernc::serial {

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::ports::TransportStep;

/// @brief 做一个没有字节、没有错误码的步骤。
/// @param kind Idle、Closed 或其他不携带载荷的类别。
/// @return 调用方拥有的步骤。
TransportStep bareStep(TransportStep::Kind kind) {
    TransportStep step;
    step.kind = kind;
    return step;
}

}  // namespace

std::vector<std::string> listSerialPortNames() {
    std::vector<std::string> names;
    const QList<QSerialPortInfo> ports = QSerialPortInfo::availablePorts();
    names.reserve(static_cast<std::size_t>(ports.size()));
    for (const QSerialPortInfo& info : ports) {
        names.push_back(info.portName().toStdString());
    }
    return names;
}

SerialTransport::SerialTransport(std::string portName, std::int32_t baudRate)
    : QObject(nullptr)
    , portName_(std::move(portName))
    , baudRate_(baudRate)
    , ownerThread_(QThread::currentThread()) {}

SerialTransport::~SerialTransport() {
    if (worker_ == nullptr) {
        return;
    }
    if (worker_->isRunning() && QThread::currentThread() != thread()) {
        QThread* const destination = ownerThread_;
        const bool retired = QMetaObject::invokeMethod(
            this,
            [this, destination] { retireOnWorker(destination); },
            Qt::BlockingQueuedConnection);
        if (retired) {
            worker_->quit();
            worker_->wait();
        }
    }
    delete worker_;
    worker_ = nullptr;
}

void SerialTransport::start() {
    if (worker_ != nullptr || QCoreApplication::instance() == nullptr) {
        return;
    }
    if (QThread::currentThread() != ownerThread_) {
        return;
    }
    worker_ = new QThread();
    worker_->setObjectName(QStringLiteral("ernc-serial-worker"));
    moveToThread(worker_);
    QEventLoop started;
    QObject::connect(worker_, &QThread::started, &started, &QEventLoop::quit);
    worker_->start();
    started.exec();
    QMetaObject::invokeMethod(this, &SerialTransport::createPort, Qt::BlockingQueuedConnection);
}

Result<void> SerialTransport::open() {
    if (!calledOnOwnerThread()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (port_ == nullptr) {
        return Result<void>::failure(ErrorCode::SerialOpenFailed);
    }
    if (open_) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (portName_.empty() || baudRate_ <= 0) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    port_->setPortName(QString::fromStdString(portName_));
    if (!port_->setBaudRate(baudRate_)) {
        return Result<void>::failure(ErrorCode::SerialOpenFailed);
    }
    if (!port_->open(QIODevice::ReadWrite)) {
        return Result<void>::failure(ErrorCode::SerialOpenFailed);
    }
    open_ = true;
    return Result<void>::success();
}

Result<void> SerialTransport::close() {
    if (!calledOnOwnerThread()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (!open_) {
        return Result<void>::success();
    }
    open_ = false;
    if (port_ != nullptr && port_->isOpen()) {
        port_->close();
    }
    incoming_.push_back(bareStep(TransportStep::Kind::Closed));
    return Result<void>::success();
}

Result<void> SerialTransport::submit(std::span<const std::uint8_t> bytes) {
    if (!calledOnOwnerThread()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (!open_ || port_ == nullptr || !port_->isOpen()) {
        return Result<void>::failure(ErrorCode::DeviceDisconnected);
    }
    if (bytes.empty()) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<qint64>::max())) {
        return Result<void>::failure(ErrorCode::InvalidParameter);
    }
    const qint64 written = port_->write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size()));
    // write 失败时 Qt 可能在返回前同步发出 errorOccurred。那里会再排入 Fault 并关闭端口。
    if (written != static_cast<qint64>(bytes.size())) {
        return Result<void>::failure(ErrorCode::SerialIoFailed);
    }
    return Result<void>::success();
}

TransportStep SerialTransport::poll() {
    if (!calledOnOwnerThread()) {
        return bareStep(TransportStep::Kind::Idle);
    }
    if (incoming_.empty()) {
        return bareStep(TransportStep::Kind::Idle);
    }
    TransportStep step = std::move(incoming_.front());
    incoming_.erase(incoming_.begin());
    return step;
}

bool SerialTransport::serialPortOnWorkerThread() const {
    if (!calledOnOwnerThread() || worker_ == nullptr || port_ == nullptr) {
        return false;
    }
    return port_->thread() == worker_ && thread() == worker_;
}

void SerialTransport::createPort() {
    port_ = new QSerialPort(this);
    QObject::connect(port_, &QSerialPort::readyRead, this, &SerialTransport::onReadyRead);
    QObject::connect(port_, &QSerialPort::errorOccurred, this, &SerialTransport::onErrorOccurred);
}

void SerialTransport::onReadyRead() {
    if (!open_ || port_ == nullptr) {
        return;
    }
    const QByteArray data = port_->readAll();
    if (data.isEmpty()) {
        return;
    }
    TransportStep step = bareStep(TransportStep::Kind::Bytes);
    step.bytes.assign(
        reinterpret_cast<const std::uint8_t*>(data.constData()),
        reinterpret_cast<const std::uint8_t*>(data.constData()) + data.size());
    incoming_.push_back(std::move(step));
}

void SerialTransport::onErrorOccurred(QSerialPort::SerialPortError error) {
    if (error == QSerialPort::NoError || !open_ || port_ == nullptr) {
        return;
    }
    TransportStep step = bareStep(TransportStep::Kind::Fault);
    step.error = error == QSerialPort::ResourceError ? ErrorCode::DeviceDisconnected
                                                      : ErrorCode::SerialIoFailed;
    open_ = false;
    incoming_.push_back(std::move(step));
    if (port_->isOpen()) {
        port_->close();
    }
}

void SerialTransport::retireOnWorker(QThread* destination) {
    open_ = false;
    if (port_ != nullptr) {
        port_->disconnect(this);
        if (port_->isOpen()) {
            port_->close();
        }
        delete port_;
        port_ = nullptr;
    }
    if (destination != nullptr && destination != thread()) {
        moveToThread(destination);
    }
}

bool SerialTransport::calledOnOwnerThread() const noexcept {
    return QThread::currentThread() == thread();
}

}  // namespace ernc::serial
