#include "application_runtime.hpp"

#include "kf2/update/update_state.hpp"

namespace kf2::app {

optimizer::AdaptivePolicy adaptive_policy_from(
    const config::Settings& settings) noexcept {
    optimizer::AdaptiveAggressiveness aggressiveness =
        optimizer::AdaptiveAggressiveness::balanced;
    if (settings.adaptive_aggressiveness == "conservative") {
        aggressiveness = optimizer::AdaptiveAggressiveness::conservative;
    } else if (settings.adaptive_aggressiveness == "aggressive") {
        aggressiveness = optimizer::AdaptiveAggressiveness::aggressive;
    }
    return {
        .target_fps = settings.target_fps,
        .aggressiveness = aggressiveness,
        .minimum_quality = settings.adaptive_minimum_quality,
        .maximum_quality = settings.adaptive_maximum_quality,
        .quality_change_budget = settings.adaptive_quality_change_budget,
        .performance_headroom =
            static_cast<double>(settings.adaptive_headroom_percent) / 100.0,
        .emergency_enabled = settings.adaptive_emergency_enabled,
        .quality_recovery_enabled =
            settings.adaptive_quality_recovery_enabled,
        .manual_locks_enabled = settings.adaptive_manual_locks_enabled,
        .shadow_mode = settings.adaptive_shadow_mode,
        .calibration_enabled = settings.adaptive_calibration_enabled,
        .adaptive_logging = settings.adaptive_logging,
    };
}

optimizer::Profile stored_adaptive_profile(
    const config::Settings& settings) noexcept {
    return optimizer::parse_adaptive_profile(settings.optimizer_profile)
        .value_or(optimizer::Profile::balanced);
}

std::wstring adaptive_profile_reason(
    const optimizer::AdaptiveDecision& decision) {
    const std::string_view reason = decision.reason;
    return std::wstring{reason.begin(), reason.end()};
}

Result<config::Settings> load_or_create_settings(
    const std::filesystem::path& path) {
    config::Settings settings;
    bool must_write = !std::filesystem::exists(path);
    if (!must_write) {
        std::string bytes;
        {
            std::ifstream input(path, std::ios::binary);
            if (!input) {
                return Result<config::Settings>::failure(
                    {ErrorCode::io_failure, L"Settings file cannot be opened", 0});
            }
            bytes.assign(std::istreambuf_iterator<char>{input},
                         std::istreambuf_iterator<char>{});
        }
        auto parsed = config::parse_settings(bytes);
        if (parsed.has_value()) {
            const bool canonicalization_needed =
                bytes.find("optimizer_mode=smart") != std::string::npos ||
                bytes.find("smart_mode=") != std::string::npos ||
                bytes.find("sound_enabled=") != std::string::npos ||
                bytes.find("optimizer_mode=manual") != std::string::npos ||
                bytes.find("adaptive_flex_enabled=") != std::string::npos ||
                bytes.find("adaptive_flex_auto=") != std::string::npos ||
                bytes.find("adaptive_flex_max_substeps=") != std::string::npos ||
                bytes.find("manual_flex_substeps=") != std::string::npos ||
                bytes.find("guide_completed=") != std::string::npos ||
                bytes.find("guide_step=") != std::string::npos ||
                bytes.find("manual_corpse_limit=") != std::string::npos ||
                bytes.find("manual_gore_effect_limit=") != std::string::npos ||
                bytes.find("adaptive_enabled=") != std::string::npos ||
                bytes.find("adaptive_online_allowed=") != std::string::npos ||
                parsed.value().target_fps_migrated ||
                parsed.value().adaptive_quality_range_migrated;
            if (canonicalization_needed) {
                const auto migrated =
                    platform::windows::atomic_replace_utf8(
                        path, config::serialize_settings(parsed.value()));
                if (!migrated.has_value()) {
                    return Result<config::Settings>::failure(
                        migrated.error());
                }
            }
            return parsed;
        }

        const auto quarantined =
            platform::windows::quarantine_regular_file(path);
        if (!quarantined.has_value()) {
            return Result<config::Settings>::failure(
                {ErrorCode::io_failure, L"Corrupt settings quarantine failed",
                 quarantined.error().native_code});
        }
        must_write = true;
    }

    if (must_write) {
        const auto written = platform::windows::atomic_replace_utf8(
            path, config::serialize_settings(settings));
        if (!written.has_value()) {
            return Result<config::Settings>::failure(written.error());
        }
    }
    return Result<config::Settings>::success(std::move(settings));
}

std::wstring format_gib(std::uint64_t bytes) {
    std::wostringstream text;
    text << std::fixed << std::setprecision(1)
         << static_cast<long double>(bytes) /
                (1024.0L * 1024.0L * 1024.0L)
         << L" GiB";
    return text.str();
}

Result<bool> open_local_directory(const std::filesystem::path& path) {
    std::error_code error;
    const auto resolved = std::filesystem::weakly_canonical(path, error);
    if (error || !std::filesystem::is_directory(resolved, error)) {
        return Result<bool>::failure(
            {ErrorCode::not_found, L"Requested local directory was not found",
             static_cast<std::uint32_t>(error.value())});
    }
    const auto result = reinterpret_cast<std::intptr_t>(ShellExecuteW(
        nullptr, L"open", resolved.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        return Result<bool>::failure(
            {ErrorCode::platform_failure, L"Windows could not open the local directory",
             static_cast<std::uint32_t>(result)});
    }
    return Result<bool>::success(true);
}

std::optional<std::filesystem::path> choose_directory(
    HWND owner, std::wstring_view title) {
    Microsoft::WRL::ComPtr<IFileDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return std::nullopt;
    }
    DWORD options = 0;
    if (FAILED(dialog->GetOptions(&options)) ||
        FAILED(dialog->SetOptions(options | FOS_PICKFOLDERS |
                                  FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST |
                                  FOS_DONTADDTORECENT))) {
        return std::nullopt;
    }
    const std::wstring owned_title{title};
    dialog->SetTitle(owned_title.c_str());
    if (dialog->Show(owner) != S_OK) return std::nullopt;
    Microsoft::WRL::ComPtr<IShellItem> selected;
    if (FAILED(dialog->GetResult(&selected))) return std::nullopt;
    PWSTR raw = nullptr;
    if (FAILED(selected->GetDisplayName(SIGDN_FILESYSPATH, &raw)) || !raw) {
        return std::nullopt;
    }
    std::filesystem::path path{raw};
    CoTaskMemFree(raw);
    return path;
}

std::optional<std::filesystem::path> choose_game_directory(HWND owner) {
    return choose_directory(owner, L"Select the Killing Floor 2 folder");
}

std::optional<std::string> path_utf8(const std::filesystem::path& path) {
    const auto value = path.wstring();
    if (value.empty()) return std::nullopt;
    const int size = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr);
    if (size <= 0) return std::nullopt;
    std::string result(static_cast<std::size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), size,
                            nullptr, nullptr) != size) {
        return std::nullopt;
    }
    return result;
}

Result<std::string> read_verified_local_file(const std::filesystem::path& path,
                                             std::uintmax_t maximum_bytes) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied, L"Local file identity is unsafe", GetLastError()});
    }
    HANDLE file = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure, L"Local file cannot be inspected", GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    const bool identity_ok = GetFileInformationByHandle(file, &information) &&
        information.nNumberOfLinks == 1;
    CloseHandle(file);
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (!identity_ok || error || size > maximum_bytes) {
        return Result<std::string>::failure(
            {ErrorCode::access_denied, L"Local file failed identity or size validation",
             static_cast<std::uint32_t>(error.value())});
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::string>::failure(
            {ErrorCode::io_failure, L"Local file cannot be opened", 0});
    }
    std::string bytes{std::istreambuf_iterator<char>{input},
                      std::istreambuf_iterator<char>{}};
    if (bytes.size() != size) {
        return Result<std::string>::failure(
            {ErrorCode::stale_data, L"Local file changed while it was being read", 0});
    }
    return Result<std::string>::success(std::move(bytes));
}

std::wstring query_hardware_summary() {
    std::wstring result = L"Hardware:";
    const auto inventory = telemetry::query_hardware_inventory();
    if (inventory.has_value()) {
        result += L" CPU " + std::to_wstring(inventory.value().logical_processors) +
            L" logical / " + std::to_wstring(inventory.value().processor_groups) +
            L" groups | RAM " + format_gib(inventory.value().available_memory_bytes) +
            L" free / " + format_gib(inventory.value().installed_memory_bytes);
    } else {
        result += L" CPU/RAM unavailable";
    }
    const auto adapters = telemetry::enumerate_gpu_adapters();
    if (adapters.has_value()) {
        const auto physical_adapters =
            telemetry::unique_physical_gpu_adapters(adapters.value());
        std::size_t index = 0;
        for (const auto& adapter : physical_adapters) {
            ++index;
            result += L" | GPU " + std::to_wstring(index) + L" " + adapter.name +
                L" / " + format_gib(adapter.dedicated_memory_bytes) + L" VRAM";
            if (adapter.umd_driver_version) {
                result += L" / UMD " + telemetry::format_gpu_driver_version(
                    *adapter.umd_driver_version);
            }
        }
        if (index == 0) result += L" | GPU unavailable";
    } else {
        result += L" | GPU unavailable";
    }
    return result;
}

UiRuntime::~UiRuntime() {
    static_cast<void>(restore_live_adaptive_quality(
        L"KF2 Optimizer closed"));
    static_cast<void>(restore_protected_session_config(
        L"KF2 Optimizer closed"));
    if (window) {
        KillTimer(static_cast<HWND>(window->native_handle_for_testing()), 1);
    }
    if (high_resolution_animation_timer) timeEndPeriod(1);
}

std::filesystem::path UiRuntime::adaptive_locks_path() const {
    return settings_path.parent_path() / L"adaptive-locks.ini";
}

void UiRuntime::rebuild_adaptive_lock_cache() {
    adaptive_lock_cache.clear();
    adaptive_lock_cache.reserve(adaptive_locks.size());
    for (const auto& [name, state] : adaptive_locks) {
        adaptive_lock_cache.push_back({name, state, std::nullopt});
    }
}

Result<bool> UiRuntime::load_adaptive_locks() {
    const auto path = adaptive_locks_path();
    if (!std::filesystem::exists(path)) {
        adaptive_locks.clear();
        rebuild_adaptive_lock_cache();
        adaptive_locks_valid = true;
        return Result<bool>::success(true);
    }
    const auto document = read_verified_local_file(path, 256 * 1024);
    if (!document.has_value()) {
        adaptive_locks_valid = false;
        return Result<bool>::failure(document.error());
    }
    const auto parsed = config::parse_adaptive_locks(document.value());
    if (!parsed.has_value()) {
        adaptive_locks_valid = false;
        return Result<bool>::failure(parsed.error());
    }
    adaptive_locks = parsed.value();
    rebuild_adaptive_lock_cache();
    adaptive_locks_valid = true;
    return Result<bool>::success(true);
}

Result<bool> UiRuntime::save_adaptive_locks() {
    const auto saved = platform::windows::atomic_replace_utf8(
        adaptive_locks_path(),
        config::serialize_adaptive_locks(adaptive_locks));
    adaptive_locks_valid = saved.has_value();
    if (saved.has_value()) rebuild_adaptive_lock_cache();
    return saved;
}

void UiRuntime::update_adaptive_policy_status(ui::UiStatus& status) const {
    status.adaptive_shadow_mode =
        optimizer_settings.adaptive_shadow_mode;
    status.adaptive_aggressiveness = std::wstring{
        optimizer_settings.adaptive_aggressiveness.begin(),
        optimizer_settings.adaptive_aggressiveness.end()};
    status.adaptive_minimum_quality =
        optimizer_settings.adaptive_minimum_quality;
    status.adaptive_maximum_quality =
        optimizer_settings.adaptive_maximum_quality;
    status.adaptive_quality_change_budget =
        optimizer_settings.adaptive_quality_change_budget;
    status.adaptive_headroom_percent =
        optimizer_settings.adaptive_headroom_percent;
    status.adaptive_emergency_enabled =
        optimizer_settings.adaptive_emergency_enabled;
    status.adaptive_quality_recovery_enabled =
        optimizer_settings.adaptive_quality_recovery_enabled;
    status.adaptive_manual_locks_enabled =
        optimizer_settings.adaptive_manual_locks_enabled;
    status.adaptive_calibration_enabled =
        optimizer_settings.adaptive_calibration_enabled;
    status.adaptive_logging = optimizer_settings.adaptive_logging;
}

UiRuntime::UiRuntime(const std::filesystem::path& state_root, bool recovery_required,
          const config::Settings& settings, diagnostics::EventLog& event_log,
          const std::optional<game::GameDiscoveryInput>& discovery,
          StartMode mode,
          std::filesystem::path executable_directory)
    : controller{model,
                  {.invalidate = [this] { invalidate(); },
                   .repaint = [this] {
                       if (window) window->invalidate();
                   },
                   .paint = [this](const ui::ShellLayoutResult& layout) {
                       paint(layout);
                   },
                   .get_object = [this](WPARAM wparam, LPARAM lparam) {
                       return automation ? automation->handle_get_object(wparam, lparam)
                                         : static_cast<LRESULT>(0);
                   },
                   .tick = [this] { runtime_tick(); },
                   .system_resume = [this] { system_resume(); },
                   .toggle_overlay = [this] {
                       static_cast<void>(toggle_overlay());
                   },
                   .activate_action = [this](std::string_view action) {
                       execute_action(action);
                   },
                   .set_slider_value = [this](std::string_view id, int value) {
                       set_slider_value(id, value);
                   },
                   .request_close = [this] {
                       if (!window) return;
                       const auto handle = static_cast<HWND>(
                           window->native_handle_for_testing());
                       if (handle) PostMessageW(handle, WM_CLOSE, 0, 0);
                   },
                   .theme_changed = [this] {
                       update_animation_cadence();
                   }}},
      events{&event_log},
      settings_path{state_root / L"settings.ini"},
      executable_root{std::move(executable_directory)},
      optimizer_settings{settings},
      backups{state_root}, discovery_input{discovery}, start_mode{mode},
      update_controller{current_build_identity().version},
      update_state_path{state_root / L"update-state.ini"} {
    // Build the immutable target registry during startup. No first-use
    // allocation is then possible in the measurement/control hot path.
    static_cast<void>(optimizer::adaptive_target_registry());
    if (settings.target_fps_migrated) {
        events->append({0, diagnostics::Severity::info,
            "TARGET_FPS_LEGACY_MIGRATED",
            L"Legacy TargetFPS above 240 was normalized and saved as 240",
            L"optimizer"});
    }
    if (settings.adaptive_quality_range_migrated) {
        events->append({0, diagnostics::Severity::info,
            "ADAPTIVE_QUALITY_RANGE_MIGRATED",
            L"The legacy 70% Adaptive quality floor was expanded to 10%",
            L"optimizer"});
    }
    overlay_enabled = settings.overlay_enabled;
    overlay_scale = static_cast<float>(settings.overlay_scale_percent) / 100.0F;
    if (settings.overlay_position == "top_left") {
        overlay_corner = overlay::OverlayCorner::top_left;
    } else if (settings.overlay_position == "bottom_left") {
        overlay_corner = overlay::OverlayCorner::bottom_left;
    } else if (settings.overlay_position == "bottom_right") {
        overlay_corner = overlay::OverlayCorner::bottom_right;
    }
    model.set_state_path(state_root.wstring());
    const auto identity = format_build_identity(current_build_identity());
    model.set_build_identity(std::wstring{identity.begin(), identity.end()});
    model.set_recovery_required(recovery_required);
    ui::UiStatus status;
    status.mode = mode == StartMode::read_only
        ? L"Read-only" : L"Adaptive / Automatic";
    status.target_fps = settings.target_fps;
    status.corpse_limit = settings.corpse_limit;
    update_adaptive_policy_status(status);
    status.restore_config_after_game = settings.restore_config_after_game;
    status.overlay_enabled = overlay_enabled;
    status.overlay_show_fps = settings.overlay_show_fps;
    status.overlay_show_frame_time = settings.overlay_show_frame_time;
    status.overlay_show_cpu = settings.overlay_show_cpu;
    status.overlay_show_gpu = settings.overlay_show_gpu;
    status.overlay_show_memory = settings.overlay_show_memory;
    status.overlay_scale_percent = settings.overlay_scale_percent;
    status.overlay_position = settings.overlay_position == "top_left" ? L"top left" :
        settings.overlay_position == "bottom_left" ? L"bottom left" :
        settings.overlay_position == "bottom_right" ? L"bottom right" : L"top right";
    status.hardware_summary = query_hardware_summary();
    status.profile = std::wstring{settings.optimizer_profile.begin(),
                                  settings.optimizer_profile.end()};
    status.quality = std::wstring{settings.quality_policy.begin(),
                                  settings.quality_policy.end()};
    const auto initial_profile = optimizer::bound_adaptive_profile(
        stored_adaptive_profile(settings),
        settings.adaptive_minimum_quality,
        settings.adaptive_maximum_quality);
    status.recommended_profile = initial_profile
        ? std::wstring{optimizer::adaptive_profile_label(*initial_profile)}
        : L"not available";
    status.recommendation_reason = initial_profile
        ? L"Automatic profile is ready; gameplay telemetry will refine later launches"
        : L"No verified named profile fits the selected quality limits";
    if (discovery) {
        auto found = game::discover_game_installation(*discovery);
        if (found.has_value()) {
            installation = std::move(found.value());
            status.game = L"Game detected: " + installation->install_root.wstring();
            status.game_detected = true;
            const auto game_running =
                game::find_running_game_process(installation->executable).has_value();
            const auto telemetry_recovered =
                game::recover_offline_telemetry_lab(
                    installation->config_root, state_root, game_running);
            if (!telemetry_recovered.has_value()) {
                event_log.append({0, diagnostics::Severity::error,
                    "OFFLINE_TELEMETRY_RECOVERY_BLOCKED",
                    telemetry_recovered.error().message, L"game"});
                recovery_required = true;
            } else if (telemetry_recovered.value().cleaned) {
                event_log.append({0, diagnostics::Severity::warning,
                    "OFFLINE_TELEMETRY_RECOVERED",
                    L"Interrupted offline telemetry package was hash-verified and removed",
                    L"game"});
            } else if (telemetry_recovered.value().active) {
                event_log.append({0, diagnostics::Severity::info,
                    "OFFLINE_TELEMETRY_RESUMED",
                    L"The running KF2 process retained its verified offline telemetry package",
                    L"game"});
            }
            const auto flex_directory = installation->install_root /
                L"Binaries" / L"Win64";
            const auto flex_state = state_root / L"flex-lab";
            const bool flex_transaction_exists =
                std::filesystem::exists(
                    flex_state / L"flex-lab-transaction.marker") ||
                std::filesystem::exists(
                    flex_directory / L"flexRelease_original.dll");
            const auto recovered = !game_running && flex_transaction_exists
                ? flex::restore_offline_lab(
                      flex_directory, flex_state, false)
                : flex::recover_offline_lab(
                      flex_directory, flex_state, game_running);
            if (!recovered.has_value()) {
                event_log.append({0, diagnostics::Severity::error,
                                  "FLEX_LAB_RECOVERY_BLOCKED",
                                  recovered.error().message, L"flex"});
                recovery_required = true;
            } else if (recovered.value()) {
                event_log.append({0, diagnostics::Severity::warning,
                                  "FLEX_LAB_RECOVERED",
                                  L"Interrupted offline FleX laboratory state was restored and verified",
                                  L"flex"});
            }
            if (game_running) {
                auto resumed = config::resume_session_config(
                    installation->config_root, state_root);
                if (!resumed.has_value()) {
                    event_log.append({0, diagnostics::Severity::error,
                        "SESSION_CONFIG_RESUME_FAILED",
                        resumed.error().message, L"config"});
                    recovery_required = true;
                } else if (resumed.value()) {
                    session_config_snapshot = std::move(*resumed.value());
                    event_log.append({0, diagnostics::Severity::info,
                        "SESSION_CONFIG_RESUMED",
                        L"Existing verified KF2 INI snapshot was resumed for the running game session",
                        L"config"});
                }
            } else {
                const auto ini_recovered = config::recover_session_config(
                    installation->config_root, state_root, false);
                if (!ini_recovered.has_value()) {
                    event_log.append({0, diagnostics::Severity::error,
                        "SESSION_CONFIG_RECOVERY_BLOCKED",
                        ini_recovered.error().message, L"config"});
                    recovery_required = true;
                } else if (ini_recovered.value() > 0) {
                    event_log.append({0, diagnostics::Severity::warning,
                        "SESSION_CONFIG_RECOVERED",
                        L"Deferred KF2 INI session snapshot was restored and verified; temporal anti-aliasing remains disabled",
                        L"config"});
                }
                if (ini_recovered.has_value() &&
                    telemetry_recovered.has_value() &&
                    !telemetry_recovered.value().active) {
                    const auto stale_configuration =
                        game::cleanup_stale_offline_gameplay_configuration(
                            installation->config_root, false);
                    if (!stale_configuration.has_value()) {
                        event_log.append({0, diagnostics::Severity::error,
                            "STALE_TELEMETRY_CONFIG_RECOVERY_BLOCKED",
                            stale_configuration.error().message, L"config"});
                        recovery_required = true;
                    } else if (stale_configuration.value()) {
                        event_log.append({0, diagnostics::Severity::warning,
                            "STALE_TELEMETRY_CONFIG_RECOVERED",
                            L"Stale optimizer-owned KF2 telemetry configuration was removed before the next protected snapshot",
                            L"config"});
                    }
                }
            }
        } else {
            event_log.append({0, diagnostics::Severity::warning,
                              "GAME_NOT_DETECTED", found.error().message,
                              L"discovery"});
        }
    }
    if (!recovery_required) {
        const auto existing = backups.list_backups();
        if (existing.has_value() && !existing.value().empty()) {
            last_backup_id = existing.value().front().id;
        }
    }
    const auto loaded_locks = load_adaptive_locks();
    if (!loaded_locks.has_value()) {
        event_log.append({0, diagnostics::Severity::error,
            "ADAPTIVE_LOCKS_INVALID",
            L"Adaptive is frozen because the per-setting lock file is invalid: " +
                loaded_locks.error().message,
            L"optimizer"});
        status.adaptive_state = L"frozen";
        status.adaptive_reason =
            L"Per-setting locks could not be verified; Adaptive fails closed";
    }
    model.set_recovery_required(recovery_required);
    model.set_status(std::move(status));
    reload_video_settings();
    reload_advanced_settings();
    const auto persisted_update = update::load_update_state(update_state_path);
    const auto cached_update = persisted_update.has_value()
        ? persisted_update.value() : update::PersistedUpdateState{};
    update_controller.restore_preferences(
        settings.automatic_update_checks,
        cached_update.last_check_unix_seconds,
        cached_update.last_result != update::PersistedCheckResult::unknown,
        cached_update.last_result == update::PersistedCheckResult::available
            ? cached_update.available_version : std::string{},
        cached_update.ignored_version);
    refresh_update_presentation();
    callbacks_ready = true;
    if (current_build_identity().channel == "release") {
        start_update_check(update::CheckTrigger::automatic);
    }
    invalidate();
}

Result<config::ConfigPreview> UiRuntime::prepare(
    const std::vector<config::RequestedChange>& requests,
    std::wstring context) {
    if (!installation) return Result<config::ConfigPreview>::failure(
        {ErrorCode::not_found, L"Game not detected", 0});
    std::map<std::filesystem::path, config::IniDocument> documents;
    for (const auto& request : requests) {
        const auto* definition = config::find_setting(request.id);
        if (!definition) return Result<config::ConfigPreview>::failure(
            {ErrorCode::invalid_argument, L"Requested setting is not verified", 0});
        if (documents.contains(definition->relative_path)) continue;
        const auto path = installation->config_root / definition->relative_path;
        std::ifstream input(path, std::ios::binary);
        if (!input) return Result<config::ConfigPreview>::failure(
            {ErrorCode::not_found, L"Required KF2 config file is missing", 0});
        const std::string bytes{std::istreambuf_iterator<char>{input},
                                std::istreambuf_iterator<char>{}};
        auto parsed = config::IniDocument::parse(bytes);
        if (!parsed.has_value()) return Result<config::ConfigPreview>::failure(parsed.error());
        documents.emplace(definition->relative_path, std::move(parsed.value()));
    }
    auto built = config::build_preview(*installation, requests, documents);
    if (!built.has_value()) return built;
    preview = built.value();
    preview_context = std::move(context);
    events->append({0, diagnostics::Severity::info,
                    "CONFIG_PREVIEW_READY",
                    preview_context + L": " +
                        std::to_wstring(preview->items.size()) +
                        L" settings across " +
                        std::to_wstring(preview->files.size()) + L" files",
                    L"config"});
    invalidate();
    return built;
}

}  // namespace kf2::app
