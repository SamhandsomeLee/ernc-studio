#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ernc::lab {

/// @brief 实验台从调用方档案里读到的串口限制。
///
/// 端口名、波特率、从站和槽位只来自档案正文。缺任何必填项时 usable 为 false。
/// 本结构不提供设备默认波特率，也不代表已经冻结的 6MIC 档案。
struct SerialProfile {
    /// loadSerialProfile 返回 true 时为 true。
    bool usable{false};

    /// 档案里的端口名。只交给 QSerialPort，不从本机枚举里挑选。
    std::string port;

    /// 档案里的波特率，单位是 bit/s。未写明时保持 0，调用方不得据此打开端口。
    std::int32_t baudRate{0};

    /// 档案里的从站地址字节。可用范围是 1 到 247，不含广播地址。
    int slave{0};

    /// 允许读取的 slot。空列表使整份档案不可用。个数不超过一次 FC03。
    std::vector<int> readSlots;

    /// 允许写入的 slot。缺省或空列表表示没有可写槽位。个数不超过一次 FC16。
    std::vector<int> writeSlots;
};

/// @brief 读取一份实验台串口档案。
///
/// 打不开、超过 64 KiB、不是单一 JSON 对象，或必填项缺失时返回 false。
/// 未知键也拒绝，避免把没看懂的字段当成默认值。
/// @param path 档案路径。调用方拥有这个字符串。
/// @param profile 成功时写入可用档案。失败时 usable 为 false，其余字段无意义。
/// @return 档案可以用来决定是否打开端口时为 true。这不表示端口已经打开。
bool loadSerialProfile(const std::string& path, SerialProfile& profile);

}  // namespace ernc::lab
