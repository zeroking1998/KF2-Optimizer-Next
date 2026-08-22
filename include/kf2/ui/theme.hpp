#pragma once

#include <cstdint>

namespace kf2::ui {

struct ThemeInput {
    bool high_contrast{false};
    bool dark{true};
    std::uint32_t system_background{0xFF000000};
    std::uint32_t system_text{0xFFFFFFFF};
    std::uint32_t system_accent{0xFFFFFF00};
};

struct Theme {
    std::uint32_t background;
    std::uint32_t surface;
    std::uint32_t surface_raised;
    std::uint32_t text;
    std::uint32_t muted_text;
    std::uint32_t accent;
    std::uint32_t accent_hover;
    std::uint32_t border;
    std::uint32_t success;
    std::uint32_t info;
    std::uint32_t warning;
    std::uint32_t error;
    bool animations_enabled;
};

[[nodiscard]] Theme resolve_theme(const ThemeInput& input) noexcept;
[[nodiscard]] double contrast_ratio(std::uint32_t foreground,
                                    std::uint32_t background) noexcept;

}  // namespace kf2::ui
