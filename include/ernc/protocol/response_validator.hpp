#pragma once

#include "ernc/domain/types.hpp"
#include "ernc/protocol/modbus_fc16.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace ernc::protocol {

/// 当前要核对的那一条命令。调用方传入，本类型不保存为在途状态。
enum class CommandKind {
    /// FC03 读。正常功能码是 0x03，异常功能码是 0x83。
    ReadHolding,

    /// FC16 写。正常功能码是 0x10，异常功能码是 0x90。
    WriteMultiple
};

/// @brief 一条已经发出的读或写命令所期望的响应。
///
/// 只描述期望，不持有线字节，也不访问设备。数量为 0、读超过 62 或写超过 61 时，校验直接拒绝。
struct CommandExpectation {
    /// 读还是写。决定可以接受的功能码。
    CommandKind kind{CommandKind::ReadHolding};

    /// 起始逻辑 slot。FC03 响应里没有寄存器地址，因此读响应不回显这个字段。写 ACK 必须回显 slot 乘 2。
    std::uint16_t firstSlot{0};

    /// 期望的 slot 数。读响应的数据字节数必须是这个值乘 4。写 ACK 的寄存器数量必须是这个值乘 2。
    std::uint16_t slotCount{0};

    /// 期望的从站地址。默认是已确认的 0x01。响应里的从站字节必须与它相同。
    std::uint8_t slaveAddress{kConfirmedSlaveAddress};
};

/// @brief 与读命令匹配的正常 FC03 响应。
///
/// 数据保持线上字节顺序。没有 Verified 标志，也不更新 deviceValue。
struct AcceptedRead {
    /// 已经和命令比对过的从站地址。
    std::uint8_t slaveAddress{0};

    /// 功能码之后的数据区。长度是 slotCount 乘 4。调用方拥有这份副本。
    std::vector<std::uint8_t> payload;

    /// payload 中的 slot 数，等于命令里的 slotCount。
    std::uint16_t slotCount{0};
};

/// @brief 与写命令匹配的正常 FC16 ACK。
///
/// 只含回显的从站、起始寄存器和寄存器数量。这不是读回 Verified。
struct AcceptedWriteAck {
    /// 已经和命令比对过的从站地址。
    std::uint8_t slaveAddress{0};

    /// ACK 中的起始寄存器。它等于命令的 firstSlot 乘 2。
    std::uint16_t startRegister{0};

    /// ACK 中的寄存器数量。它等于命令的 slotCount 乘 2。
    std::uint16_t registerCount{0};
};

/// 校验接受的两种正常响应。异常应答不会放在这里。
using AcceptedResponse = std::variant<AcceptedRead, AcceptedWriteAck>;

/// @brief 一帧响应相对当前命令的校验结论。
///
/// accepted() 为 true 只表示功能码、长度、从站和地址数量都与命令一致。
/// 异常应答的 accepted() 为 false，错误码是 ModbusException，同时能读到异常码。
/// 本对象不注册回调，也不修改 RuntimeStore。
class ResponseVerdict {
public:
    /// @brief 做成已接受的结论。
    /// @param response 与命令匹配的正常读数据或写 ACK。按值保存，调用方仍拥有自己的那份。
    /// @return accepted() 为 true。error() 和 exceptionCode() 为空。
    static ResponseVerdict accept(AcceptedResponse response);

    /// @brief 做成不含异常码的拒绝结论。
    /// @param code 失败类别。不要用它表示一个已经取到的 Modbus 异常码；那种情况用 rejectException。
    /// @return accepted() 为 false，error() 指向该类别。
    static ResponseVerdict reject(ernc::domain::ErrorCode code);

    /// @brief 做成 Modbus 异常拒绝，并保留异常码。
    /// @param code 响应里的异常码，例如 0x02。它不是 ErrorCode 的整数值。
    /// @return accepted() 为 false，error() 为 ModbusException，exceptionCode() 指向这个字节。
    static ResponseVerdict rejectException(std::uint8_t code);

    /// @brief 报告这帧是否被接受为当前命令的正常完成。
    /// @return 只有正常读数据或写 ACK 匹配时为 true。异常、CRC 错误和字段不符都是 false。
    bool accepted() const noexcept;

    /// @brief 在接受时借出正常响应。
    /// @return 接受时指向内部响应，拒绝时为空。指针不转移所有权，只在本对象存活期间有效。
    const AcceptedResponse* response() const noexcept;

    /// @brief 在拒绝时借出错误类别。
    /// @return 拒绝时指向错误码，接受时为空。指针不转移所有权，只在本对象存活期间有效。
    const ernc::domain::ErrorCode* error() const noexcept;

    /// @brief 在 Modbus 异常拒绝时借出异常码。
    /// @return 仅当错误码是 ModbusException 且帧里带了异常码时非空。指针不转移所有权。
    const std::uint8_t* exceptionCode() const noexcept;

private:
    /// 没有公开的空结论，避免调用方把未校验状态当成接受。
    ResponseVerdict() = default;

    /// true 时 response_ 有效，error_ 不对外暴露。
    bool accepted_{false};

    /// 拒绝时的错误类别。接受时不使用。
    ernc::domain::ErrorCode error_{ernc::domain::ErrorCode::Unknown};

    /// Modbus 异常码。只有拒绝且类别为 ModbusException 时有值。
    std::optional<std::uint8_t> exceptionCode_;

    /// 接受时的正常响应。拒绝时为空。
    std::optional<AcceptedResponse> response_;
};

/// @brief 判断一帧带长度前缀的响应是否配得上当前命令。
///
/// 先看长度字节，再核对内层 CRC，然后比对功能码、从站，以及读数据长度或写回显地址。
/// 读命令只接受 0x03 和异常码 0x83。写命令只接受 0x10 和异常码 0x90。
/// 证据等级：字段来自旧版 decode_modbus_rtu 能看到的响应布局。没有已批准 Golden。不访问串口。
/// @param command 当前命令的期望，只在本次调用期间借用。
/// @param wire 含 1 字节长度前缀和 CRC 的整帧，只在本次调用期间借用。长度字节等于后续字节数。
/// @return 匹配时 accepted() 为 true。长度不符为 FrameLengthInvalid，CRC 不符为 CrcMismatch。
/// 功能码、从站或写回显地址不符为 ResponseMismatch。读数据长度不符为 PayloadSizeMismatch。
/// 命令范围本身非法时为 InvalidParameter。对应的异常帧为 ModbusException，并可读取异常码。
ResponseVerdict validateResponse(const CommandExpectation& command, std::span<const std::uint8_t> wire);

}  // namespace ernc::protocol
