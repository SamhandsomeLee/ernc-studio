#pragma once

#include <cstddef>
#include <iterator>
#include <string_view>

namespace ernc::presentation {

/// 设计 v1.2 第 17.4 节的一条浅色令牌。数值是 ERNC Studio 自己的建议初值，不是设备参数，也不是 Siemens 官方色。
struct ThemeToken {
    /// 令牌名，例如 `surface.app`。只用于对照设计表，不是对象路径或参数 ID。
    std::string_view name;

    /// `#RRGGBB` 形式的颜色。大小写与设计表一致。
    std::string_view hex;
};

/// 浅色主题的全部颜色。顺序与设计 v1.2 第 17.4 节表格一致。暗色主题不在本切片。
inline constexpr ThemeToken kLightThemeTokens[] = {
    {"surface.app", "#F2F4F5"},
    {"surface.panel", "#FFFFFF"},
    {"surface.header", "#E7EBEE"},
    {"surface.selected", "#D6E9EB"},
    {"surface.hover", "#EAF2F3"},
    {"border.default", "#CFD6DB"},
    {"text.primary", "#202B33"},
    {"text.secondary", "#5E6C76"},
    {"accent.primary", "#007D88"},
    {"status.success", "#167A49"},
    {"status.warning", "#A86609"},
    {"status.error", "#B42318"},
    {"status.info", "#2563A5"},
    {"status.unknown", "#687781"},
    {"focus.outline", "#005D9B"},
};

/// 间距模数，单位是 Qt 逻辑像素。来自设计第 17.4 节的 4 / 8 / 12 / 16 / 24。
/// 这些值只用于边距和间距，不用于给控件写绝对坐标。
inline constexpr int kSpace4 = 4;
inline constexpr int kSpace8 = 8;
inline constexpr int kSpace12 = 12;
inline constexpr int kSpace16 = 16;
inline constexpr int kSpace24 = 24;

/// 正文点号。10 pt 在 96 DPI 下约等于设计要求的 13 px，并跟随 Qt 的系统缩放。
inline constexpr int kBodyPointSize = 10;

/// 设计第 17.9 节的一条界面状态。本枚举不表示设备已经读取、确认或验证。
enum class StatusKind {
    /// 还没有读数。灰色，不得显示成 Off 或关闭。
    Unknown,

    /// 读取或写入仍在进行。蓝色。
    ReadingOrPending,

    /// 已经有一次读数。中性色。不是写权限，也不是验证通过。
    Read,

    /// 用户目标值和已观察值不同，尚未写入。琥珀色。
    Dirty,

    /// 值未通过校验。红色。不产生写请求。
    Invalid,

    /// 设备已经确认命令。提示色。它不是读回一致。
    Acked,

    /// 读回值与目标一致。绿色。
    Verified,

    /// 读回值与目标不一致。红色。目标值仍保留。
    Mismatch,

    /// 连接断开后的旧读数。灰色。不能用于安全判断。
    Stale,

    /// 传输只完成了一部分。琥珀色。
    TransferPartial,

    /// 连接异常。琥珀色。不能显示成完全在线。
    DeviceDegraded,
};

/// 状态图标的形状。相同颜色的状态必须使用不同形状，避免只靠颜色区分。
/// 形状是本切片画出来的简单图形，不是外部图标资源。
enum class StatusIconShape {
    /// 问号。对应设计表里的 `?`。
    Question,

    /// 省略号。对应设计表里的 `…`。
    Ellipsis,

    /// 三条横线，表示已有读数。
    List,

    /// 实心三角，表示还有未写入的修改。
    Triangle,

    /// 叉，表示值无效。
    Cross,

    /// 单勾。只表示设备已确认，不表示读回验证。
    SingleCheck,

    /// 圆圈加勾，表示读回一致。
    CircledCheck,

    /// 不等号，表示读回值和目标值不同。
    NotEqual,

    /// 时钟，表示读数已过期。
    Clock,

    /// 左半填充的圆，表示只完成了一部分。
    HalfCircle,

    /// 断开的圆弧，表示连接不完整。
    BrokenCircle,
};

/// 一条状态在界面上的固定说法。文字、说明、颜色和图标一起使用。
struct StatusPresentation {
    /// 该说法对应的状态。
    StatusKind kind;

    /// 不依赖颜色也能区分状态的图标。
    StatusIconShape icon;

    /// 界面上显示的中文状态，不含颜色含义。
    std::string_view text;

    /// 给辅助技术的说明，写的是设计第 17.9 节的操作约束。
    std::string_view description;

    /// 使用的令牌颜色，必须等于 `kLightThemeTokens` 里的某一条 `hex`。
    std::string_view colorHex;
};

/// 设计第 17.9 节的全部状态说法。顺序与该表一致。这里没有实时设备值。
inline constexpr StatusPresentation kStatusPresentations[] = {
    {StatusKind::Unknown, StatusIconShape::Question, "未读取", "不将其解释为 Off", "#687781"},
    {StatusKind::ReadingOrPending, StatusIconShape::Ellipsis, "正在读取/写入", "禁止重复提交同一请求", "#2563A5"},
    {StatusKind::Read, StatusIconShape::List, "已读", "可修改 Set（有写权限时）", "#202B33"},
    {StatusKind::Dirty, StatusIconShape::Triangle, "待写入", "必须能查看差异", "#A86609"},
    {StatusKind::Invalid, StatusIconShape::Cross, "参数无效", "不生成 WriteRequest", "#B42318"},
    {StatusKind::Acked, StatusIconShape::SingleCheck, "设备已确认", "不声明已验证", "#2563A5"},
    {StatusKind::Verified, StatusIconShape::CircledCheck, "读回一致", "可清理 Dirty", "#167A49"},
    {StatusKind::Mismatch, StatusIconShape::NotEqual, "读回不一致", "保留目标值、显示实际值", "#B42318"},
    {StatusKind::Stale, StatusIconShape::Clock, "数据已过期", "不用于安全判断", "#687781"},
    {StatusKind::TransferPartial, StatusIconShape::HalfCircle, "部分完成", "显示已写区间和风险", "#A86609"},
    {StatusKind::DeviceDegraded, StatusIconShape::BrokenCircle, "连接异常", "允许诊断，禁止误导为完全在线", "#A86609"},
};

/// @brief 查找样式表里没有写到的第一条浅色令牌。
/// @param qss 整份浅色样式表文本，只在本次调用期间借用。空文本视为全部缺失。
/// @return 第一条 `hex` 没有出现在 `qss` 中的令牌。全部都能找到时返回空指针。
inline const ThemeToken* missingLightThemeToken(std::string_view qss) {
    for (const ThemeToken& token : kLightThemeTokens) {
        if (qss.find(token.hex) == std::string_view::npos) {
            return &token;
        }
    }
    return nullptr;
}

/// @brief 按颜色值查找浅色令牌。
/// @param hex `#RRGGBB`，只在本次调用期间借用。比较区分大小写。
/// @return 第一条颜色相同的令牌。没有相同颜色时返回空指针。
inline const ThemeToken* findLightThemeTokenByHex(std::string_view hex) {
    for (const ThemeToken& token : kLightThemeTokens) {
        if (token.hex == hex) {
            return &token;
        }
    }
    return nullptr;
}

/// @brief 按状态查找固定说法。
/// @param kind 设计第 17.9 节里的一种状态。
/// @return 对应说法。枚举和表不一致时返回空指针，调用方不得把它显示成 Off。
inline const StatusPresentation* findStatusPresentation(StatusKind kind) {
    const auto index = static_cast<std::size_t>(kind);
    if (index >= std::size(kStatusPresentations)) {
        return nullptr;
    }
    if (kStatusPresentations[index].kind != kind) {
        return nullptr;
    }
    return &kStatusPresentations[index];
}

} // namespace ernc::presentation
