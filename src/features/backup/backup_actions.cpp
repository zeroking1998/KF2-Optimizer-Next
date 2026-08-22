#include "features/backup/backup_actions.hpp"

#include "app/application_runtime.hpp"

namespace kf2::features::backup {
namespace {

namespace product_diagnostics = ::kf2::diagnostics;

void show_notice(app::UiRuntime& runtime, ui::NoticeSeverity severity,
                 std::wstring code, std::wstring message) {
    runtime.model.set_notice(
        {severity, std::move(code), std::move(message), L""});
    runtime.invalidate();
}

}  // namespace

app::runtime::DispatchResult create(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    if (!runtime.installation) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"BACKUP_UNAVAILABLE",
                    L"A verified KF2 installation was not found.");
        return app::runtime::DispatchResult::handled;
    }
    if (game::find_running_game_process(
            runtime.installation->executable).has_value()) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"BACKUP_GAME_RUNNING",
                    L"Close KF2 before creating a consistent INI backup.");
        return app::runtime::DispatchResult::handled;
    }
    config::ConfigPreview backup_preview;
    backup_preview.config_root = runtime.installation->config_root;
    constexpr std::array<const wchar_t*, 3> files{
        L"KFEngine.ini", L"KFGame.ini", L"KFSystemSettings.ini"};
    for (const auto* name : files) {
        const auto relative = std::filesystem::path{name};
        auto bytes = app::read_verified_local_file(
            runtime.installation->config_root / relative,
            16 * 1024 * 1024);
        if (!bytes.has_value()) {
            show_notice(runtime, ui::NoticeSeverity::error,
                        L"BACKUP_BLOCKED", bytes.error().message);
            return app::runtime::DispatchResult::handled;
        }
        backup_preview.files.push_back(
            {relative, bytes.value(), bytes.value()});
    }
    auto created = runtime.backups.create_standalone(backup_preview);
    if (created.has_value()) runtime.last_backup_id = created.value().id;
    if (created.has_value()) {
        const auto pruned =
            runtime.backups.prune_verified({.keep_latest = 8});
        if (!pruned.has_value()) {
            runtime.events->append(
                {0, product_diagnostics::Severity::warning,
                 "BACKUP_RETENTION_FAILED", pruned.error().message,
                 L"backup"});
        } else if (pruned.value() != 0) {
            runtime.events->append(
                {0, product_diagnostics::Severity::info,
                 "BACKUP_RETENTION_APPLIED",
                 L"Removed " + std::to_wstring(pruned.value()) +
                     L" older verified backups; eight newest are retained",
                 L"backup"});
        }
        runtime.events->append(
            {0, product_diagnostics::Severity::info,
             "CONFIG_BACKUP_CREATED",
             L"Verified standalone backup created for KFEngine.ini, KFGame.ini and KFSystemSettings.ini",
             L"backup"});
    }
    show_notice(runtime,
                created.has_value() ? ui::NoticeSeverity::info
                                    : ui::NoticeSeverity::error,
                created.has_value() ? L"BACKUP_CREATED" : L"BACKUP_FAILED",
                created.has_value()
                    ? L"The three verified KF2 INI files were backed up and hash-verified."
                    : created.error().message);
    return app::runtime::DispatchResult::handled;
}

app::runtime::DispatchResult restore(
    app::UiRuntime& runtime, const app::runtime::NoPayload&) {
    if (runtime.last_backup_id.empty()) {
        show_notice(runtime, ui::NoticeSeverity::warning,
                    L"RESTORE_UNAVAILABLE",
                    L"No backup from this session is available to restore.");
        return app::runtime::DispatchResult::handled;
    }
    const bool running = runtime.installation &&
        game::find_running_game_process(
            runtime.installation->executable).has_value();
    const auto result = runtime.restore(
        runtime.last_backup_id, {.game_running = running});
    show_notice(runtime,
                result.has_value() ? ui::NoticeSeverity::info
                                   : ui::NoticeSeverity::warning,
                result.has_value() ? L"CONFIG_RESTORED"
                                   : L"RESTORE_BLOCKED",
                result.has_value()
                    ? L"Configuration restored from verified backup."
                    : result.error().message);
    return app::runtime::DispatchResult::handled;
}

}  // namespace kf2::features::backup
