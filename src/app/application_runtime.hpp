#pragma once

#include "kf2/app/application.hpp"
#include "app/runtime/action_contract.hpp"
#include <mmsystem.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <ole2.h>
#include <fstream>
#include <iterator>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>
#include <memory>

#include "kf2/app/build_identity.hpp"
#include "kf2/backup/restore_transaction.hpp"
#include "kf2/config/ini_document.hpp"
#include "kf2/config/adaptive_locks.hpp"
#include "kf2/config/session_guard.hpp"
#include "kf2/game/offline_telemetry_lab.hpp"
#include "kf2/diagnostics/feature_inventory.hpp"
#include "kf2/diagnostics/crash_recorder.hpp"
#include "kf2/game/game_session.hpp"
#include "kf2/game/advanced_settings.hpp"
#include "kf2/game/video_settings.hpp"
#include "kf2/game/gameplay_log_lab.hpp"
#include "kf2/game/game_log_session.hpp"
#include "kf2/flex/flex_audit.hpp"
#include "kf2/flex/flex_adaptive_policy.hpp"
#include "kf2/flex/flex_lab.hpp"
#include "kf2/flex/flex_observation.hpp"
#include "kf2/overlay/overlay_policy.hpp"
#include "kf2/overlay/overlay_window.hpp"
#include "kf2/optimizer/adaptive_governor.hpp"
#include "kf2/optimizer/adaptive_actuation.hpp"
#include "kf2/optimizer/adaptive_profile.hpp"
#include "kf2/optimizer/optimizer_engine.hpp"
#include "kf2/platform/windows/presentmon_session.hpp"
#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/ui/automation_provider.hpp"
#include "kf2/ui/direct2d_renderer.hpp"
#include "kf2/ui/shell_controller.hpp"
#include "kf2/update/update_controller.hpp"
#include "kf2/telemetry/gpu_metrics.hpp"
#include "kf2/telemetry/system_metrics.hpp"

namespace kf2::telemetry_pipeline {
struct TelemetryFrame;
}

namespace kf2::app {

struct PackageRepairAsyncState;
struct UpdateCheckAsyncState;
struct UpdateInstallAsyncState;

optimizer::AdaptivePolicy adaptive_policy_from(
    const config::Settings& settings) noexcept;
optimizer::Profile stored_adaptive_profile(
    const config::Settings& settings) noexcept;
std::wstring adaptive_profile_reason(
    const optimizer::AdaptiveDecision& decision);
Result<config::Settings> load_or_create_settings(
    const std::filesystem::path& path);
std::wstring format_gib(std::uint64_t bytes);
Result<bool> open_local_directory(const std::filesystem::path& path);
std::optional<std::filesystem::path> choose_directory(
    HWND owner, std::wstring_view title);
std::optional<std::filesystem::path> choose_game_directory(HWND owner);
std::optional<std::string> path_utf8(const std::filesystem::path& path);
Result<std::string> read_verified_local_file(
    const std::filesystem::path& path, std::uintmax_t maximum_bytes);
std::wstring query_hardware_summary();
std::wstring optimizer_preview_context(
    const optimizer::OptimizerInput& input);

struct UiRuntime {
    ~UiRuntime();
    ui::UiModel model;
    bool callbacks_ready{false};
    ui::ShellController controller;
    std::optional<platform::windows::Window> window;
    std::optional<ui::Direct2DShellRenderer> renderer;
    std::optional<ui::AutomationProvider> automation;
    diagnostics::EventLog* events{};
    std::filesystem::path settings_path;
    std::filesystem::path executable_root;
    config::Settings optimizer_settings;
    backup::BackupStore backups;
    std::optional<game::GameDiscoveryInput> discovery_input;
    std::optional<game::GameInstallation> installation;
    std::optional<config::ConfigPreview> preview;
    std::wstring preview_context{L"Verified configuration changes"};
    std::string last_backup_id;
    bool overlay_enabled{false};
    std::uint64_t last_overlay_toggle_ns{0};
    overlay::OverlayCorner overlay_corner{overlay::OverlayCorner::top_right};
    float overlay_scale{1.0F};
    std::optional<overlay::OverlayWindow> overlay_window;
    std::optional<overlay::OverlayPresentation> overlay_presentation;
    std::uint64_t last_telemetry_tick_ns{0};
    std::optional<game::GameProcessIdentity> game_process;
    std::optional<config::SessionConfigSnapshot> session_config_snapshot;
    bool session_config_waiting_for_launch{false};
    std::uint64_t session_config_launch_deadline_ns{0};
    std::filesystem::path game_log_path;
    std::uintmax_t game_log_offset{0};
    std::uint32_t game_log_volume_serial{0};
    std::uint64_t game_log_file_index{0};
    bool game_log_bound_to_process{false};
    std::string game_log_marker_tail;
    game::GameLogSessionParser game_log_session_parser;
    bool overlay_scene_ready{false};
    HWND game_window{};
    std::unique_ptr<telemetry::PresentSource> present_source;
    std::unique_ptr<platform::windows::PresentMonSession> present_session;
    std::uint64_t present_session_started_ns{0};
    unsigned int present_session_restart_count{0};
    std::unique_ptr<telemetry::ProcessMetricSampler> process_metrics;
    std::optional<telemetry::PdhGpuSampler> gpu_metrics;
    std::optional<telemetry::NvidiaGpuSampler> nvidia_gpu_metrics;
    std::optional<std::uint64_t> adaptive_adapter_luid;
    optimizer::PerformanceEvidence optimizer_evidence;
    optimizer::AdaptiveGovernor adaptive_governor;
    optimizer::AdaptiveActuationTracker adaptive_actuation;
    std::uint64_t adaptive_settings_generation{1};
    optimizer::AdaptiveProfilePersistenceGate adaptive_profile_gate;
    optimizer::AdaptiveDecision adaptive_decision;
    config::AdaptiveLocks adaptive_locks;
    std::vector<optimizer::AdaptiveManualLock> adaptive_lock_cache;
    bool adaptive_locks_valid{true};
    std::string adaptive_map;
    std::uint64_t adaptive_map_generation{0};
    int adaptive_telemetry_sample{0};
    optimizer::AdaptiveControllerState last_adaptive_state{
        optimizer::AdaptiveControllerState::disabled};
    optimizer::AdaptiveDisposition last_adaptive_disposition{
        optimizer::AdaptiveDisposition::none};
    optimizer::AdaptiveBottleneck last_adaptive_bottleneck{
        optimizer::AdaptiveBottleneck::unknown};
    unsigned int adaptive_overhead_breaches{0};
    bool adaptive_overhead_frozen{false};
    bool adaptive_gameplay_active{false};
    telemetry::FrameMetrics last_frame_metrics;
    std::optional<game::GameLogSession> last_report_gameplay_session;
    std::optional<std::uint64_t> adapter_vram_budget;
    std::wstring telemetry_failure;
    std::uint64_t last_flex_observation_calls{0};
    bool flex_observation_announced{false};
    std::optional<flex::ObservationSnapshot> last_flex_observation;
    std::uint64_t last_flex_report_tick{0};
    flex::AdaptivePolicy flex_adaptive_policy;
    bool flex_adaptive_constrained{false};
    bool high_resolution_animation_timer{false};
    StartMode start_mode{StartMode::normal};
    std::shared_ptr<PackageRepairAsyncState> package_repair_state;
    update::UpdateController update_controller;
    std::filesystem::path update_state_path;
    std::shared_ptr<UpdateCheckAsyncState> update_check_state;
    std::shared_ptr<UpdateInstallAsyncState> update_install_state;
    std::optional<game::VideoSettings> video_saved;
    std::optional<game::VideoSettings> video_pending;
    std::optional<game::AdvancedGameSettings> advanced_saved;
    std::optional<game::AdvancedGameSettings> advanced_pending;

    void append_gameplay_report_fields(
        diagnostics::ProductReport& report) const noexcept;

    std::filesystem::path adaptive_locks_path() const;

    void rebuild_adaptive_lock_cache();

    Result<bool> load_adaptive_locks();

    Result<bool> save_adaptive_locks();

    void update_adaptive_policy_status(ui::UiStatus& status) const;

    void update_animation_cadence();

    UiRuntime(const std::filesystem::path& state_root, bool recovery_required,
              const config::Settings& settings, diagnostics::EventLog& event_log,
              const std::optional<game::GameDiscoveryInput>& discovery,
              StartMode mode,
              std::filesystem::path executable_directory);

    std::uint64_t monotonic_ns() const;

    bool save_flex_report(const flex::ObservationSnapshot& observed);

    void detach_telemetry();

    void update_overlay_scene_gate();

    void runtime_tick();

    void start_auto_package_repair();

    void poll_auto_package_repair();

    void start_update_check(update::CheckTrigger trigger);
    void poll_update_check();
    void toggle_automatic_update_checks();
    void start_update_install();
    void poll_update_install();
    void dismiss_update();
    void ignore_update();
    void refresh_update_presentation();
    void reload_video_settings();
    void refresh_video_presentation();
    void cycle_video_option(game::VideoOption option);
    void reset_video_settings();
    Result<config::ApplyResult> apply_video_settings();
    void reload_advanced_settings();
    void refresh_advanced_presentation();
    void cycle_advanced_option(game::AdvancedOption option);
    void stage_advanced_slider(game::AdvancedOption option, int value);
    void reset_advanced_settings();
    Result<config::ApplyResult> apply_advanced_settings();

    void system_resume();

    void observe_flex_process();

    Result<bool> ensure_automatic_flex_lab();

    bool restore_automatic_flex_lab(std::wstring_view reason);

    bool restore_protected_session_config(std::wstring_view reason);

    void try_attach_telemetry();

    void update_adaptive_controller(
        const telemetry_pipeline::TelemetryFrame& frame);

    void telemetry_tick();

    Result<bool> set_overlay(bool enabled);

    Result<bool> toggle_overlay();

    Result<config::ConfigPreview> prepare(
        const std::vector<config::RequestedChange>& requests,
        std::wstring context = L"Verified configuration changes");

    Result<OptimizerPreview> prepare_optimizer(
        const optimizer::OptimizerInput& input);

    Result<config::ApplyResult> apply_adaptive_launch_profile();

    void set_slider_value(std::string_view id, int requested_value);

    void execute_action(std::string_view action);

    Result<config::ApplyResult> apply(
        const config::ApplyPreconditions& preconditions);

    Result<backup::RestoreResult> restore(
        std::string_view id, const config::ApplyPreconditions& preconditions);

    Result<bool> create_window(const std::wstring& title);

    void invalidate();

    void paint(const ui::ShellLayoutResult& layout);

};

}  // namespace kf2::app
