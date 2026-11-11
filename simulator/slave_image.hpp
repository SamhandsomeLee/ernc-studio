#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace ernc::simulator {

/// @brief 虚拟从站的槽内存。
///
/// 每 4 字节是一个逻辑 slot。对象拥有这份字节。它不是整机固件镜像。
/// 字节数不是 4 的倍数时，可用 slot 数为 0，读写都不会改动原字节。
class SlaveImage {
public:
    /// @brief 用调用方给出的初始字节构造镜像。
    /// @param bytes 初始内容，按值接收。函数持有自己的副本。空向量表示没有 slot。
    explicit SlaveImage(std::vector<std::uint8_t> bytes);

    /// @brief 可用的完整 slot 数。
    /// @return 字节数除以 4。余数不为 0 时为 0。
    std::size_t slotCount() const noexcept;

    /// @brief 借出当前全部字节。
    /// @return 指向内部缓冲的只读视图，生存期与本对象相同。
    std::span<const std::uint8_t> bytes() const noexcept;

    /// @brief 报告一块连续 slot 是否完全落在镜像内。
    /// @param firstSlot 起始逻辑 slot。
    /// @param count 连续 slot 数。0 不视为落在镜像内。
    /// @return 范围没有越过末尾时为 true。
    bool contains(std::uint16_t firstSlot, std::uint16_t count) const noexcept;

    /// @brief 复制一块连续 slot 的原始字节。
    /// @param firstSlot 起始逻辑 slot。
    /// @param count 连续 slot 数。范围必须已经由 contains 接受。
    /// @return 调用方拥有的副本，长度是 count 乘 4。范围不在镜像内时为空。
    std::vector<std::uint8_t> read(std::uint16_t firstSlot, std::uint16_t count) const;

    /// @brief 把字节写入一块连续 slot。
    /// @param firstSlot 起始逻辑 slot。
    /// @param data 要写入的字节，只在本次调用期间借用。长度必须是 4 的倍数，并且正好覆盖这些 slot。
    /// @return 范围合法且长度匹配时为 true，镜像被整段替换。否则为 false，镜像不变。
    bool write(std::uint16_t firstSlot, std::span<const std::uint8_t> data);

private:
    /// 从站内存。下标 0 是 slot 0 的第一个字节。
    std::vector<std::uint8_t> bytes_;
};

/// @brief 从站对一帧请求的处理结果。
///
/// responded 为 false 时 frame 为空，镜像没有被这次调用修改。
struct SlaveReply {
    /// 从站是否给出了响应帧。false 不是成功数据区。
    bool responded{false};

    /// 带长度前缀的响应。调用方拥有这份副本。没有响应时为空。
    std::vector<std::uint8_t> frame;
};

/// @brief 用一帧长度前缀请求访问从站内存。
///
/// 只接受从站地址 0x01 的 FC03 和 FC16。FC03 最多 62 个 slot，FC16 最多 61 个 slot。
/// 相同输入得到相同输出。不读取时钟，也不访问串口。
/// @param image 从站内存。合法 FC16 会修改它。只在本次调用期间借用。
/// @param request 带长度前缀的请求，只在本次调用期间借用。
/// @return 合法请求时 responded 为 true。FC03 的数据区来自 image。FC16 只表示数据已写入。
/// 地址、CRC、功能码、数量或范围不合法时 responded 为 false，image 保持进入函数前的内容。
SlaveReply exchange(SlaveImage& image, std::span<const std::uint8_t> request);

}  // namespace ernc::simulator
