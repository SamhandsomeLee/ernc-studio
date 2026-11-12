#include "analysis/curve_color_palette.hpp"

#include <QString>

namespace ernc::presentation {
namespace {

constexpr std::string_view kCurvePaletteHex[] = {
    "#007D88", "#B42318", "#167A49", "#2563A5", "#A86609",
    "#7A4EAB", "#C24D2C", "#3B6E8F", "#5E8F3B", "#8A5A44",
    "#0E7490", "#9F1239", "#047857", "#1D4ED8", "#B45309",
};

constexpr std::size_t kCurvePaletteSize = sizeof(kCurvePaletteHex) / sizeof(kCurvePaletteHex[0]);

}  // namespace

QColor curveColorForIndex(const std::size_t index) noexcept {
    const std::string_view hex = kCurvePaletteHex[index % kCurvePaletteSize];
    return QColor(QString::fromLatin1(hex.data(), static_cast<int>(hex.size())));
}

}  // namespace ernc::presentation
