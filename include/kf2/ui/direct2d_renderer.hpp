#pragma once

#include <Windows.h>

#include <filesystem>
#include <memory>

#include "kf2/core/result.hpp"
#include "kf2/ui/shell_layout.hpp"
#include "kf2/ui/theme.hpp"

namespace kf2::ui {

struct PixelSize {
    unsigned width{0};
    unsigned height{0};
};

class Direct2DShellRenderer final {
public:
    static Result<Direct2DShellRenderer> create(HWND window);

    Direct2DShellRenderer(Direct2DShellRenderer&&) noexcept;
    Direct2DShellRenderer& operator=(Direct2DShellRenderer&&) noexcept;
    Direct2DShellRenderer(const Direct2DShellRenderer&) = delete;
    Direct2DShellRenderer& operator=(const Direct2DShellRenderer&) = delete;
    ~Direct2DShellRenderer();

    [[nodiscard]] Result<bool> render(const ShellLayoutResult& layout,
                                      const Theme& theme);
    [[nodiscard]] Result<bool> resize(PixelSize size, float dpi);
    void discard_device_resources() noexcept;
    [[nodiscard]] Result<bool> capture_wic_png(
        const std::filesystem::path& path, const ShellLayoutResult& layout,
        const Theme& theme, PixelSize pixel_size, float dpi);

private:
    struct Impl;
    explicit Direct2DShellRenderer(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
};

}  // namespace kf2::ui
