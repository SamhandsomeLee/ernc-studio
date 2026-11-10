#include "serial_transport.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

#include <cstdint>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::ports::IDeviceTransport;
using ernc::ports::TransportStep;
using ernc::serial::SerialTransport;
using ernc::serial::listSerialPortNames;

/// 构造用波特率，单位是 bit/s。
/// This number is a test fixture, not a confirmed device baud.
constexpr std::int32_t kFixtureBaud = 9600;

/// 不存在的端口名。只交给 SerialTransport，不打开 listSerialPortNames 返回的名字。
constexpr char kMissingPortName[] = "ERNCTEST_NO_SUCH_PORT";

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 失败结果的错误码等于期望值。成功对象只读 tryError()。
/// @param result 被测结果，只在本次调用期间借用。
/// @param expected 期望的错误码。
/// @return 非 ok 且 tryError() 指向 expected 时为 true。
bool failedWith(const Result<void>& result, ErrorCode expected) {
    const ErrorCode* error = result.tryError();
    return !result.ok() && error != nullptr && *error == expected;
}

/// @brief 成功结果没有可读取的错误码。
/// @param result 被测结果，只在本次调用期间借用。
/// @return ok 且 tryError() 为空时为 true。
bool succeeded(const Result<void>& result) {
    return result.ok() && result.tryError() == nullptr;
}

/// @brief 步骤是空闲，字节为空，错误码也为空。
/// @param step 一次 poll 的返回值，只在本次调用期间借用。
/// @return 三者同时成立时为 true。
bool isIdle(const TransportStep& step) {
    return step.kind == TransportStep::Kind::Idle && step.bytes.empty() && !step.error.has_value();
}

/// @brief 在 Worker 上执行 action，并阻塞到它返回。
///
/// 对象仍在调用方线程时直接返回 false，避免 BlockingQueuedConnection 在同一线程死锁。
/// @param transport 已 start 的对象，只在本次调用期间借用。调用方必须在构造线程。
/// @param action 无参可调用对象。它被移入 Worker 执行；其中捕获的引用只在本次调度期间借用。
/// @return invokeMethod 接受这次调度时为 true。
template <typename Action>
bool invokeOnWorker(SerialTransport& transport, Action action) {
    if (transport.thread() == QThread::currentThread()) {
        return false;
    }
    return QMetaObject::invokeMethod(
        &transport, std::move(action), Qt::BlockingQueuedConnection);
}

/// @brief 在 Worker 上 poll，并要求空闲、空字节、无错误码。
/// @param transport 已 start 的对象，只在本次调用期间借用。
/// @param name 失败时打印的用例名，只在本次调用期间借用。
/// @return 调度成功且步骤为空闲时为 true。
bool expectIdlePoll(SerialTransport& transport, const char* name) {
    TransportStep step;
    const bool invoked = invokeOnWorker(transport, [&transport, &step] { step = transport.poll(); });
    if (!invoked || !isIdle(step)) {
        return fail(name);
    }
    return true;
}

/// @brief 通过 IDeviceTransport 提交两字节夹具，不打开端口。
/// @param transport 借用的端口。调用方保证本次调用在该端口的所有者线程，且对象在调用期间存活。
/// @return submit 的结果。关闭时为 DeviceDisconnected。字节 0x01 0x03 只是测试夹具，不是已确认的设备命令。
Result<void> submitProbeBytes(IDeviceTransport& transport) {
    const std::uint8_t bytes[] = {0x01, 0x03};
    return transport.submit(bytes);
}

/// @brief Worker 上能看见 QSerialPort，构造线程上的直接调用看不到。
/// @return 两边结果都符合时为 true。
bool workerOwnsPort() {
    SerialTransport transport(kMissingPortName, kFixtureBaud);
    transport.start();
    bool owned = false;
    const bool invoked =
        invokeOnWorker(transport, [&transport, &owned] { owned = transport.serialPortOnWorkerThread(); });
    if (!invoked || !owned) {
        return fail("worker_owns_port");
    }
    if (transport.serialPortOnWorkerThread()) {
        return fail("worker_owns_port");
    }
    return true;
}

/// @brief 打不开的端口保持关闭，随后的 poll 仍是空闲。
/// @return open 为 SerialOpenFailed 且 poll 为空闲时为 true。
bool missingPortStaysClosed() {
    SerialTransport transport(kMissingPortName, kFixtureBaud);
    transport.start();
    Result<void> opened = Result<void>::failure(ErrorCode::Unknown);
    const bool invoked = invokeOnWorker(transport, [&transport, &opened] { opened = transport.open(); });
    if (!invoked || !failedWith(opened, ErrorCode::SerialOpenFailed)) {
        return fail("missing_port_stays_closed");
    }
    return expectIdlePoll(transport, "missing_port_stays_closed");
}

/// @brief 关闭状态下提交两字节得到 DeviceDisconnected，poll 为空闲。
/// @return 错误码和随后的空闲步骤都符合时为 true。
bool closedSubmit() {
    SerialTransport transport(kMissingPortName, kFixtureBaud);
    transport.start();
    const std::uint8_t bytes[] = {0x01, 0x03};
    Result<void> submitted = Result<void>::failure(ErrorCode::Unknown);
    const bool invoked =
        invokeOnWorker(transport, [&transport, &submitted, bytes] { submitted = transport.submit(bytes); });
    if (!invoked || !failedWith(submitted, ErrorCode::DeviceDisconnected)) {
        return fail("closed_submit");
    }
    return expectIdlePoll(transport, "closed_submit");
}

/// @brief 关闭状态下提交空序列得到 DeviceDisconnected，poll 为空闲。
/// @return 错误码和随后的空闲步骤都符合时为 true。
bool emptySubmitWhileClosed() {
    SerialTransport transport(kMissingPortName, kFixtureBaud);
    transport.start();
    const std::span<const std::uint8_t> empty;
    Result<void> submitted = Result<void>::failure(ErrorCode::Unknown);
    const bool invoked =
        invokeOnWorker(transport, [&transport, &submitted, empty] { submitted = transport.submit(empty); });
    if (!invoked || !failedWith(submitted, ErrorCode::DeviceDisconnected)) {
        return fail("empty_submit_while_closed");
    }
    return expectIdlePoll(transport, "empty_submit_while_closed");
}

/// @brief 已关闭时连续两次 close 都成功，并且都不产生步骤。
/// @return 两次都成功且每次之后 poll 都为空闲时为 true。
bool closeWhileClosed() {
    SerialTransport transport(kMissingPortName, kFixtureBaud);
    transport.start();
    Result<void> first = Result<void>::failure(ErrorCode::Unknown);
    const bool firstInvoked = invokeOnWorker(transport, [&transport, &first] { first = transport.close(); });
    if (!firstInvoked || !succeeded(first)) {
        return fail("close_while_closed");
    }
    if (!expectIdlePoll(transport, "close_while_closed")) {
        return false;
    }
    Result<void> second = Result<void>::failure(ErrorCode::Unknown);
    const bool secondInvoked =
        invokeOnWorker(transport, [&transport, &second] { second = transport.close(); });
    if (!secondInvoked || !succeeded(second)) {
        return fail("close_while_closed");
    }
    return expectIdlePoll(transport, "close_while_closed");
}

/// @brief 构造线程直接 open 被拒绝，不把调用派发到 Worker。
/// @return 错误码为 InvalidParameter 时为 true。
bool offWorkerOpen() {
    SerialTransport transport(kMissingPortName, kFixtureBaud);
    transport.start();
    const Result<void> opened = transport.open();
    if (!failedWith(opened, ErrorCode::InvalidParameter)) {
        return fail("off_worker_open");
    }
    return true;
}

/// @brief 枚举到的端口名都非空。空向量也算通过。不打开任何一个返回的名字。
/// @return 没有空名字时为 true。
bool listPortsHasNoEmptyName() {
    const std::vector<std::string> names = listSerialPortNames();
    for (const std::string& name : names) {
        if (name.empty()) {
            return fail("list_ports_has_no_empty_name");
        }
    }
    return true;
}

/// @brief 经 IDeviceTransport 在 Worker 上提交两字节，关闭状态被拒绝。
/// @return DeviceDisconnected 且随后 poll 为空闲时为 true。
bool interfaceClosedSubmit() {
    SerialTransport transport(kMissingPortName, kFixtureBaud);
    transport.start();
    Result<void> submitted = Result<void>::failure(ErrorCode::Unknown);
    const bool invoked =
        invokeOnWorker(transport, [&transport, &submitted] { submitted = submitProbeBytes(transport); });
    if (!invoked || !failedWith(submitted, ErrorCode::DeviceDisconnected)) {
        return fail("interface_closed_submit");
    }
    return expectIdlePoll(transport, "interface_closed_submit");
}

/// @brief 按用例名运行一个检查。
/// @param name CTest 传入的用例名，只在本次调用期间借用。
/// @param recognized 用例名存在时设为 true。
/// @return 该用例通过时为 true。
bool runCase(std::string_view name, bool& recognized) {
    recognized = true;
    if (name == "worker_owns_port") {
        return workerOwnsPort();
    }
    if (name == "missing_port_stays_closed") {
        return missingPortStaysClosed();
    }
    if (name == "closed_submit") {
        return closedSubmit();
    }
    if (name == "empty_submit_while_closed") {
        return emptySubmitWhileClosed();
    }
    if (name == "close_while_closed") {
        return closeWhileClosed();
    }
    if (name == "off_worker_open") {
        return offWorkerOpen();
    }
    if (name == "list_ports_has_no_empty_name") {
        return listPortsHasNoEmptyName();
    }
    if (name == "interface_closed_submit") {
        return interfaceClosedSubmit();
    }
    recognized = false;
    return false;
}

}  // namespace

/// @brief 运行一个串口传输单元测试。
/// @param argc 参数个数，包含程序名。必须等于 2。
/// @param argv argv[1] 是用例名。Qt 只看到 argv[0]，避免吃掉用例名。
/// @return 用例通过时为 0。用例失败时为 1，参数不对时为 2。
int main(int argc, char** argv) {
    if (argc != 2 || argv == nullptr || argv[1] == nullptr) {
        std::cerr << "usage: ernc_serial_transport_tests <case>\n";
        return 2;
    }
    int qtArgc = 1;
    QCoreApplication app(qtArgc, argv);
    const std::string_view caseName = argv[1];
    bool recognized = false;
    if (!runCase(caseName, recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << caseName << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
