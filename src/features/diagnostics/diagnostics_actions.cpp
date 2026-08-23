#include "features/diagnostics/diagnostics_actions.hpp"

#include "app/application_runtime.hpp"
#include "kf2/security/package_integrity.hpp"

namespace kf2::features::diagnostics {
namespace {

namespace product_diagnostics = ::kf2::diagnostics;

void show_notice(app::UiRuntime& runtime, ui::NoticeSeverity severity,
                 std::wstring code, std::wstring message) {
    runtime.model.set_notice(
        {severity, std::move(code), std::move(message), L""});
    runtime.invalidate();
}

product_diagnostics::ProductReport make_product_report(
    app::UiRuntime& runtime) {
    const auto& status = runtime.model.status();
    product_diagnostics::ProductReport report{
        .build_identity = runtime.model.build_identity(),
        .mode = status.mode,
        .game = status.game,
        .game_session = status.game_session,
        .telemetry = status.telemetry,
        .performance_analysis = status.performance_analysis,
        .hardware = status.hardware_summary,
        .flex = status.flex_telemetry,
        .optimizer_profile = status.profile,
        .quality_policy = status.quality,
        .overlay_position = status.overlay_position,
        .target_fps = status.target_fps,
        .overlay_scale_percent = status.overlay_scale_percent,
        .overlay_enabled = status.overlay_enabled,
        .restore_config_after_game = status.restore_config_after_game,
        .game_pid = runtime.game_process
            ? std::optional<std::uint32_t>{runtime.game_process->pid}
            : std::nullopt,
        .game_process_start_id = runtime.game_process
            ? std::optional<std::uint64_t>{
                  runtime.game_process->process_start_id}
            : std::nullopt,
        .event_log_stats = runtime.events->stats(),
        .game_log_stats = runtime.game_log_session_parser.stats(),
        .retained_crash_records =
            product_diagnostics::retained_crash_record_count(
                runtime.settings_path.parent_path() /
                L"logs" / L"crashes"),
        .events = runtime.events->snapshot(),
    };
    runtime.append_gameplay_report_fields(report);
    return report;
}

}  // namespace

app::runtime::DispatchResult open_log(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto path = runtime.events->persistence_path();
    if (path.empty() || !std::filesystem::exists(path)) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"LOG_UNAVAILABLE",
                    L"The local session log is not available.");
        return app::runtime::DispatchResult::handled;
    }
    const auto opened = reinterpret_cast<std::intptr_t>(ShellExecuteW(
        nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    show_notice(runtime,
                opened > 32 ? ui::NoticeSeverity::info
                            : ui::NoticeSeverity::warning,
                opened > 32 ? L"LOG_OPENED" : L"LOG_OPEN_FAILED",
                opened > 32 ? L"The local JSON session log was opened."
                            : L"Windows could not open the local session log.");
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult export_support(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto inventory =
        product_diagnostics::serialize_feature_inventory_json(
            app::format_build_identity(app::current_build_identity()));
    const auto document = product_diagnostics::serialize_support_bundle_json(
        make_product_report(runtime), inventory);
    const auto exported = platform::windows::atomic_replace_utf8(
        runtime.settings_path.parent_path() / L"private-support-bundle.json",
        document);
    show_notice(runtime,
                exported.has_value() ? ui::NoticeSeverity::info
                                     : ui::NoticeSeverity::error,
                exported.has_value() ? L"SUPPORT_BUNDLE_EXPORTED"
                                     : L"EXPORT_FAILED",
                exported.has_value()
                    ? L"A privacy-safe local support bundle was exported to Data. Nothing was uploaded."
                    : exported.error().message);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult open_data(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto opened =
        app::open_local_directory(runtime.settings_path.parent_path());
    show_notice(runtime,
                opened.has_value() ? ui::NoticeSeverity::info
                                   : ui::NoticeSeverity::warning,
                opened.has_value() ? L"DATA_FOLDER_OPENED"
                                   : L"DATA_FOLDER_UNAVAILABLE",
                opened.has_value() ? L"The portable Data folder was opened."
                                   : opened.error().message);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult repair_package(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto selected = app::choose_directory(
        runtime.window
            ? static_cast<HWND>(runtime.window->native_handle_for_testing())
            : nullptr,
        L"Select a complete KF2OptimizerNext package folder");
    if (!selected) {
        show_notice(runtime, ui::NoticeSeverity::info,
                    L"PACKAGE_REPAIR_CANCELLED",
                    L"Required-file import was cancelled. No files were changed.");
        return app::runtime::DispatchResult::handled;
    }

    std::filesystem::path source = *selected;
    std::error_code error;
    if (!std::filesystem::is_regular_file(
            source / L"Data" / L"package-integrity.ini", error)) {
        error.clear();
        const auto nested = source / L"KF2OptimizerNext";
        if (std::filesystem::is_regular_file(
                nested / L"Data" / L"package-integrity.ini", error)) {
            source = nested;
        }
    }
    const auto repaired = security::repair_package_from_directory(
        runtime.executable_root, source,
        app::current_build_identity().commit);
    if (!repaired.has_value()) {
        runtime.events->append(
            {0, product_diagnostics::Severity::error,
             "PACKAGE_REPAIR_REJECTED", repaired.error().message,
             L"package"});
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"PACKAGE_REPAIR_REJECTED",
                    repaired.error().message);
        return app::runtime::DispatchResult::handled;
    }
    if (repaired.value().repaired_files == 0) {
        runtime.events->append(
            {0, product_diagnostics::Severity::info,
             "PACKAGE_REPAIR_NOT_NEEDED",
             L"All required package files already matched their SHA-256 values",
             L"package"});
        show_notice(runtime, ui::NoticeSeverity::info,
                    L"PACKAGE_REPAIR_NOT_NEEDED",
                    L"All required files are already present and verified.");
        return app::runtime::DispatchResult::handled;
    }

    runtime.events->append(
        {0, product_diagnostics::Severity::info,
         "PACKAGE_REPAIR_APPLIED",
         L"The user selected a matching complete package; " +
             std::to_wstring(repaired.value().repaired_files) +
             L" missing or damaged required files were imported atomically and verified",
         L"package"});
    show_notice(
        runtime, ui::NoticeSeverity::info, L"PACKAGE_REPAIR_APPLIED",
        std::to_wstring(repaired.value().repaired_files) +
            L" required files were imported and verified. Restart KF2 Optimizer to load them.");
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult auto_repair_package(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.start_auto_package_repair();
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult flex_restore(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    if (!runtime.installation) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"FLEX_RESTORE_UNAVAILABLE",
                    L"Detect a valid KF2 installation first.");
        return app::runtime::DispatchResult::handled;
    }
    const bool running = game::find_running_game_process(
        runtime.installation->executable).has_value();
    const auto restored = flex::restore_offline_lab(
        runtime.installation->install_root / L"Binaries" / L"Win64",
        runtime.settings_path.parent_path() / L"flex-lab", running);
    show_notice(runtime,
                restored.has_value() ? ui::NoticeSeverity::info
                                     : ui::NoticeSeverity::error,
                restored.has_value() ? L"FLEX_ORIGINAL_RESTORED"
                                     : L"FLEX_RESTORE_BLOCKED",
                restored.has_value()
                    ? L"Original FleX runtime restored and hash-verified."
                    : restored.error().message);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult full_check(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    std::size_t passed = 0;
    std::size_t failed = 0;
    std::size_t unavailable = 0;
    const auto record = [&](bool success, std::string code,
                            std::wstring message) {
        success ? ++passed : ++failed;
        runtime.events->append(
            {0,
             success ? product_diagnostics::Severity::info
                     : product_diagnostics::Severity::error,
             std::move(code), std::move(message), L"self-check"});
    };
    const auto record_unavailable = [&](std::string code,
                                        std::wstring message) {
        ++unavailable;
        runtime.events->append(
            {0, product_diagnostics::Severity::warning,
             std::move(code), std::move(message), L"self-check"});
    };

    const auto data_root = runtime.settings_path.parent_path();
    record(data_root.is_absolute() &&
               std::filesystem::is_directory(data_root),
           "SELF_CHECK_DATA",
           L"Portable absolute Data directory is available");
    record(runtime.events->persistence_ready(), "SELF_CHECK_EVENT_LOG",
           runtime.events->persistence_ready()
               ? L"Bounded atomic event persistence is ready"
               : L"Bounded event persistence is unavailable");
    record(product_diagnostics::issue72_feature_inventory().size() == 149,
           "SELF_CHECK_INVENTORY",
           L"All 149 Issue 72 function records are available");
    const auto settings_roundtrip = config::parse_settings(
        config::serialize_settings(runtime.optimizer_settings));
    record(settings_roundtrip.has_value(), "SELF_CHECK_SETTINGS",
           settings_roundtrip.has_value()
               ? L"Settings serialize/parse contract passed"
               : L"Settings serialize/parse contract failed");
    record(config::all_settings().size() == 213,
           "SELF_CHECK_CATALOG",
           L"Strict typed KF2 catalog contains exactly 213 settings");
    const auto hardware = telemetry::query_hardware_inventory();
    record(hardware.has_value() &&
               hardware.value().logical_processors > 0 &&
               hardware.value().installed_memory_bytes > 0,
           "SELF_CHECK_HARDWARE",
           hardware.has_value()
               ? L"CPU groups and installed/available memory were queried"
               : L"CPU and memory inventory could not be queried");
    const auto system_memory = telemetry::query_system_memory_metrics();
    record(system_memory.has_value(), "SELF_CHECK_SYSTEM_MEMORY",
           system_memory.has_value()
               ? L"Live system RAM pressure source is available"
               : L"Live system RAM pressure source is unavailable");
    const auto adapters = telemetry::enumerate_gpu_adapters();
    const bool physical_gpu = adapters.has_value() &&
        std::any_of(adapters.value().begin(), adapters.value().end(),
                    [](const auto& adapter) { return !adapter.software; });
    record(physical_gpu, "SELF_CHECK_GPU",
           physical_gpu
               ? L"At least one physical DXGI GPU identity is available"
               : L"No physical DXGI GPU identity is available");

    if (!runtime.installation) {
        record_unavailable(
            "SELF_CHECK_GAME_UNAVAILABLE",
            L"A verified KF2 installation is not currently available");
    }

    if (runtime.installation) {
        const auto process = game::find_running_game_process(
            runtime.installation->executable);
        record(process.has_value() ||
                   process.error().code == ErrorCode::not_found,
               "SELF_CHECK_PROCESS",
               process.has_value()
                   ? L"Running KF2 process identity is verified"
                   : L"KF2 is stopped; process identity check is ready");
        const auto directory = runtime.installation->install_root /
            L"Binaries" / L"Win64";
        const bool lab_active =
            std::filesystem::exists(
                directory / L"flexRelease_original.dll") &&
            std::filesystem::exists(
                runtime.settings_path.parent_path() / L"flex-lab" /
                L"flex-lab-transaction.marker");
        const auto audited = flex::audit_runtime(
            directory / (lab_active ? L"flexRelease_original.dll"
                                    : L"flexRelease_x64.dll"),
            lab_active);
        record(audited.has_value() &&
                   audited.value().exact_known_runtime,
               "SELF_CHECK_FLEX",
               audited.has_value() && audited.value().exact_known_runtime
                   ? L"Known KF2 FleX ABI and identity verified"
                   : L"KF2 FleX ABI or identity did not pass verification");
        const auto listed = runtime.backups.list_backups();
        bool backup_ok = listed.has_value();
        if (backup_ok) {
            for (const auto& backup : listed.value()) {
                if (!runtime.backups.verify(backup).has_value()) {
                    backup_ok = false;
                    break;
                }
            }
        }
        record(backup_ok, "SELF_CHECK_BACKUP",
               backup_ok
                   ? L"Backup index and every retained backup verify"
                   : L"Backup index or a retained backup failed verification");
        const auto resumed = config::resume_session_config(
            runtime.installation->config_root, data_root);
        record(resumed.has_value(), "SELF_CHECK_SESSION_CONFIG",
               resumed.has_value()
                   ? (resumed.value()
                          ? L"A verified protected INI session snapshot is active"
                          : L"No incomplete protected INI session snapshot exists")
                   : L"Protected INI session snapshot failed verification");
    }

    const auto crash_count =
        product_diagnostics::retained_crash_record_count(
            data_root / L"logs" / L"crashes");
    record(crash_count <= 4, "SELF_CHECK_CRASH_RETENTION",
           L"Local privacy-bounded crash record retention is within its four-record limit");

    product_diagnostics::ProductReport self_check_report{
        .build_identity = runtime.model.build_identity(),
        .mode = runtime.model.status().mode,
        .game = runtime.model.status().game,
        .game_session = runtime.model.status().game_session,
        .telemetry = runtime.model.status().telemetry,
        .performance_analysis =
            runtime.model.status().performance_analysis,
        .hardware = runtime.model.status().hardware_summary,
        .flex = runtime.model.status().flex_telemetry,
        .optimizer_profile = runtime.model.status().profile,
        .quality_policy = runtime.model.status().quality,
        .overlay_position = runtime.model.status().overlay_position,
        .target_fps = runtime.model.status().target_fps,
        .overlay_scale_percent =
            runtime.model.status().overlay_scale_percent,
        .overlay_enabled = runtime.model.status().overlay_enabled,
        .restore_config_after_game =
            runtime.model.status().restore_config_after_game,
        .event_log_stats = runtime.events->stats(),
        .game_log_stats = runtime.game_log_session_parser.stats(),
        .retained_crash_records = crash_count,
        .events = runtime.events->snapshot(),
    };
    runtime.append_gameplay_report_fields(self_check_report);
    const auto document =
        product_diagnostics::serialize_product_report_json(
            self_check_report);
    const auto written = platform::windows::atomic_replace_utf8(
        runtime.settings_path.parent_path() / L"full-self-check.json",
        document);
    if (!written.has_value()) {
        ++failed;
        runtime.events->append(
            {0, product_diagnostics::Severity::error,
             "SELF_CHECK_REPORT_WRITE_FAILED", written.error().message,
             L"self-check"});
    } else {
        ++passed;
    }
    show_notice(runtime,
                failed == 0 ? ui::NoticeSeverity::info
                            : ui::NoticeSeverity::error,
                failed == 0 ? L"FULL_CHECK_PASSED"
                            : L"FULL_CHECK_FAILED",
                L"Full local check: " + std::to_wstring(passed) +
                    L" passed, " + std::to_wstring(failed) +
                    L" failed, " + std::to_wstring(unavailable) +
                    L" unavailable. Detailed local report saved in Data.");
    return app::runtime::DispatchResult::handled;
}

}  // namespace kf2::features::diagnostics
