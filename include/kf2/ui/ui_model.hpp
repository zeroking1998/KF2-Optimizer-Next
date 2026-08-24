#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "kf2/ui/numeric_presentation.hpp"

namespace kf2::ui {

enum class Destination : std::size_t {
    dashboard,
    graphics,
    overlay,
    advanced,
    diagnostics,
};

inline constexpr std::array<Destination, 5> kDestinations{
    Destination::dashboard, Destination::graphics,
    Destination::overlay, Destination::advanced, Destination::diagnostics,
};

enum class NavigationCommand { next, previous, home, end };
enum class NoticeSeverity { info, warning, error };
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
    std::wstring hardware_summary{L"Hardware not refreshed"};
    std::wstring flex_telemetry{L"FleX telemetry not observed"};
    bool game_detected{false};
    std::optional<double> live_fps;
    std::optional<double> live_frame_time_ms;
    std::optional<double> live_cpu_percent;
    std::optional<double> live_gpu_percent;
    std::optional<int> live_active_corpses;
    std::optional<int> live_sleeping_corpses;
    bool restore_config_after_game{true};
    bool overlay_enabled{true};
    bool overlay_show_fps{true};
    bool overlay_show_frame_time{true};
    bool overlay_show_cpu{true};
    bool overlay_show_gpu{true};
    bool overlay_show_memory{true};
    std::wstring overlay_position{L"top right"};
    int overlay_scale_percent{100};
    int target_fps{60};
    std::optional<int> active_target_fps;
    int corpse_limit{20};
    std::optional<int> active_corpse_limit;
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
    bool adaptive_shadow_mode{false};
    std::wstring adaptive_aggressiveness{L"balanced"};
    int adaptive_minimum_quality{10};
    int adaptive_maximum_quality{100};
    int adaptive_quality_change_budget{2};
    int adaptive_headroom_percent{8};
    bool adaptive_emergency_enabled{true};
    bool adaptive_quality_recovery_enabled{true};
    bool adaptive_manual_locks_enabled{true};
    bool adaptive_calibration_enabled{true};
    bool adaptive_logging{true};
    std::wstring update_installed_version{L"unknown"};
    std::wstring update_available_version{L"None"};
    std::wstring update_last_check{L"Never"};
    std::wstring update_status{L"Not checked yet"};
    std::wstring update_published_at;
    std::wstring update_download_size;
    std::wstring update_changelog;
    bool automatic_update_checks{true};
    bool update_checking{false};
    bool update_available{false};
    bool update_newer_version_known{false};
    bool update_prompt_visible{false};
    bool update_check_completed{false};
    bool update_installable{false};
    bool update_installing{false};
    bool graphics_available{false};
    bool graphics_dirty{false};
    bool graphics_game_running{false};
    std::array<std::wstring, 21> graphics_values{};
    std::wstring graphics_aspect_ratio{L"Unknown"};
    int graphics_film_grain_percent{50};
    bool advanced_available{false};
    bool advanced_dirty{false};
    bool advanced_game_running{false};
    std::array<std::wstring, 15> advanced_values{};
    int advanced_screen_percentage{100};
    int advanced_particle_percentage{100};
    int advanced_decal_lifetime{30};
    std::wstring profile{L"balanced"};
    std::wstring quality{L"exact"};
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
    void set_notice(Notice notice);
    void clear_notice() noexcept;

    [[nodiscard]] const std::wstring& state_path() const noexcept;
    [[nodiscard]] const std::wstring& build_identity() const noexcept;
    [[nodiscard]] bool recovery_required() const noexcept;
    [[nodiscard]] const UiStatus& status() const noexcept;
    [[nodiscard]] int presented_target_fps() const noexcept;
    [[nodiscard]] int presented_corpse_limit() const noexcept;
    void preview_target_fps(int value) noexcept;
    void preview_corpse_limit(int value) noexcept;
    void commit_target_fps_presentation(int value) noexcept;
    void commit_corpse_limit_presentation(int value) noexcept;
    [[nodiscard]] std::optional<double> presented_live_fps() const noexcept;
    [[nodiscard]] std::optional<double> presented_live_frame_time_ms() const noexcept;
    [[nodiscard]] std::optional<double> presented_live_cpu_percent() const noexcept;
    [[nodiscard]] std::optional<double> presented_live_gpu_percent() const noexcept;
    [[nodiscard]] std::optional<int> presented_live_active_corpses() const noexcept;
    [[nodiscard]] std::optional<int> presented_live_sleeping_corpses() const noexcept;
    [[nodiscard]] bool advance_numeric_presentation(bool animate) noexcept;
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
    NumericPresentation numeric_presentation_;
    std::optional<Notice> notice_;
};

}  // namespace kf2::ui
