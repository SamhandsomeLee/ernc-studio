#pragma once

#include <string>

namespace ernc::lab {

/// @brief 串口入口的两种停止方式。两种都不提交线字节。
enum class SerialGate {
    /// 没有可用档案，或 Worker 上的 open 没有成功。调用方退出码为 3。
    Blocked,

    /// 端口曾经打开，并已在 Worker 上关闭。没有读期望，也不提交写入。调用方退出码为 2。
    NoByteExchange,
};

/// @brief 按档案决定能否打开串口，打开后立即关闭。
///
/// 档案路径为空、文件不可用，或只读白名单、写白名单、从站、波特率不合格时，
/// 不创建 QSerialPort。结构合格时只在 SerialTransport 的 Worker 上调用 open。
/// open 失败保持 Blocked。open 成功后调用 close，不调用 submit。
/// @param profilePath 档案路径。空字符串表示调用方没有传入 --profile。
/// @return Blocked 或 NoByteExchange。不会返回一个可以记成 PASS 的结果。
SerialGate runSerialGate(const std::string& profilePath);

}  // namespace ernc::lab
