#include "features/diagnostics/diagnostics_actions.hpp"

#include "app/application_runtime.hpp"

namespace kf2::features::diagnostics {
namespace {

namespace product_diagnostics = ::kf2::diagnostics;

void show_notice(app::UiRuntime& runtime, ui::NoticeSeverity severity,
                 std::wstring code, std::wstring message) {
    runtime.model.set_notice(
        {severity, std::move(code), std::move(message), L""});
    runtime.invalidate();
}

app::runtime::DispatchResult run_benchmark(app::UiRuntime& runtime,
                                           bool capture_baseline) {
    const auto& frames = runtime.last_frame_metrics;
    const auto& current_session =
        runtime.game_log_session_parser.current();
    if (!runtime.installation || !frames.average_fps ||
        !frames.one_percent_low_fps || !frames.p95_ms ||
        frames.quality == telemetry::SampleQuality::unavailable ||
        !current_session ||
        !game::game_log_is_active_gameplay(*current_session)) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"BENCHMARK_UNAVAILABLE",
                    L"Start an active KF2 map and wait for fresh FPS, 1% low and frame-time data before capturing a benchmark.");
        return app::runtime::DispatchResult::handled;
    }
    std::string scenario = "kf2-session";
    scenario = current_session->map.empty() ? scenario : current_session->map;
    const std::string configuration =
        runtime.optimizer_settings.optimizer_profile + "-" +
        runtime.optimizer_settings.quality_policy + "-" +
        std::to_string(runtime.optimizer_settings.target_fps);
    const benchmark::Run candidate{
        scenario, configuration, *frames.average_fps,
        *frames.one_percent_low_fps, *frames.p95_ms,
        static_cast<std::uint64_t>(frames.stutter_count)};
    const auto valid = benchmark::validate(candidate);
    if (!valid.has_value()) {
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"BENCHMARK_REJECTED", valid.error().message);
        return app::runtime::DispatchResult::handled;
    }
    const auto baseline_path = runtime.settings_path.parent_path() /
        L"benchmark-baseline.ini";
    const auto history_path = runtime.settings_path.parent_path() /
        L"benchmark-history.ini";
    const auto append_history = [&]() -> Result<bool> {
        std::vector<benchmark::HistoryEntry> history;
        if (std::filesystem::exists(history_path)) {
            const auto history_document =
                app::read_verified_local_file(history_path, 256 * 1024);
            const auto parsed = history_document.has_value()
                ? benchmark::parse_history(history_document.value())
                : Result<std::vector<benchmark::HistoryEntry>>::failure(
                      history_document.error());
            if (!parsed.has_value()) {
                return Result<bool>::failure(parsed.error());
            }
            history = parsed.value();
        }
        history = benchmark::append_history(
            std::move(history), candidate, 64);
        return platform::windows::atomic_replace_utf8(
            history_path, benchmark::serialize_history(history));
    };
    if (capture_baseline) {
        const auto saved = platform::windows::atomic_replace_utf8(
            baseline_path, benchmark::serialize(candidate));
        const auto history_saved = saved.has_value()
            ? append_history()
            : Result<bool>::failure(saved.error());
        if (history_saved.has_value()) {
            runtime.events->append(
                {0, product_diagnostics::Severity::info,
                 "BENCHMARK_BASELINE_SAVED",
                 L"Fresh A/B baseline and bounded history entry saved for " +
                     std::wstring{scenario.begin(), scenario.end()},
                 L"benchmark"});
        }
        show_notice(
            runtime,
            history_saved.has_value() ? ui::NoticeSeverity::info
                                      : ui::NoticeSeverity::error,
            history_saved.has_value() ? L"BENCHMARK_BASELINE_SAVED"
                                      : L"BENCHMARK_SAVE_FAILED",
            history_saved.has_value()
                ? L"Fresh baseline saved. Keep the same map/scenario, change only the intended profile, then use Compare with baseline."
                : history_saved.error().message);
        return app::runtime::DispatchResult::handled;
    }
    if (!std::filesystem::exists(baseline_path)) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"BENCHMARK_BASELINE_MISSING",
                    L"Capture an A/B baseline first.");
        return app::runtime::DispatchResult::handled;
    }
    const auto document =
        app::read_verified_local_file(baseline_path, 64 * 1024);
    const auto baseline = document.has_value()
        ? benchmark::parse(document.value())
        : Result<benchmark::Run>::failure(document.error());
    if (!baseline.has_value()) {
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"BENCHMARK_BASELINE_INVALID",
                    baseline.error().message);
        return app::runtime::DispatchResult::handled;
    }
    const auto comparison = benchmark::compare(baseline.value(), candidate);
    if (comparison.outcome == benchmark::Comparison::incompatible) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"BENCHMARK_INCOMPATIBLE",
                    comparison.reason +
                        L". Capture a new baseline on this map/scenario.");
        return app::runtime::DispatchResult::handled;
    }
    const wchar_t* outcome =
        comparison.outcome == benchmark::Comparison::improved
            ? L"Improved"
            : comparison.outcome == benchmark::Comparison::regressed
                ? L"Regressed"
                : comparison.outcome == benchmark::Comparison::incompatible
                    ? L"Incompatible" : L"Inconclusive";
    std::wostringstream result;
    result << std::fixed << std::setprecision(1)
           << outcome << L": AVG "
           << comparison.average_fps_change_percent << L"%, 1% low "
           << comparison.one_percent_low_change_percent
           << L"%, p95 frame time "
           << comparison.p95_frame_time_change_percent << L"%. "
           << comparison.reason;
    runtime.events->append(
        {0,
         comparison.outcome == benchmark::Comparison::regressed
             ? product_diagnostics::Severity::warning
             : product_diagnostics::Severity::info,
         "BENCHMARK_COMPARED", result.str(), L"benchmark"});
    const auto saved = platform::windows::atomic_replace_utf8(
        runtime.settings_path.parent_path() / L"benchmark-candidate.ini",
        benchmark::serialize(candidate));
    const auto history_saved = saved.has_value()
        ? append_history()
        : Result<bool>::failure(saved.error());
    show_notice(runtime,
                history_saved.has_value() ? ui::NoticeSeverity::info
                                          : ui::NoticeSeverity::error,
                history_saved.has_value() ? L"BENCHMARK_COMPARED"
                                          : L"BENCHMARK_SAVE_FAILED",
                history_saved.has_value() ? result.str()
                                          : history_saved.error().message);
    return app::runtime::DispatchResult::handled;
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

app::runtime::DispatchResult refresh(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto snapshot = runtime.events->snapshot();
    const auto event_stats = runtime.events->stats();
    std::size_t warnings = 0;
    std::size_t errors = 0;
    for (const auto& event : snapshot) {
        if (event.severity == product_diagnostics::Severity::warning) ++warnings;
        if (event.severity == product_diagnostics::Severity::error) ++errors;
    }
    const std::wstring hardware_summary = app::query_hardware_summary();
    auto status = runtime.model.status();
    status.hardware_summary = hardware_summary;
    runtime.model.set_status(std::move(status));
    runtime.events->append(
        {0, product_diagnostics::Severity::info,
         "HARDWARE_REFRESHED", hardware_summary, L"hardware"});
    show_notice(
        runtime,
        errors > 0 ? ui::NoticeSeverity::error
                   : warnings > 0 ? ui::NoticeSeverity::warning
                                  : ui::NoticeSeverity::info,
        L"DIAGNOSTICS_STATUS",
        L"Local events: " + std::to_wstring(snapshot.size()) +
            L" | overwritten " +
            std::to_wstring(event_stats.overwritten) +
            L" | deduplicated " +
            std::to_wstring(event_stats.deduplicated) +
            L" | persistence failures " +
            std::to_wstring(event_stats.persistence_failures) +
            L" | warnings: " + std::to_wstring(warnings) +
            L" | errors: " + std::to_wstring(errors) + L" | " +
            hardware_summary);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult benchmark_baseline(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return run_benchmark(runtime, true);
}

app::runtime::DispatchResult benchmark_compare(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    return run_benchmark(runtime, false);
}

app::runtime::DispatchResult clear(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    runtime.events->clear();
    runtime.events->append(
        {0, product_diagnostics::Severity::info,
         "DIAGNOSTICS_CLEARED",
         L"Local diagnostic events cleared by the user",
         L"diagnostics"});
    show_notice(runtime, ui::NoticeSeverity::info,
                L"DIAGNOSTICS_CLEARED",
                L"Local diagnostic events were cleared.");
    return app::runtime::DispatchResult::handled;
}

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

app::runtime::DispatchResult export_report(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto document = product_diagnostics::serialize_product_report_json(
        make_product_report(runtime));
    const auto exported = platform::windows::atomic_replace_utf8(
        runtime.settings_path.parent_path() / L"diagnostics-report.json",
        document);
    show_notice(runtime,
                exported.has_value() ? ui::NoticeSeverity::info
                                     : ui::NoticeSeverity::error,
                exported.has_value() ? L"DIAGNOSTICS_EXPORTED"
                                     : L"EXPORT_FAILED",
                exported.has_value()
                    ? L"Local diagnostic report exported to Data. Nothing was uploaded."
                    : exported.error().message);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult export_inventory(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto document = product_diagnostics::serialize_feature_inventory_json(
        app::format_build_identity(app::current_build_identity()));
    const auto exported = platform::windows::atomic_replace_utf8(
        runtime.settings_path.parent_path() / L"issue72-feature-inventory.json",
        document);
    if (exported.has_value()) {
        runtime.events->append(
            {0, product_diagnostics::Severity::info,
             "ISSUE72_INVENTORY_EXPORTED",
             L"All 149 functions from the 18 Issue 72 areas were exported locally",
             L"diagnostics"});
    }
    show_notice(runtime,
                exported.has_value() ? ui::NoticeSeverity::info
                                     : ui::NoticeSeverity::error,
                exported.has_value() ? L"ISSUE72_INVENTORY_EXPORTED"
                                     : L"EXPORT_FAILED",
                exported.has_value()
                    ? L"All 149 Issue 72 functions were exported to Data. Nothing was uploaded."
                    : exported.error().message);
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

app::runtime::DispatchResult flex_audit(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    if (!runtime.installation) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"FLEX_AUDIT_UNAVAILABLE",
                    L"Detect a valid KF2 installation first.");
        return app::runtime::DispatchResult::handled;
    }
    const auto directory = runtime.installation->install_root /
        L"Binaries" / L"Win64";
    const auto lab_active =
        std::filesystem::exists(directory / L"flexRelease_original.dll") &&
        std::filesystem::exists(runtime.settings_path.parent_path() /
                                L"flex-lab" /
                                L"flex-lab-transaction.marker");
    const auto path = directory /
        (lab_active ? L"flexRelease_original.dll" : L"flexRelease_x64.dll");
    const auto audit = flex::audit_runtime(path, lab_active);
    if (!audit.has_value()) {
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"FLEX_AUDIT_FAILED", audit.error().message);
        return app::runtime::DispatchResult::handled;
    }
    const auto written = platform::windows::atomic_replace_utf8(
        runtime.settings_path.parent_path() / L"flex-runtime-audit.json",
        flex::serialize_audit_json(audit.value()));
    std::wstring live_detail;
    if (runtime.game_process) {
        if (const auto observed = flex::read_observation(*runtime.game_process)) {
            live_detail = L" Live observation: " +
                std::to_wstring(observed->update_calls) +
                L" solver updates, " +
                std::to_wstring(observed->last_substeps) +
                L" substeps, relay " +
                (observed->pass_through_healthy
                     ? L"healthy."
                     : L"error detected.");
        } else if (lab_active) {
            live_detail =
                L" The laboratory is installed, but this KF2 process has not published valid FleX data yet.";
        }
    }
    std::wstring audit_message = !written.has_value()
        ? written.error().message
        : audit.value().exact_known_runtime
            ? (lab_active
                   ? L"Offline FleX laboratory is active; its preserved NVIDIA FleX 1.0.5 original is hash- and ABI-verified."
                   : L"Known KF2 FleX 1.0.5 x64 runtime verified. Offline laboratory report exported.")
            : L"FleX version, hash or ABI mismatch; offline hook remains fail-closed.";
    audit_message += live_detail;
    show_notice(runtime,
                written.has_value() && audit.value().exact_known_runtime
                    ? ui::NoticeSeverity::info
                    : ui::NoticeSeverity::error,
                written.has_value() && audit.value().exact_known_runtime
                    ? L"FLEX_ABI_VERIFIED"
                    : L"FLEX_AUDIT_FAILED",
                std::move(audit_message));
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

    const auto history_path = data_root / L"benchmark-history.ini";
    if (std::filesystem::exists(history_path)) {
        const auto history_document =
            app::read_verified_local_file(history_path, 256 * 1024);
        const auto history = history_document.has_value()
            ? benchmark::parse_history(history_document.value())
            : Result<std::vector<benchmark::HistoryEntry>>::failure(
                  history_document.error());
        record(history.has_value(), "SELF_CHECK_BENCHMARK_HISTORY",
               history.has_value()
                   ? L"Bounded A/B benchmark history parses successfully"
                   : L"A/B benchmark history failed verification");
    } else {
        record_unavailable("SELF_CHECK_BENCHMARK_NOT_CAPTURED",
                           L"No A/B benchmark has been captured yet");
    }

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
