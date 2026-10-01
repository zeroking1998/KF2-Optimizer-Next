#include "application_runtime.hpp"

#include "kf2/optimizer/adaptive_registry.hpp"

#include <chrono>

namespace kf2::app {

Application::Application(
    std::filesystem::path state_root,
    platform::windows::SingleInstance instance,
    SessionGuard session,
    config::Settings settings,
    std::unique_ptr<diagnostics::EventLog> events,
    std::unique_ptr<UiRuntime> ui_runtime,
    bool com_initialized)
    : state_root_{std::move(state_root)},
      instance_{std::move(instance)},
      session_{std::move(session)},
      settings_{std::move(settings)},
      events_{std::move(events)},
      ui_runtime_{std::move(ui_runtime)},
      com_initialized_{com_initialized} {}

Application::Application(Application&& other) noexcept
    : state_root_{std::move(other.state_root_)},
      instance_{std::move(other.instance_)},
      session_{std::move(other.session_)},
      settings_{std::move(other.settings_)},
      events_{std::move(other.events_)},
      ui_runtime_{std::move(other.ui_runtime_)},
      com_initialized_{std::exchange(other.com_initialized_, false)},
      clean_shutdown_{other.clean_shutdown_} {}

Application& Application::operator=(Application&& other) noexcept {
    if (this == &other) return *this;
    ui_runtime_.reset();
    if (com_initialized_) CoUninitialize();
    state_root_ = std::move(other.state_root_);
    instance_ = std::move(other.instance_);
    session_ = std::move(other.session_);
    settings_ = std::move(other.settings_);
    events_ = std::move(other.events_);
    ui_runtime_ = std::move(other.ui_runtime_);
    com_initialized_ = std::exchange(other.com_initialized_, false);
    clean_shutdown_ = other.clean_shutdown_;
    return *this;
}

Application::~Application() noexcept {
    ui_runtime_.reset();
    if (com_initialized_) CoUninitialize();
}

Result<Application> Application::start(const StartOptions& options) {
    if (options.state_root.empty() || options.executable_root.empty() ||
        options.instance_name.empty() || options.identity.pid == 0 ||
        options.identity.process_start_id == 0) {
        return Result<Application>::failure(
            {ErrorCode::invalid_argument, L"Application start options are invalid", 0});
    }

    try {
        // Function-local static initialization is all-or-nothing and retries
        // after an exception. Convert a construction failure before any
        // process or filesystem state is acquired.
        static_cast<void>(optimizer::adaptive_target_registry(
            options.adaptive_registry_initialization_probe));
    } catch (...) {
        return Result<Application>::failure({
            ErrorCode::internal_failure,
            L"Adaptive target registry could not be initialized", 0});
    }

    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool owns_com = SUCCEEDED(initialized);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE) {
        return Result<Application>::failure(
            {ErrorCode::platform_failure, L"COM initialization failed",
             static_cast<std::uint32_t>(initialized)});
    }
    struct ComRollback {
        bool active;
        ~ComRollback() { if (active) CoUninitialize(); }
    } rollback{owns_com};

    auto instance = platform::windows::SingleInstance::acquire(options.instance_name);
    if (!instance.has_value()) {
        return Result<Application>::failure(instance.error());
    }

    std::error_code directory_error;
    std::filesystem::create_directories(options.state_root / L"logs", directory_error);
    if (directory_error) {
        return Result<Application>::failure(
            {ErrorCode::io_failure, L"Portable state directories cannot be created",
             static_cast<std::uint32_t>(directory_error.value())});
    }

    auto settings = load_or_create_settings(options.state_root / L"settings.ini");
    if (!settings.has_value()) {
        return Result<Application>::failure(settings.error());
    }
    const auto event_log_rotation = diagnostics::prepare_event_log_rotation(
        options.state_root / L"logs");
    auto events = std::make_unique<diagnostics::EventLog>(
        512, event_log_rotation.persistence_path);
    events->append({0, diagnostics::Severity::info, "APP_START",
                    L"Application lifecycle initialized", L"app"});
    if (event_log_rotation.disposition ==
        diagnostics::PreviousEventLogDisposition::archived) {
        events->append({0, diagnostics::Severity::info,
                        "PREVIOUS_EVENT_LOG_ARCHIVED",
                        L"The bounded event log from the previous application session was preserved locally",
                        L"diagnostics"});
    } else if (event_log_rotation.disposition ==
               diagnostics::PreviousEventLogDisposition::retained) {
        events->append({0, diagnostics::Severity::warning,
                        "PREVIOUS_EVENT_LOG_RETAINED",
                        L"The previous event log archive was unavailable; its verified contents were retained under a bounded local fallback name",
                        L"diagnostics"});
    } else if (event_log_rotation.disposition ==
               diagnostics::PreviousEventLogDisposition::deferred) {
        events->append({0, diagnostics::Severity::warning,
                        "PREVIOUS_EVENT_LOG_PRESERVATION_DEFERRED",
                        L"The previous event log could not be safely rotated and was left unchanged for the next start",
                        L"diagnostics"});
    }
    const auto crash_records = diagnostics::retained_crash_record_count(
        options.state_root / L"logs" / L"crashes");
    if (crash_records > 0) {
        events->append({0, diagnostics::Severity::warning,
                        "PREVIOUS_CRASH_RECORD_AVAILABLE",
                        std::to_wstring(crash_records) +
                            L" bounded local optimizer crash record(s) are available in Data\\logs\\crashes",
                        L"diagnostics"});
    }
    // An interrupted app process is diagnostic evidence, not by itself a
    // recovery condition. The banner is reserved for a transaction, FleX lab
    // or INI snapshot that cannot be safely verified or restored below.
    bool recovery_required = false;
    backup::BackupStore recovery_store{options.state_root};
    std::optional<std::filesystem::path> verified_recovery_root;
    if (options.game_discovery) {
        const auto verified_installation =
            game::discover_game_installation(*options.game_discovery);
        if (verified_installation.has_value()) {
            verified_recovery_root = verified_installation.value().config_root;
        }
    }
    auto recovery = backup::recover_transactions(
        recovery_store, verified_recovery_root);
    if (!recovery.has_value()) {
        recovery_required = true;
        events->append({0, diagnostics::Severity::error, "CONFIG_RECOVERY_FAILED",
                        recovery.error().message, L"recovery"});
    } else if (recovery.value().transactions_recovered > 0) {
        events->append({0, diagnostics::Severity::warning, "CONFIG_RECOVERED",
                        L"Interrupted configuration transaction recovered",
                        L"recovery"});
    }

    auto runtime = std::make_unique<UiRuntime>(
        options.state_root, recovery_required,
        settings.value(), *events, options.game_discovery, options.mode,
        options.executable_root);
    if (!runtime->model.recovery_required() &&
        options.mode == StartMode::normal) {
        const bool game_running = runtime->installation &&
            game::find_running_game_process(
                runtime->installation->executable).has_value();
        if (runtime->installation && !game_running) {
            const auto capped = runtime->synchronize_frame_rate_cap();
            if (!capped.has_value()) {
                events->append({0, diagnostics::Severity::error,
                    "TARGET_FPS_PERSIST_FAILED", capped.error().message,
                    L"config"});
                runtime->model.set_notice({
                    ui::NoticeSeverity::error,
                    L"TARGET_FPS_PERSIST_FAILED",
                    L"KF2's native FPS cap could not be prepared: " +
                        capped.error().message,
                    L"The app remains available; KF2 settings were not partially changed."});
            } else if (capped.value().changed) {
                events->append({0, diagnostics::Severity::info,
                    "TARGET_FPS_PERSISTED",
                    L"KF2's vendor-independent startup cap was set to " +
                        std::to_wstring(capped.value().target_fps) + L" FPS",
                    L"config"});
            }
        } else if (runtime->installation) {
            events->append({0, diagnostics::Severity::info,
                "TARGET_FPS_PENDING_RESTART",
                L"KF2 is already running; the selected native FPS cap remains saved for the next start",
                L"config"});
        }
        const auto prepared =
            runtime->prepare_automatic_external_launch_profile();
        if (!prepared.has_value()) {
            events->append({0, diagnostics::Severity::error,
                "ADAPTIVE_EXTERNAL_LAUNCH_PREPARE_FAILED",
                prepared.error().message, L"optimizer"});
            runtime->model.set_notice({
                ui::NoticeSeverity::error,
                L"ADAPTIVE_EXTERNAL_LAUNCH_PREPARE_FAILED",
                L"Adaptive launch capabilities could not be prepared safely: " +
                    prepared.error().message,
                runtime->model.recovery_required() ||
                        prepared.error().code == ErrorCode::recovery_required
                    ? L"Do not start KF2 until Repair confirms that all protected files were restored."
                    : L"No game files were left partially changed."});
        }
    }
    if (!options.startup_warning.empty()) {
        events->append({0, diagnostics::Severity::error,
                        "PACKAGE_INTEGRITY_FAILED",
                        options.startup_warning, L"package"});
        runtime->model.set_notice({
            ui::NoticeSeverity::error, L"PACKAGE_INTEGRITY_FAILED",
            options.startup_warning,
            L"Use Repair to restore the affected component; unrelated controls remain available."});
    }
    if (options.create_window) {
        auto created = runtime->create_window(options.window_title);
        if (!created.has_value()) {
            return Result<Application>::failure(created.error());
        }
    }

    // Initialization failure is not an interrupted operational session.
    // Commit the unclean marker only after runtime/window initialization;
    // until this boundary, prior marker bytes (or absence) stay untouched.
    auto ready_state_root = options.state_root;
    auto session = SessionGuard::start(ready_state_root / L"session.marker",
                                       options.identity);
    if (!session.has_value()) {
        return Result<Application>::failure(session.error());
    }
    if (session.value().previous_session_unclean()) {
        events->append({0, diagnostics::Severity::info, "PREVIOUS_APP_INTERRUPTED",
                        L"The previous app process ended without its final marker",
                        L"session"});
    }

    rollback.active = false;
    return Result<Application>::success(Application{
        std::move(ready_state_root), std::move(instance.value()),
        std::move(session.value()),
        std::move(settings.value()), std::move(events), std::move(runtime), owns_com});
}

Result<int> Application::run(int show_command) {
    if (!ui_runtime_ || !ui_runtime_->window.has_value()) {
        return Result<int>::failure(
            {ErrorCode::invalid_argument, L"Application has no window", 0});
    }
    ui_runtime_->window->show(show_command);
    return ui_runtime_->window->run_message_loop();
}

Result<bool> Application::shutdown_cleanly() {
    if (clean_shutdown_) {
        return Result<bool>::success(true);
    }
    try {
        if (ui_runtime_) {
            const auto stopped = ui_runtime_->shutdown();
            if (!stopped.has_value()) {
                return Result<bool>::failure(stopped.error());
            }
        }
        if (events_ && !events_->flush(std::chrono::seconds{5})) {
            return Result<bool>::failure({
                ErrorCode::io_failure,
                L"Shutdown diagnostics could not be persisted; recovery remains armed",
                0});
        }
        auto result = session_.mark_clean();
        if (result.has_value()) {
            clean_shutdown_ = true;
            ui_runtime_.reset();
        }
        return result;
    } catch (...) {
        return Result<bool>::failure({
            ErrorCode::internal_failure,
            L"Application shutdown encountered an unexpected local error; recovery remains armed",
            0});
    }
}

const std::filesystem::path& Application::state_root() const noexcept {
    return state_root_;
}

const ui::UiModel& Application::ui_model() const noexcept {
    return ui_runtime_->model;
}

HWND Application::native_window_handle() const noexcept {
    return ui_runtime_ && ui_runtime_->window
               ? static_cast<HWND>(ui_runtime_->window->native_handle_for_testing())
               : nullptr;
}

const std::optional<game::GameInstallation>&
Application::game_installation() const noexcept {
    return ui_runtime_->installation;
}

Result<config::ConfigPreview> Application::prepare_config_changes(
    const std::vector<config::RequestedChange>& requests) {
    return ui_runtime_->prepare(requests);
}

Result<config::ApplyResult> Application::apply_prepared_config(
    const config::ApplyPreconditions& preconditions) {
    return ui_runtime_->apply(preconditions);
}

Result<backup::RestoreResult> Application::restore_config(
    std::string_view id, const config::ApplyPreconditions& preconditions) {
    return ui_runtime_->restore(id, preconditions);
}

Result<bool> Application::set_overlay_enabled(bool enabled) {
    return ui_runtime_->set_overlay(enabled);
}

bool Application::overlay_enabled() const noexcept {
    return ui_runtime_ && ui_runtime_->overlay_enabled;
}

void Application::telemetry_tick_for_testing() {
    if (ui_runtime_) ui_runtime_->telemetry_tick();
}

}  // namespace kf2::app
