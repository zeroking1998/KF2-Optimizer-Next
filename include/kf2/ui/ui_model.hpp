#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace kf2::ui {

enum class Destination : std::size_t {
    dashboard,
    game,
    optimizer,
    overlay,
    diagnostics,
    settings,
};

inline constexpr std::array<Destination, 6> kDestinations{
    Destination::dashboard, Destination::game, Destination::settings,
    Destination::overlay, Destination::optimizer, Destination::diagnostics,
};

enum class NavigationCommand { next, previous, home, end };
enum class NoticeSeverity { info, warning, error };
enum class ConfigWorkflowState {
    unavailable, detected, preview_ready, apply_blocked, applied,
    restore_available, recovery_required
};

struct Notice {
    NoticeSeverity severity{NoticeSeverity::info};
    std::wstring code;
    std::wstring message;
    std::wstring recovery_action;
};

struct UiStatus {
    std::wstring mode{L"Normal"};
    std::wstring game{L"Game not detected"};
    std::wstring game_session;
    std::wstring telemetry{L"Telemetry unavailable"};
    std::wstring performance_analysis{L"Performance analysis unavailable"};
    std::wstring recommended_profile{L"waiting"};
    std::wstring recommendation_reason{L"Fresh stable telemetry is required"};
    bool recommendation_ready{false};
    std::wstring hardware_summary{L"Hardware not refreshed"};
    std::wstring flex_telemetry{L"FleX telemetry not observed"};
    bool game_detected{false};
    std::optional<double> live_fps;
    std::optional<double> live_frame_time_ms;
    std::optional<double> live_cpu_percent;
    std::optional<double> live_gpu_percent;
    std::optional<int> live_active_corpses;
    std::optional<int> live_sleeping_corpses;
    bool animations_enabled{true};
    bool restore_config_after_game{true};
    bool offline_gameplay_telemetry{false};
    bool overlay_enabled{false};
    bool overlay_show_fps{true};
    bool overlay_show_frame_time{true};
    bool overlay_show_cpu{true};
    bool overlay_show_gpu{true};
    bool overlay_show_memory{false};
    std::wstring overlay_position{L"top right"};
    int overlay_scale_percent{100};
    int target_fps{60};
    int corpse_limit{20};
    std::optional<int> adaptive_runtime_corpse_limit;
    std::wstring adaptive_corpse_capability{L"UNAVAILABLE"};
    std::wstring adaptive_corpse_action_status{L"NONE"};
    std::optional<int> adaptive_flex_requested_substeps;
    std::optional<int> adaptive_flex_effective_substeps;
    std::wstring adaptive_flex_action_status{L"NONE"};
    std::wstring adaptive_flex_capability{L"UNAVAILABLE"};
    std::wstring adaptive_particle_capability{L"UNAVAILABLE"};
    std::wstring adaptive_state{L"observing"};
    std::wstring adaptive_bottleneck{L"unknown"};
    std::wstring adaptive_cpu_parallelism{L"not available"};
    std::wstring adaptive_action{L"none"};
    std::wstring adaptive_reason{L"Fresh validated telemetry is required"};
    int adaptive_confidence_percent{0};
    int adaptive_drop_risk_percent{0};
    int adaptive_quality_score{100};
    int adaptive_headroom_available_percent{0};
    std::wstring adaptive_data_quality{L"NOT_AVAILABLE"};
    std::wstring adaptive_prediction{L"not available"};
    std::wstring adaptive_session{L"SESSION_UNKNOWN"};
    std::wstring adaptive_source{L"not selected"};
    std::wstring adaptive_safety{L"LAB / SHADOW_ONLY"};
    std::wstring adaptive_evidence{L"NOT_AVAILABLE"};
    std::uint64_t adaptive_restore_generation{0};
    bool adaptive_shadow_mode{true};
    std::wstring adaptive_aggressiveness{L"balanced"};
    int adaptive_minimum_quality{70};
    int adaptive_maximum_quality{100};
    int adaptive_quality_change_budget{2};
    int adaptive_headroom_percent{8};
    bool adaptive_emergency_enabled{true};
    bool adaptive_quality_recovery_enabled{true};
    bool adaptive_manual_locks_enabled{true};
    bool adaptive_calibration_enabled{true};
    bool adaptive_logging{true};
    bool advanced_settings_visible{false};
    std::wstring profile{L"balanced"};
    std::wstring quality{L"exact"};
    std::wstring diagnostics_summary{L"No local diagnostic events"};
    ConfigWorkflowState config{ConfigWorkflowState::unavailable};
};

struct UiAction {
    bool changed{false};
};

[[nodiscard]] std::wstring_view destination_label(Destination destination);

class UiModel final {
public:
    [[nodiscard]] Destination selected() const noexcept;
    [[nodiscard]] Destination focused_destination() const noexcept;
    [[nodiscard]] const std::optional<std::string>& focused_action() const noexcept;
    [[nodiscard]] UiAction navigate(NavigationCommand command) noexcept;
    [[nodiscard]] UiAction focus_destination(Destination destination) noexcept;
    [[nodiscard]] UiAction focus_action(std::optional<std::string> action) noexcept;
    [[nodiscard]] UiAction activate_focused() noexcept;

    void set_scroll_extent(float extent) noexcept;
    [[nodiscard]] UiAction set_scroll(float offset) noexcept;
    [[nodiscard]] float scroll_offset() const noexcept;
    [[nodiscard]] float scroll_extent() const noexcept;

    void set_state_path(std::wstring path);
    void set_build_identity(std::wstring identity);
    void set_recovery_required(bool required) noexcept;
    void set_status(UiStatus status);
    void set_preview_summary(std::wstring summary);
    void clear_preview_summary() noexcept;
    void set_notice(Notice notice);
    void clear_notice() noexcept;

    [[nodiscard]] const std::wstring& state_path() const noexcept;
    [[nodiscard]] const std::wstring& build_identity() const noexcept;
    [[nodiscard]] bool recovery_required() const noexcept;
    [[nodiscard]] const UiStatus& status() const noexcept;
    [[nodiscard]] const std::optional<Notice>& notice() const noexcept;
    [[nodiscard]] std::wstring page_heading() const;
    [[nodiscard]] std::wstring page_body() const;

private:
    Destination selected_{Destination::dashboard};
    Destination focused_{Destination::dashboard};
    std::optional<std::string> focused_action_;
    float scroll_offset_{0.0F};
    float scroll_extent_{0.0F};
    std::wstring state_path_;
    std::wstring build_identity_;
    bool recovery_required_{false};
    UiStatus status_;
    std::optional<Notice> notice_;
    std::wstring preview_summary_;
};

}  // namespace kf2::ui
