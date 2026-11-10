#pragma once

#include "ernc/ports/device_transport.hpp"

#include <QObject>
#include <QSerialPort>

#include <cstdint>
#include <string>
#include <vector>

class QThread;

namespace ernc::serial {

/// @brief 列出本机可见的串口名，不打开其中任何一个。
///
/// 名字来自 QSerialPortInfo::portName，例如 COM3。空向量表示当前没有枚举到端口。
/// 本函数不创建 Worker，也不调用 open。需要进程里已经有 QCoreApplication。
/// @return 调用方拥有的端口名副本。不会为了补齐结果而生成假端口名。
std::vector<std::string> listSerialPortNames();

/// @brief 在专属 Worker 线程上用 QSerialPort 实现字节端口。
///
/// 构造线程可以创建对象，但此时还没有 QSerialPort。start 把本对象移到新线程，
/// 并在那个线程里创建 QSerialPort。open、close、submit、poll 之后只能在该 Worker 上调用。
/// 从别的线程直接调用时，返回 Result 的方法给出 InvalidParameter，poll 返回 Idle 且不取出队列。
/// 析构必须发生在构造时所在的线程。对象不设 QObject parent，以便 moveToThread。
/// 端口名和波特率由调用方传入。数据位、校验和停止位保持 QSerialPort 新建后的默认值，
/// 不把它们写成已经确认的设备参数。适配器不拆帧，也不识别 Modbus。
class SerialTransport : public QObject, public ernc::ports::IDeviceTransport {
    Q_OBJECT

public:
    /// @brief 保存端点参数，不打开串口，也不启动线程。
    /// @param portName 端口名，按原样交给 QSerialPort。空字符串会在 open 时被拒绝。
    /// @param baudRate 波特率，单位是 bit/s。小于或等于 0 会在 open 时被拒绝。
    SerialTransport(std::string portName, std::int32_t baudRate);

    /// @brief 在构造线程上关闭并销毁 Worker 中的 QSerialPort，然后结束线程。
    ///
    /// 未调用 start 时只销毁本对象。调用时所在线程必须是构造线程。
    ~SerialTransport() override;

    /// @brief 启动 Worker，并等待 QSerialPort 在该线程创建完成。
    ///
    /// 只能从构造线程调用，且进程中必须已有 QCoreApplication。重复调用不再创建第二个线程。
    /// 条件不满足时直接返回，随后的 open 会因为还没有端口而失败。
    void start();

    /// @brief 在 Worker 上打开构造时指定的端口。
    /// @return 成功只表示本地串口已打开。重复打开为 InvalidParameter。
    /// 端口名为空、波特率无效或调用线程不是 Worker 时为 InvalidParameter。
    /// 端口不存在或系统拒绝打开时为 SerialOpenFailed，状态保持关闭。
    ernc::domain::Result<void> open() override;

    /// @brief 在 Worker 上关闭串口。
    /// @return 已关闭时再次调用仍成功，且不追加 Closed。
    /// 从打开状态关闭时，在接收队列末尾追加一次 Closed。已经排队的 Bytes 仍排在它前面。
    /// 调用线程不是 Worker 时为 InvalidParameter，串口保持原状态。
    ernc::domain::Result<void> close() override;

    /// @brief 在 Worker 上把整段线字节交给 QSerialPort::write。
    /// @param bytes 待发送字节，只在本次调用期间借用。空序列在端口已打开时被拒绝。
    /// @return 写调用收下全部字节时成功。成功不表示设备已经响应，也不产生 Bytes。
    /// 未打开时为 DeviceDisconnected。空序列在已打开时为 InvalidParameter。
    /// 一个字节都没写完时为 SerialIoFailed，不产生 Bytes。
    ernc::domain::Result<void> submit(std::span<const std::uint8_t> bytes) override;

    /// @brief 在 Worker 上取走接收队列中的下一步。
    /// @return 队列为空时为 Idle。Bytes 携带读到的原始线字节。Fault 携带错误码。
    /// Closed 的字节和错误码都空。调用线程不是 Worker 时返回 Idle，且不取出已有步骤。
    ernc::ports::TransportStep poll() override;

    /// @brief 报告 QSerialPort 是否已在 Worker 线程上创建。
    ///
    /// 必须在 Worker 线程调用。未 start，或当前线程不是 Worker 时为 false。
    /// @return port 的所属线程与 Worker 相同时为 true。
    bool serialPortOnWorkerThread() const;

private slots:
    /// @brief 在 Worker 线程创建 QSerialPort 并接上读与错误信号。
    void createPort();

    /// @brief 把 readyRead 读到的原始字节排入接收队列。
    void onReadyRead();

    /// @brief 打开之后的串口错误变成 Fault，并使本地端口回到关闭。
    /// @param error Qt 报告的串口错误。NoError 被忽略。
    void onErrorOccurred(QSerialPort::SerialPortError error);

    /// @brief 在 Worker 上删除 QSerialPort，再把本对象移回构造线程。
    /// @param destination 构造时所在的线程。调用方保证它仍存活。
    void retireOnWorker(QThread* destination);

private:
    /// @brief 当前调用是否发生在本对象现在所属的线程。
    /// @return 是该线程时为 true。
    bool calledOnOwnerThread() const noexcept;

    /// 构造时传入的端口名。open 之前不会被使用。
    std::string portName_;

    /// 构造时传入的波特率，单位是 bit/s。
    std::int32_t baudRate_{0};

    /// 构造时所在的线程。析构和 start 必须在这个线程。
    QThread* ownerThread_{nullptr};

    /// Worker 线程对象。它本身留在构造线程，不作为本对象的子对象。
    QThread* worker_{nullptr};

    /// 只在 Worker 上创建和销毁的串口。未 start 时为空。
    QSerialPort* port_{nullptr};

    /// 本地是否已成功打开。新建时为 false。
    bool open_{false};

    /// poll 尚未取走的步骤。只在 Worker 上访问。
    std::vector<ernc::ports::TransportStep> incoming_;
};

}  // namespace ernc::serial
