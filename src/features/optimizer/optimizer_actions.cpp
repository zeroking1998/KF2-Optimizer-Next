#include "features/optimizer/optimizer_actions.hpp"

#include "app/application_runtime.hpp"

namespace kf2::features::optimizer {
namespace {

namespace product_backup = ::kf2::backup;
namespace product_optimizer = ::kf2::optimizer;

void show_notice(app::UiRuntime& runtime, ui::NoticeSeverity severity,
                 std::wstring code, std::wstring message) {
    runtime.model.set_notice(
        {severity, std::move(code), std::move(message), L""});
    runtime.invalidate();
}

}  // namespace

app::runtime::DispatchResult preview(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const product_optimizer::QualityPolicy quality =
        runtime.optimizer_settings.offline_gameplay_telemetry
            ? product_optimizer::QualityPolicy::performance
            : product_optimizer::QualityPolicy::exact;
    const product_optimizer::Profile profile =
        app::stored_adaptive_profile(runtime.optimizer_settings);
    const auto result = runtime.prepare_optimizer({
        .target_fps = runtime.optimizer_settings.target_fps,
        .quality = quality,
        .profile = profile,
        .profile_preview_requested = true,
        .evidence = runtime.optimizer_evidence,
    });
    show_notice(runtime,
                result.has_value() ? ui::NoticeSeverity::info
                                   : ui::NoticeSeverity::warning,
                result.has_value() ? L"PREVIEW_READY"
                                   : L"PREVIEW_UNAVAILABLE",
                result.has_value()
                    ? L"Safe preview is ready; review it, then apply."
                    : result.error().message);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult export_preview(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    if (!runtime.preview) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"EXPORT_UNAVAILABLE",
                    L"Create a safe preview before exporting it.");
        return app::runtime::DispatchResult::handled;
    }
    const auto document = product_backup::export_preview_json(*runtime.preview);
    if (!document.has_value()) {
        show_notice(runtime, ui::NoticeSeverity::error,
                    L"EXPORT_FAILED", document.error().message);
        return app::runtime::DispatchResult::handled;
    }
    const auto exported = platform::windows::atomic_replace_utf8(
        runtime.settings_path.parent_path() / L"optimizer-preview.json",
        document.value());
    show_notice(runtime,
                exported.has_value() ? ui::NoticeSeverity::info
                                     : ui::NoticeSeverity::error,
                exported.has_value() ? L"PREVIEW_EXPORTED"
                                     : L"EXPORT_FAILED",
                exported.has_value()
                    ? L"Validated preview exported to the portable Data folder."
                    : exported.error().message);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult import_preview(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const auto path =
        runtime.settings_path.parent_path() / L"optimizer-preview.json";
    std::error_code size_error;
    const auto size = std::filesystem::file_size(path, size_error);
    if (size_error || size > 1024 * 1024) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"IMPORT_UNAVAILABLE",
                    L"A valid optimizer-preview.json was not found in Data.");
        return app::runtime::DispatchResult::handled;
    }
    std::ifstream input(path, std::ios::binary);
    const std::string document{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    const auto imported =
        product_backup::import_requested_changes_json(document);
    if (!imported.has_value()) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"IMPORT_REJECTED", imported.error().message);
        return app::runtime::DispatchResult::handled;
    }
    const auto prepared =
        runtime.prepare(imported.value(), L"Imported local preview");
    show_notice(runtime,
                prepared.has_value() ? ui::NoticeSeverity::info
                                     : ui::NoticeSeverity::warning,
                prepared.has_value() ? L"IMPORT_PREVIEW_READY"
                                     : L"IMPORT_REJECTED",
                prepared.has_value()
                    ? L"Imported values passed validation; review before applying."
                    : prepared.error().message);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult apply_preview(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    const bool running = runtime.installation &&
        game::find_running_game_process(
            runtime.installation->executable).has_value();
    const auto result = runtime.apply({.game_running = running});
    if (result.has_value()) {
        runtime.last_backup_id = result.value().backup.id;
    }
    show_notice(runtime,
                result.has_value() ? ui::NoticeSeverity::info
                                   : ui::NoticeSeverity::warning,
                result.has_value() ? L"CONFIG_APPLIED"
                                   : L"CONFIG_APPLY_BLOCKED",
                result.has_value()
                    ? L"Configuration applied; restore is available."
                    : result.error().message);
    return app::runtime::DispatchResult::handled;
}

}  // namespace kf2::features::optimizer
