#include "serial_gate.hpp"

#include "profile.hpp"
#include "serial_transport.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QThread>

namespace ernc::lab {

namespace {

using ernc::domain::ErrorCode;
using ernc::domain::Result;
using ernc::serial::SerialTransport;

/// @brief 在 Worker 上执行动作，并阻塞到它返回。
///
/// 对象仍在当前线程时直接返回 false，避免 BlockingQueuedConnection 把自己卡住。
/// @param transport 已经 start 的端口，只在本次调用期间借用。调用方必须在构造线程。
/// @param action 无参可调用对象。它被移入 Worker；捕获的引用只在这次调度期间借用。
/// @return invokeMethod 接受这次调度时为 true。
template <typename Action>
bool invokeOnWorker(SerialTransport& transport, Action action) {
    if (transport.thread() == QThread::currentThread()) {
        return false;
    }
    return QMetaObject::invokeMethod(&transport, std::move(action), Qt::BlockingQueuedConnection);
}

/// @brief 在 Worker 上打开档案指定的端口，成功后立刻关闭。
/// @param profile 已经判定为可用的档案，只在本次调用期间借用。
/// @return open 没有成功时为 Blocked。打开后又关闭、且没有 submit 时为 NoByteExchange。
SerialGate openThenClose(const SerialProfile& profile) {
    int argc = 1;
    char program[] = "ernc-comm-lab";
    char* argv[] = {program, nullptr};
    QCoreApplication application(argc, argv);

    SerialTransport transport(profile.port, profile.baudRate);
    transport.start();
    Result<void> opened = Result<void>::failure(ErrorCode::SerialOpenFailed);
    const bool invoked = invokeOnWorker(transport, [&transport, &opened] { opened = transport.open(); });
    if (!invoked || !opened.ok() || opened.tryError() != nullptr) {
        return SerialGate::Blocked;
    }
    const bool closed = invokeOnWorker(transport, [&transport] { (void)transport.close(); });
    if (!closed) {
        return SerialGate::Blocked;
    }
    return SerialGate::NoByteExchange;
}

}  // namespace

SerialGate runSerialGate(const std::string& profilePath) {
    if (profilePath.empty()) {
        return SerialGate::Blocked;
    }
    SerialProfile profile;
    if (!loadSerialProfile(profilePath, profile) || !profile.usable) {
        return SerialGate::Blocked;
    }
    return openThenClose(profile);
}

}  // namespace ernc::lab
