#include "slave_image.hpp"

#include "ernc/protocol/crc16.hpp"

#include <cstddef>
#include <cstdint>

namespace ernc::simulator {
namespace {

constexpr std::uint8_t kSlaveAddress = 0x01;
constexpr std::uint8_t kReadFunction = 0x03;
constexpr std::uint8_t kWriteFunction = 0x10;
constexpr std::uint16_t kMaxReadSlots = 62;
constexpr std::uint16_t kMaxWriteSlots = 61;
constexpr std::size_t kBytesPerSlot = 4;

/// @brief 读一个大端 16 位字段。
/// @param bytes 至少含 offset 之后两个字节的缓冲，只在本次调用期间借用。
/// @param offset 高字节下标。
/// @return 大端数值。
std::uint16_t readBe16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                      bytes[offset + 1U]);
}

/// @brief 把寄存器数量换成 slot 数。奇数或未对齐的起始寄存器不是合法 slot 范围。
/// @param startRegister 请求里的起始寄存器。
/// @param registerCount 请求里的寄存器数量。
/// @param firstSlot 成功时写入起始 slot。
/// @param slotCount 成功时写入 slot 数。
/// @return 两者都能被 2 整除且数量非 0 时为 true。
bool slotsFromRegisters(std::uint16_t startRegister, std::uint16_t registerCount, std::uint16_t& firstSlot,
                        std::uint16_t& slotCount) {
    if (registerCount == 0 || (registerCount % 2U) != 0U || (startRegister % 2U) != 0U) {
        return false;
    }
    firstSlot = static_cast<std::uint16_t>(startRegister / 2U);
    slotCount = static_cast<std::uint16_t>(registerCount / 2U);
    return true;
}

/// @brief 给 Modbus 本体加上长度前缀。
/// @param modbus 已含 CRC 的本体，只在本次调用期间借用。长度必须在 1 到 255 之间。
/// @return 调用方拥有的整帧。本体为空或长于 255 时为空。
std::vector<std::uint8_t> prefixLength(std::span<const std::uint8_t> modbus) {
    if (modbus.empty() || modbus.size() > 255U) {
        return {};
    }
    std::vector<std::uint8_t> frame;
    frame.reserve(modbus.size() + 1U);
    frame.push_back(static_cast<std::uint8_t>(modbus.size()));
    frame.insert(frame.end(), modbus.begin(), modbus.end());
    return frame;
}

/// @brief 处理 FC03。范围不合法时不读镜像。
/// @param image 从站内存，只在本次调用期间借用。
/// @param body 含 CRC 的 Modbus 本体。长度必须是 8。
/// @return 合法时为带长度前缀的读响应。否则为空。
std::vector<std::uint8_t> replyRead(const SlaveImage& image, std::span<const std::uint8_t> body) {
    if (body.size() != 8U) {
        return {};
    }
    std::uint16_t firstSlot = 0;
    std::uint16_t slotCount = 0;
    if (!slotsFromRegisters(readBe16(body, 2), readBe16(body, 4), firstSlot, slotCount) ||
        slotCount > kMaxReadSlots || !image.contains(firstSlot, slotCount)) {
        return {};
    }
    const std::vector<std::uint8_t> data = image.read(firstSlot, slotCount);
    std::vector<std::uint8_t> payload;
    payload.reserve(3U + data.size());
    payload.push_back(kSlaveAddress);
    payload.push_back(kReadFunction);
    payload.push_back(static_cast<std::uint8_t>(data.size()));
    payload.insert(payload.end(), data.begin(), data.end());
    return prefixLength(ernc::protocol::appendCrc16(payload));
}

/// @brief 处理 FC16。校验通过后才写入镜像。
/// @param image 从站内存。成功时被修改。
/// @param body 含 CRC 的 Modbus 本体。
/// @return 合法时为带长度前缀的 ACK。失败时 image 不变，返回空。
std::vector<std::uint8_t> replyWrite(SlaveImage& image, std::span<const std::uint8_t> body) {
    if (body.size() < 9U) {
        return {};
    }
    const std::uint8_t byteCount = body[6];
    const std::size_t dataBytes = body.size() - 9U;
    if (dataBytes != byteCount) {
        return {};
    }
    std::uint16_t firstSlot = 0;
    std::uint16_t slotCount = 0;
    if (!slotsFromRegisters(readBe16(body, 2), readBe16(body, 4), firstSlot, slotCount) ||
        slotCount > kMaxWriteSlots || byteCount != slotCount * kBytesPerSlot ||
        !image.contains(firstSlot, slotCount)) {
        return {};
    }
    const std::span<const std::uint8_t> data{body.data() + 7, byteCount};
    if (!image.write(firstSlot, data)) {
        return {};
    }
    const std::uint8_t ack[] = {kSlaveAddress, kWriteFunction, body[2], body[3], body[4], body[5]};
    return prefixLength(ernc::protocol::appendCrc16(ack));
}

}  // namespace

SlaveImage::SlaveImage(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

std::size_t SlaveImage::slotCount() const noexcept {
    if ((bytes_.size() % kBytesPerSlot) != 0U) {
        return 0;
    }
    return bytes_.size() / kBytesPerSlot;
}

std::span<const std::uint8_t> SlaveImage::bytes() const noexcept {
    return bytes_;
}

bool SlaveImage::contains(std::uint16_t firstSlot, std::uint16_t count) const noexcept {
    if (count == 0) {
        return false;
    }
    const std::size_t end = static_cast<std::size_t>(firstSlot) + count;
    return end >= firstSlot && end <= slotCount();
}

std::vector<std::uint8_t> SlaveImage::read(std::uint16_t firstSlot, std::uint16_t count) const {
    if (!contains(firstSlot, count)) {
        return {};
    }
    const std::size_t offset = static_cast<std::size_t>(firstSlot) * kBytesPerSlot;
    const std::size_t length = static_cast<std::size_t>(count) * kBytesPerSlot;
    return std::vector<std::uint8_t>(bytes_.begin() + static_cast<std::ptrdiff_t>(offset),
                                     bytes_.begin() + static_cast<std::ptrdiff_t>(offset + length));
}

bool SlaveImage::write(std::uint16_t firstSlot, std::span<const std::uint8_t> data) {
    if (!contains(firstSlot, static_cast<std::uint16_t>(data.size() / kBytesPerSlot)) ||
        data.size() != (static_cast<std::size_t>(data.size() / kBytesPerSlot) * kBytesPerSlot)) {
        return false;
    }
    const std::uint16_t count = static_cast<std::uint16_t>(data.size() / kBytesPerSlot);
    if (!contains(firstSlot, count) || data.size() != static_cast<std::size_t>(count) * kBytesPerSlot) {
        return false;
    }
    const std::size_t offset = static_cast<std::size_t>(firstSlot) * kBytesPerSlot;
    for (std::size_t index = 0; index < data.size(); ++index) {
        bytes_[offset + index] = data[index];
    }
    return true;
}

SlaveReply exchange(SlaveImage& image, std::span<const std::uint8_t> request) {
    SlaveReply reply;
    if (request.size() < 2U || static_cast<std::size_t>(request[0]) + 1U != request.size()) {
        return reply;
    }
    const std::span<const std::uint8_t> body{request.data() + 1, request[0]};
    if (!ernc::protocol::verifyCrc16(body).ok() || body.size() < 2U || body[0] != kSlaveAddress) {
        return reply;
    }
    std::vector<std::uint8_t> frame;
    if (body[1] == kReadFunction) {
        frame = replyRead(image, body);
    } else if (body[1] == kWriteFunction) {
        frame = replyWrite(image, body);
    }
    if (frame.empty()) {
        return reply;
    }
    reply.responded = true;
    reply.frame = std::move(frame);
    return reply;
}

}  // namespace ernc::simulator
