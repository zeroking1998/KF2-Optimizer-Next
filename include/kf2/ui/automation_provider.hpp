#pragma once

#include <Windows.h>
#include <Unknwn.h>

#include <cstddef>
#include <cstdint>
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
    [[nodiscard]] bool update_layout(ShellLayoutResult layout) noexcept;
#if defined(KF2_AUTOMATION_PROVIDER_TESTING)
    void fail_next_child_allocation_for_testing() noexcept;
    void fail_runtime_id_write_for_testing(LONG position, HRESULT failure) noexcept;
    [[nodiscard]] HRESULT runtime_id_cleanup_result_for_testing() const noexcept;
    [[nodiscard]] IUnknown* retain_child_for_testing(
        std::size_t index) const noexcept;
    void disconnect_for_testing() noexcept;
    [[nodiscard]] std::uint32_t provider_options_for_testing(
        bool child) const noexcept;
#endif

private:
    struct Impl;
    explicit AutomationProvider(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
};

}  // namespace kf2::ui
