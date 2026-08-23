#pragma once

#include <filesystem>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "kf2/app/session.hpp"
#include "kf2/backup/restore_transaction.hpp"
#include "kf2/config/config_preview.hpp"
#include "kf2/config/settings.hpp"
#include "kf2/core/result.hpp"
#include "kf2/diagnostics/event_log.hpp"
#include "kf2/game/game_discovery.hpp"
#include "kf2/optimizer/optimizer_engine.hpp"
#include "kf2/platform/windows/single_instance.hpp"
#include "kf2/platform/windows/window.hpp"
#include "kf2/ui/ui_model.hpp"

namespace kf2::app {

enum class StartMode { normal, read_only };

[[nodiscard]] constexpr bool should_prepare_protected_gameplay_provider(
    StartMode mode) noexcept {
    return mode == StartMode::normal;
}

[[nodiscard]] constexpr bool should_prepare_adaptive_flex_runtime(
    StartMode mode, int configured_physx_level) noexcept {
    return mode == StartMode::normal && configured_physx_level > 0;
}

void preserve_user_flex_activation(
    std::vector<config::RequestedChange>& changes) noexcept;
void enforce_temporal_aa_disabled(
    std::vector<config::RequestedChange>& changes) noexcept;

struct StartOptions {
    std::filesystem::path state_root;
    std::filesystem::path executable_root;
    std::wstring instance_name;
    SessionIdentity identity;
    bool create_window{true};
    std::wstring window_title{L"KF2 Optimizer Next"};
    std::optional<game::GameDiscoveryInput> game_discovery;
    StartMode mode{StartMode::normal};
    std::wstring startup_warning;
};

class Application final {
public:
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) noexcept;
    Application& operator=(Application&&) noexcept;
    ~Application();

    [[nodiscard]] static Result<Application> start(const StartOptions& options);
    [[nodiscard]] Result<int> run(int show_command);
    [[nodiscard]] Result<bool> shutdown_cleanly();
    [[nodiscard]] const std::filesystem::path& state_root() const noexcept;
    [[nodiscard]] const ui::UiModel& ui_model() const noexcept;
    [[nodiscard]] HWND native_window_handle() const noexcept;
    [[nodiscard]] const std::optional<game::GameInstallation>&
        game_installation() const noexcept;
    [[nodiscard]] Result<config::ConfigPreview> prepare_config_changes(
        const std::vector<config::RequestedChange>& requests);
    [[nodiscard]] Result<config::ApplyResult> apply_prepared_config(
        const config::ApplyPreconditions& preconditions);
    [[nodiscard]] Result<backup::RestoreResult> restore_config(
        std::string_view id, const config::ApplyPreconditions& preconditions);
    [[nodiscard]] Result<bool> set_overlay_enabled(bool enabled);
    [[nodiscard]] bool overlay_enabled() const noexcept;
    void telemetry_tick_for_testing();

private:
    Application(std::filesystem::path state_root,
                platform::windows::SingleInstance instance,
                SessionGuard session,
                config::Settings settings,
                std::unique_ptr<diagnostics::EventLog> events,
                std::unique_ptr<struct UiRuntime> ui_runtime,
                bool com_initialized);

    std::filesystem::path state_root_;
    platform::windows::SingleInstance instance_;
    SessionGuard session_;
    config::Settings settings_;
    std::unique_ptr<diagnostics::EventLog> events_;
    std::unique_ptr<struct UiRuntime> ui_runtime_;
    bool com_initialized_{false};
    bool clean_shutdown_{false};
};

}  // namespace kf2::app
