#pragma once

#include <Windows.h>

#include <functional>
#include <memory>
#include <string_view>

#include "kf2/core/result.hpp"
#include "kf2/ui/shell_layout.hpp"
#include "kf2/ui/ui_model.hpp"

namespace kf2::ui {

class AutomationProvider final {
public:
    static Result<AutomationProvider> create(HWND window, UiModel& model,
                                             ShellLayoutResult layout,
                                             std::function<void(std::string_view)> activate_action = {},
                                             std::function<void()> invalidate = {},
                                             std::function<void(std::string_view, int)>
                                                 set_slider_value = {});
    AutomationProvider(AutomationProvider&&) noexcept;
    AutomationProvider& operator=(AutomationProvider&&) noexcept;
    AutomationProvider(const AutomationProvider&) = delete;
    AutomationProvider& operator=(const AutomationProvider&) = delete;
    ~AutomationProvider();

    [[nodiscard]] LRESULT handle_get_object(WPARAM wparam, LPARAM lparam) noexcept;
    void update_layout(ShellLayoutResult layout);

private:
    struct Impl;
    explicit AutomationProvider(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
};

}  // namespace kf2::ui
