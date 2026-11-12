#pragma once

#include "ernc/domain/firmware_layout.hpp"
#include "ernc/domain/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ernc::domain {

/// @brief 证据等级是否允许真实 FC03 区域读。
/// @param evidence locate() 保留的证据等级。
/// @return Pack4Sequential 表示与 CHNB `ERNCUppInit_Para` 及 `firmware/layout_manifest.json` 对齐的基线几何，允许像旧版一样整块读。
/// Verified 表示独立复核后的几何。二者都不是已经发出的 FC03。
constexpr bool realReadPermitted(LayoutEvidence evidence) noexcept {
    return evidence == LayoutEvidence::Verified || evidence == LayoutEvidence::Pack4Sequential;
}

/// @brief 证据等级是否允许真实 FC16 区域写。
/// @param evidence locate() 保留的证据等级。
/// @param writable manifest 标量 writable。标量批量写计划据此排除目录字段；区域工作台走独立 preview+读回验证，不依赖该位。
/// @return Pack4Sequential 基线区域为 true。Verified 还需 writable 为 true。这不是已经发出的 FC16。
constexpr bool realWritePermitted(LayoutEvidence evidence, bool writable) noexcept {
    if (evidence == LayoutEvidence::Pack4Sequential) {
        return true;
    }
    return evidence == LayoutEvidence::Verified && writable;
}

/// 区域中的一个直接子字段。下标和轴名称不在这里。
struct SubRegion {
    /// manifest 中的完整路径。例如 ERNC_TF.SpecRadius。
    std::string path;

    /// 起始逻辑 slot。不是 Modbus 寄存器地址。
    std::uint32_t slot{0};

    /// 连续占用的 slot 数。一个 slot 是 4 字节。
    std::uint32_t slotCount{0};

    /// 字节长度。等于 slotCount 乘 4。
    std::uint32_t size{0};

    /// 相对布局 base 的字节偏移。不是报文地址。
    std::uint32_t offset{0};

    /// manifest 的 type 原文。
    std::string typeName;

    /// float、int32 或 uint32。结构体类型为空。
    std::optional<DataType> scalarType;

    /// 各级长度。空表示 manifest 没有给出数组维度。
    std::vector<std::uint32_t> shape;

    /// 这个子字段自己的证据等级。
    LayoutEvidence evidence{LayoutEvidence::Pack4Sequential};

    /// 这个子字段自己的 writable。
    bool writable{false};
};

/// 一块区域及其直接子字段。对象拥有路径和子字段副本。
struct RegionDefinition {
    /// 区域路径，也是目录键。ERNC_TF、check_data 或 ERNC_TF.SpecRadius。
    std::string path;

    /// 起始逻辑 slot。不是 Modbus 寄存器地址。
    std::uint32_t slot{0};

    /// 连续占用的 slot 数。
    std::uint32_t slotCount{0};

    /// 字节长度。等于 slotCount 乘 4。
    std::uint32_t size{0};

    /// 相对布局 base 的字节偏移。
    std::uint32_t offset{0};

    /// manifest 的 type 原文。父区域可以是结构体名。
    std::string typeName;

    /// 标量类型。结构体父区域为空。
    std::optional<DataType> scalarType;

    /// 区域自己的 shape。结构体父区域通常为空。
    std::vector<std::uint32_t> shape;

    /// 区域自己的证据等级。
    LayoutEvidence evidence{LayoutEvidence::Pack4Sequential};

    /// 区域自己的 writable。
    bool writable{false};

    /// 按 manifest 顺序排列的直接子字段。SpecRadius 没有下一级。
    std::vector<SubRegion> subregions;
};

/// @brief ERNC_TF、check_data 和 SpecRadius 的几何目录。
///
/// 对象拥有三块区域。find 返回的指针指向内部记录，目录被移动或销毁后失效。
/// 不组帧，不访问设备，也不包含 Qt。构建失败时不留下半份目录。
class RegionCatalog {
public:
    /// @brief 从已加载的布局建立三块区域。
    /// @param layout 固件布局，只在本次调用期间借用。几何来自 locate()，维度来自它的 shape。
    /// @return 三块区域都存在，且有子字段的区域被子字段首尾相接盖满时成功。
    /// 缺路径、locate 失败或子字段对不齐时为 InvalidParameter，没有部分目录。
    static Result<RegionCatalog> load(const FirmwareLayout& layout);

    /// @brief 按固件路径查找区域。
    /// @param path ERNC_TF、check_data 或 ERNC_TF.SpecRadius。只在本次调用期间借用。
    /// @return 找到时指向内部区域，不转移所有权。其他路径为空指针。
    const RegionDefinition* find(std::string_view path) const noexcept;

    /// @brief 目录中的区域个数。
    /// @return 成功建立的目录固定为 3。
    std::size_t size() const noexcept;

private:
    /// @brief 接管已经检查过的三块区域。
    /// @param regions 按 ERNC_TF、check_data、SpecRadius 的顺序。函数接管这份向量。
    explicit RegionCatalog(std::vector<RegionDefinition> regions);

    /// 三块区域。本对象拥有。
    std::vector<RegionDefinition> regions_;
};

}  // namespace ernc::domain
