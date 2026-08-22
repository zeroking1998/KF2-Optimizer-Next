#include "kf2/ui/theme.hpp"

#include <algorithm>
#include <cmath>

namespace kf2::ui {
namespace {

double channel(std::uint32_t color, unsigned shift) {
    const double encoded = static_cast<double>((color >> shift) & 0xFFU) / 255.0;
    return encoded <= 0.04045 ? encoded / 12.92
                              : std::pow((encoded + 0.055) / 1.055, 2.4);
}

double luminance(std::uint32_t color) {
    return 0.2126 * channel(color, 16) + 0.7152 * channel(color, 8) +
           0.0722 * channel(color, 0);
}

}  // namespace

Theme resolve_theme(const ThemeInput& input) noexcept {
    if (input.high_contrast) {
        return {input.system_background, input.system_background,
                input.system_background, input.system_text, input.system_text,
                input.system_accent, input.system_accent, input.system_text,
                input.system_accent, input.system_accent, input.system_accent,
                input.system_accent, false};
    }
    if (input.dark) {
        return {0xFF050A11, 0xFF09131F, 0xFF0D1A29, 0xFFF2F5FA,
                0xFFA8B3C2, 0xFFE3262E, 0xFFFF3740, 0xFF213348,
                0xFF25D978, 0xFF3DA5FF, 0xFFFFB02E, 0xFFFF4651,
                true};
    }
    return {0xFFF4F6FA, 0xFFFFFFFF, 0xFFF8FAFD, 0xFF151925,
            0xFF4D5668, 0xFFC91E27, 0xFFE3262E, 0xFFD5DAE4,
            0xFF08783F, 0xFF1168B5, 0xFF8A5700, 0xFFB42318,
            true};
}

double contrast_ratio(std::uint32_t foreground,
                      std::uint32_t background) noexcept {
    const double first = luminance(foreground);
    const double second = luminance(background);
    const double lighter = std::max(first, second);
    const double darker = std::min(first, second);
    return (lighter + 0.05) / (darker + 0.05);
}

}  // namespace kf2::ui
