#include "theme/theme.hpp"

#include "theme/theme_tokens.hpp"

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QString>

namespace ernc::presentation {
namespace {

/// 打进可执行文件的浅色样式表。路径相对 Qt 资源前缀 `/ernc`。
constexpr auto kLightThemeResource = ":/ernc/themes/ernc-light.qss";

/// @brief 把令牌颜色转成 Qt 颜色。
/// @param hex `#RRGGBB`，只在本次调用期间借用。必须来自 `kLightThemeTokens`。
/// @return 对应的不透明颜色。`hex` 无法解析时返回无效颜色。
QColor colorFromHex(std::string_view hex) {
    return QColor(QString::fromUtf8(hex.data(), static_cast<qsizetype>(hex.size())));
}

/// @brief 按令牌名取颜色。
/// @param name 设计令牌名，只在本次调用期间借用。
/// @param missingName 找不到时写入这个名字；找到时不修改。调用方拥有该字符串。
/// @return 找到且能解析时返回有效颜色，否则返回无效颜色。
QColor tokenColor(std::string_view name, QString& missingName) {
    for (const ThemeToken& token : kLightThemeTokens) {
        if (token.name != name) {
            continue;
        }
        const QColor color = colorFromHex(token.hex);
        if (!color.isValid()) {
            missingName = QString::fromUtf8(token.name.data(), static_cast<qsizetype>(token.name.size()));
        }
        return color;
    }
    missingName = QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    return {};
}

/// @brief 用浅色令牌覆盖应用调色板里和主题对应的角色。
/// @param application 要修改的应用，调用方拥有。
/// @param error 失败时写入缺少的令牌名。成功时不修改。
/// @return 调色板已替换时为 true。任一令牌缺失时不调用 `setPalette`。
bool applyLightPalette(QApplication& application, QString& error) {
    QString missingName;
    const QColor window = tokenColor("surface.app", missingName);
    const QColor windowText = tokenColor("text.primary", missingName);
    const QColor base = tokenColor("surface.panel", missingName);
    const QColor header = tokenColor("surface.header", missingName);
    const QColor selected = tokenColor("surface.selected", missingName);
    const QColor secondary = tokenColor("text.secondary", missingName);
    const QColor accent = tokenColor("accent.primary", missingName);
    if (!window.isValid() || !windowText.isValid() || !base.isValid() || !header.isValid()
        || !selected.isValid() || !secondary.isValid() || !accent.isValid()) {
        error = QStringLiteral("浅色主题令牌无法解析：") + missingName;
        return false;
    }

    QPalette palette = application.palette();
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, windowText);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, header);
    palette.setColor(QPalette::Text, windowText);
    palette.setColor(QPalette::Button, header);
    palette.setColor(QPalette::ButtonText, windowText);
    palette.setColor(QPalette::Highlight, selected);
    palette.setColor(QPalette::HighlightedText, windowText);
    palette.setColor(QPalette::Link, accent);
    palette.setColor(QPalette::PlaceholderText, secondary);
    application.setPalette(palette);
    return true;
}

} // namespace

ThemeLoadResult applyLightTheme(QApplication& application) {
    QFile file(QString::fromUtf8(kLightThemeResource));
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, QStringLiteral("浅色主题资源缺失：:/ernc/themes/ernc-light.qss")};
    }
    const QByteArray bytes = file.readAll();
    const std::string_view qss(bytes.constData(), static_cast<std::size_t>(bytes.size()));

    if (const ThemeToken* missing = missingLightThemeToken(qss)) {
        const QString name = QString::fromUtf8(missing->name.data(), static_cast<qsizetype>(missing->name.size()));
        const QString hex = QString::fromUtf8(missing->hex.data(), static_cast<qsizetype>(missing->hex.size()));
        return {false, QStringLiteral("浅色主题缺少令牌 %1（%2）").arg(name, hex)};
    }

    for (const StatusPresentation& status : kStatusPresentations) {
        if (findLightThemeTokenByHex(status.colorHex) == nullptr) {
            const QString text = QString::fromUtf8(status.text.data(), static_cast<qsizetype>(status.text.size()));
            return {false, QStringLiteral("状态“%1”的颜色不是已知令牌").arg(text)};
        }
    }

    QFont font;
    font.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Microsoft YaHei UI")});
    font.setPointSize(kBodyPointSize);

    QString paletteError;
    if (!applyLightPalette(application, paletteError)) {
        return {false, paletteError};
    }
    application.setFont(font);
    application.setStyleSheet(QString::fromUtf8(bytes));
    return {true, {}};
}

} // namespace ernc::presentation
