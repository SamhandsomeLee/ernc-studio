#include "theme/theme_tokens.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace {

using ernc::presentation::StatusIconShape;
using ernc::presentation::StatusKind;
using ernc::presentation::StatusPresentation;
using ernc::presentation::ThemeToken;
using ernc::presentation::findStatusPresentation;
using ernc::presentation::kBodyPointSize;
using ernc::presentation::kLightThemeTokens;
using ernc::presentation::kStatusPresentations;
using ernc::presentation::missingLightThemeToken;

/// 设计 v1.2 第 17.4 节表格。期望值写在测试里，不从样式表反推。
struct ExpectedToken {
    /// 令牌名。
    const char* name;

    /// `#RRGGBB` 建议初值。
    const char* hex;
};

constexpr ExpectedToken kExpectedTokens[] = {
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

/// 设计 v1.2 第 17.9 节的文字、约束和颜色。图标形状是本切片的固定搭配。
struct ExpectedStatus {
    /// 状态。
    StatusKind kind;

    /// 界面文字。
    const char* text;

    /// 辅助说明里必须保留的约束原文。
    const char* description;

    /// 该状态使用的令牌颜色。
    const char* hex;

    /// 与颜色配套的图标。同色状态不能共用。
    StatusIconShape icon;
};

constexpr ExpectedStatus kExpectedStatuses[] = {
    {StatusKind::Unknown, "未读取", "不将其解释为 Off", "#687781", StatusIconShape::Question},
    {StatusKind::ReadingOrPending, "正在读取/写入", "禁止重复提交同一请求", "#2563A5", StatusIconShape::Ellipsis},
    {StatusKind::Read, "已读", "可修改 Set（有写权限时）", "#202B33", StatusIconShape::List},
    {StatusKind::Dirty, "待写入", "必须能查看差异", "#A86609", StatusIconShape::Triangle},
    {StatusKind::Invalid, "参数无效", "不生成 WriteRequest", "#B42318", StatusIconShape::Cross},
    {StatusKind::Acked, "设备已确认", "不声明已验证", "#2563A5", StatusIconShape::SingleCheck},
    {StatusKind::Verified, "读回一致", "可清理 Dirty", "#167A49", StatusIconShape::CircledCheck},
    {StatusKind::Mismatch, "读回不一致", "保留目标值、显示实际值", "#B42318", StatusIconShape::NotEqual},
    {StatusKind::Stale, "数据已过期", "不用于安全判断", "#687781", StatusIconShape::Clock},
    {StatusKind::TransferPartial, "部分完成", "显示已写区间和风险", "#A86609", StatusIconShape::HalfCircle},
    {StatusKind::DeviceDegraded, "连接异常", "允许诊断，禁止误导为完全在线", "#A86609", StatusIconShape::BrokenCircle},
};

/// @brief 报告当前用例失败。
/// @param name 用例名，只在本次调用期间借用。
/// @return 恒为 false。
bool fail(const char* name) {
    std::cerr << "failed: " << name << '\n';
    return false;
}

/// @brief 读入整份样式表。
/// @param path 样式表文件路径，只在本次调用期间借用。
/// @param qss 成功时写入文件全文。调用方拥有。失败时内容未定义。
/// @return 文件能打开时为 true。
bool readQss(const char* path, std::string& qss) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        std::cerr << "cannot open " << path << '\n';
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    qss = buffer.str();
    return true;
}

/// @brief 目录中的令牌名和颜色与设计表一致。
/// @param qssPath 未使用。保留是为了让每个用例都接收样式表路径。
/// @return 数量、顺序和内容都一致时为 true。
bool tokensMatchDesign(const char* qssPath) {
    (void)qssPath;
    if (std::size(kLightThemeTokens) != std::size(kExpectedTokens)) {
        return fail("tokens_match_design");
    }
    for (std::size_t i = 0; i < std::size(kExpectedTokens); ++i) {
        if (kLightThemeTokens[i].name != kExpectedTokens[i].name
            || kLightThemeTokens[i].hex != kExpectedTokens[i].hex) {
            return fail("tokens_match_design");
        }
    }
    if (kBodyPointSize != 10) {
        return fail("tokens_match_design");
    }
    return true;
}

/// @brief 样式表包含每一条设计颜色，以及正文字号和标题字号。
/// @param qssPath 浅色样式表路径，只在本次调用期间借用。
/// @return 文件可读且没有缺颜色时为 true。
bool qssContainsTokens(const char* qssPath) {
    std::string qss;
    if (!readQss(qssPath, qss)) {
        return fail("qss_contains_tokens");
    }
    if (missingLightThemeToken(qss) != nullptr) {
        return fail("qss_contains_tokens");
    }
    for (const ExpectedToken& token : kExpectedTokens) {
        if (qss.find(token.hex) == std::string::npos) {
            return fail("qss_contains_tokens");
        }
    }
    if (qss.find("10pt") == std::string::npos || qss.find("9pt") == std::string::npos
        || qss.find("12pt") == std::string::npos) {
        return fail("qss_contains_tokens");
    }
    if (qss.find("Siemens") != std::string::npos || qss.find("siemens") != std::string::npos) {
        return fail("qss_contains_tokens");
    }
    return true;
}

/// @brief 缺一条颜色时指出该令牌，空样式表指出第一条。
/// @param qssPath 未使用。本用例不读文件。
/// @return 报告的令牌与被拿掉的颜色一致时为 true。
bool missingTokenIsReported(const char* qssPath) {
    (void)qssPath;
    const ThemeToken* first = missingLightThemeToken({});
    if (first == nullptr || first->hex != kExpectedTokens[0].hex) {
        return fail("missing_token_is_reported");
    }

    std::string qss;
    for (std::size_t i = 0; i < std::size(kExpectedTokens); ++i) {
        if (i == 5) {
            continue;
        }
        qss += kExpectedTokens[i].hex;
        qss += '\n';
    }
    const ThemeToken* missing = missingLightThemeToken(qss);
    if (missing == nullptr || missing->name != kExpectedTokens[5].name
        || missing->hex != kExpectedTokens[5].hex) {
        return fail("missing_token_is_reported");
    }
    return true;
}

/// @brief 状态文字、约束、颜色和图标与设计表一致。
/// @param qssPath 未使用。本用例不读文件。
/// @return 十一条说法都能按状态找回时为 true。
bool statusMatchDesign(const char* qssPath) {
    (void)qssPath;
    if (std::size(kStatusPresentations) != std::size(kExpectedStatuses)) {
        return fail("status_match_design");
    }
    for (const ExpectedStatus& expected : kExpectedStatuses) {
        const StatusPresentation* actual = findStatusPresentation(expected.kind);
        if (actual == nullptr || actual->text != expected.text || actual->description != expected.description
            || actual->colorHex != expected.hex || actual->icon != expected.icon) {
            return fail("status_match_design");
        }
        if (actual->text == "Off" || actual->text == "关闭") {
            return fail("status_match_design");
        }
    }
    const StatusPresentation* acked = findStatusPresentation(StatusKind::Acked);
    const StatusPresentation* verified = findStatusPresentation(StatusKind::Verified);
    if (acked == nullptr || verified == nullptr || acked->colorHex == verified->colorHex) {
        return fail("status_match_design");
    }
    return true;
}

/// @brief 共用一种颜色的状态必须用不同文字和图标区分。
/// @param qssPath 未使用。本用例不读文件。
/// @return 任意两条同色状态的文字和图标都不同时为 true。
bool sharedColorDiffers(const char* qssPath) {
    (void)qssPath;
    for (std::size_t i = 0; i < std::size(kStatusPresentations); ++i) {
        for (std::size_t j = i + 1; j < std::size(kStatusPresentations); ++j) {
            const StatusPresentation& left = kStatusPresentations[i];
            const StatusPresentation& right = kStatusPresentations[j];
            if (left.colorHex == right.colorHex
                && (left.icon == right.icon || left.text == right.text)) {
                return fail("shared_color_differs_by_icon_and_text");
            }
            if (left.text == right.text) {
                return fail("shared_color_differs_by_icon_and_text");
            }
        }
    }
    return true;
}

/// @brief 按用例名运行一个检查。
/// @param name 用例名，只在本次调用期间借用。
/// @param qssPath 样式表路径，只在本次调用期间借用。
/// @param recognized 识别到用例名时设为 true。调用方拥有。
/// @return 用例通过时为 true。未识别的名字也返回 false，并由 `recognized` 区分。
bool runCase(const char* name, const char* qssPath, bool& recognized) {
    recognized = true;
    const std::string_view caseName(name);
    if (caseName == "tokens_match_design") {
        return tokensMatchDesign(qssPath);
    }
    if (caseName == "qss_contains_tokens") {
        return qssContainsTokens(qssPath);
    }
    if (caseName == "missing_token_is_reported") {
        return missingTokenIsReported(qssPath);
    }
    if (caseName == "status_match_design") {
        return statusMatchDesign(qssPath);
    }
    if (caseName == "shared_color_differs_by_icon_and_text") {
        return sharedColorDiffers(qssPath);
    }
    recognized = false;
    return false;
}

} // namespace

/// @brief 运行一个主题令牌用例。
/// @param argc 参数个数。必须是 3。
/// @param argv `argv[1]` 是用例名，`argv[2]` 是样式表路径。只在本次调用期间借用。
/// @return 通过返回 0，断言失败返回 1，用法错误返回 2。
int main(int argc, char** argv) {
    if (argc != 3 || argv[1] == nullptr || argv[2] == nullptr) {
        std::cerr << "usage: ernc_theme_tokens_tests <case> <qss-path>\n";
        return 2;
    }
    bool recognized = false;
    if (!runCase(argv[1], argv[2], recognized)) {
        if (!recognized) {
            std::cerr << "unknown case: " << argv[1] << '\n';
            return 2;
        }
        return 1;
    }
    return 0;
}
