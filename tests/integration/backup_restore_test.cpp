#include <Windows.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>

#include "kf2/backup/restore_transaction.hpp"
#include "kf2/config/apply_transaction.hpp"
#include "kf2/config/setting_catalog.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

void write_bytes(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << bytes;
}

void write_journal(const kf2::backup::BackupSet& backup, std::string_view state) {
    write_bytes(backup.journal_path,
        "version=1\nstate=" + std::string{state} + "\nid=" + backup.id + "\n");
}

std::filesystem::path concurrent_recovery_target;

void change_target_before_recovery_commit() {
    write_bytes(concurrent_recovery_target, "concurrent recovery edit");
}

HANDLE lock_without_read_sharing(const std::filesystem::path& path) {
    return CreateFileW(path.c_str(), GENERIC_READ,
                       FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool deny_status(const std::filesystem::path&, std::error_code& error) {
    error = std::make_error_code(std::errc::permission_denied);
    return false;
}

bool has_quarantined_copy(const std::filesystem::path& source,
                          std::string_view expected_bytes) {
    const auto prefix = source.filename().wstring() + L".corrupt";
    std::error_code error;
    for (const auto& entry :
         std::filesystem::directory_iterator(source.parent_path(), error)) {
        if (error) return false;
        if (entry.path().filename().wstring().starts_with(prefix) &&
            read_bytes(entry.path()) == expected_bytes) {
            return true;
        }
    }
    return false;
}

std::string replace_manifest_file_field(std::string manifest,
                                        std::size_t field_index,
                                        std::string_view replacement) {
    std::size_t begin = manifest.find("file=");
    if (begin == std::string::npos) return {};
    begin += 5;
    for (std::size_t index = 0; index < field_index; ++index) {
        begin = manifest.find('|', begin);
        if (begin == std::string::npos) return {};
        ++begin;
    }
    const auto delimiter = manifest.find('|', begin);
    const auto newline = manifest.find('\n', begin);
    const auto end = std::min(delimiter, newline);
    if (end == std::string::npos) return {};
    manifest.replace(begin, end - begin, replacement);
    return manifest;
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    fs::remove_all(root);
    const auto config_root = root / L"Config";
    const auto target = config_root / L"KFEngine.ini";
    const std::string original = "[Engine.Engine]\r\nMaxSmoothedFrameRate=62\r\n";
    const std::string proposed = "[Engine.Engine]\r\nMaxSmoothedFrameRate=90\r\n";
    write_bytes(target, original);

    kf2::config::ConfigPreview preview;
    preview.config_root = config_root;
    preview.files.push_back({L"KFEngine.ini", original, proposed});
    kf2::backup::BackupStore status_error_store{
        root / L"StatusErrorState"};
    kf2::backup::set_backup_status_hook_for_testing(&deny_status);
    const auto inaccessible_backup =
        status_error_store.create_standalone(preview);
    kf2::backup::set_backup_status_hook_for_testing(nullptr);
    CHECK(!inaccessible_backup.has_value());
    CHECK(inaccessible_backup.error().code == kf2::ErrorCode::io_failure);
    CHECK(inaccessible_backup.error().native_code != 0);
    kf2::backup::BackupStore store{root / L"State"};
    const auto standalone = store.create_standalone(preview);
    CHECK(standalone.has_value());
    CHECK(store.verify(standalone.value()).has_value());
    CHECK(read_bytes(standalone.value().journal_path).find("state=complete") !=
          std::string::npos);
    const auto applied = kf2::config::apply_preview(
        preview, store, {.game_running = false});
    CHECK(applied.has_value());
    CHECK(read_bytes(target) == proposed);

    write_journal(applied.value().backup, "replacement_started");
    const auto rolled_back = kf2::backup::recover_transactions(store, config_root);
    CHECK(rolled_back.has_value());
    CHECK(rolled_back.value().outcome == kf2::backup::RecoveryOutcome::rolled_back);
    CHECK(read_bytes(target) == original);
    CHECK(kf2::backup::recover_transactions(store, config_root).value().outcome ==
          kf2::backup::RecoveryOutcome::clean);

    const auto concurrent_root = root / L"ConcurrentRecoveryConfig";
    const auto concurrent_target = concurrent_root / L"KFEngine.ini";
    write_bytes(concurrent_target, original);
    kf2::config::ConfigPreview concurrent_preview;
    concurrent_preview.config_root = concurrent_root;
    concurrent_preview.files.push_back(
        {L"KFEngine.ini", original, proposed});
    kf2::backup::BackupStore concurrent_store{
        root / L"ConcurrentRecoveryState"};
    const auto concurrent_applied = kf2::config::apply_preview(
        concurrent_preview, concurrent_store, {.game_running = false});
    CHECK(concurrent_applied.has_value());
    write_journal(concurrent_applied.value().backup, "replacement_started");
    concurrent_recovery_target = concurrent_target;
    kf2::backup::set_recovery_commit_hook_for_testing(
        &change_target_before_recovery_commit);
    const auto concurrent_recovery = kf2::backup::recover_transactions(
        concurrent_store, concurrent_root);
    kf2::backup::set_recovery_commit_hook_for_testing(nullptr);
    CHECK(!concurrent_recovery.has_value());
    CHECK(concurrent_recovery.error().code == kf2::ErrorCode::stale_data);
    CHECK(read_bytes(concurrent_target) == "concurrent recovery edit");
    CHECK(read_bytes(concurrent_applied.value().backup.journal_path)
              .find("state=replacement_started") != std::string::npos);

    const auto reapplied = kf2::config::apply_preview(
        preview, store, {.game_running = false});
    CHECK(reapplied.has_value());
    write_journal(reapplied.value().backup, "verification_complete");
    const auto rolled_forward = kf2::backup::recover_transactions(store, config_root);
    CHECK(rolled_forward.has_value());
    CHECK(rolled_forward.value().outcome == kf2::backup::RecoveryOutcome::rolled_forward);
    CHECK(read_bytes(target) == proposed);

    const auto restored = kf2::backup::restore_backup(
        store, applied.value().backup.id, config_root, {.game_running = false});
    CHECK(restored.has_value());
    CHECK(restored.value().files_restored == 1);
    CHECK(read_bytes(target) == original);
    CHECK(store.verify(restored.value().pre_restore_backup).has_value());

    const auto unreadable_restore_root = root / L"UnreadableRestoreConfig";
    const auto unreadable_restore_target =
        unreadable_restore_root / L"KFEngine.ini";
    write_bytes(unreadable_restore_target, "");
    kf2::config::ConfigPreview unreadable_restore_preview;
    unreadable_restore_preview.config_root = unreadable_restore_root;
    unreadable_restore_preview.files.push_back(
        {L"KFEngine.ini", "", "replacement"});
    kf2::backup::BackupStore unreadable_restore_store{
        root / L"UnreadableRestoreState"};
    const auto unreadable_restore_applied = kf2::config::apply_preview(
        unreadable_restore_preview, unreadable_restore_store,
        {.game_running = false});
    CHECK(unreadable_restore_applied.has_value());
    const auto restore_backups_before =
        unreadable_restore_store.list_backups();
    CHECK(restore_backups_before.has_value());
    HANDLE locked_restore =
        lock_without_read_sharing(unreadable_restore_target);
    CHECK(locked_restore != INVALID_HANDLE_VALUE);
    const auto unreadable_restore = kf2::backup::restore_backup(
        unreadable_restore_store,
        unreadable_restore_applied.value().backup.id,
        unreadable_restore_root, {.game_running = false});
    CHECK(!unreadable_restore.has_value());
    CHECK(unreadable_restore.error().message.find(L"read") !=
          std::wstring::npos);
    CHECK(CloseHandle(locked_restore) != FALSE);
    CHECK(read_bytes(unreadable_restore_target) == "replacement");
    const auto restore_backups_after =
        unreadable_restore_store.list_backups();
    CHECK(restore_backups_after.has_value());
    CHECK(restore_backups_after.value().size() ==
          restore_backups_before.value().size());

    const auto unreadable_recovery_root = root / L"UnreadableRecoveryConfig";
    const auto unreadable_recovery_target =
        unreadable_recovery_root / L"KFEngine.ini";
    write_bytes(unreadable_recovery_target, "");
    kf2::config::ConfigPreview unreadable_recovery_preview;
    unreadable_recovery_preview.config_root = unreadable_recovery_root;
    unreadable_recovery_preview.files.push_back(
        {L"KFEngine.ini", "", "replacement"});
    kf2::backup::BackupStore unreadable_recovery_store{
        root / L"UnreadableRecoveryState"};
    const auto unreadable_recovery_applied = kf2::config::apply_preview(
        unreadable_recovery_preview, unreadable_recovery_store,
        {.game_running = false});
    CHECK(unreadable_recovery_applied.has_value());
    write_journal(unreadable_recovery_applied.value().backup,
                  "replacement_started");
    HANDLE locked_recovery =
        lock_without_read_sharing(unreadable_recovery_target);
    CHECK(locked_recovery != INVALID_HANDLE_VALUE);
    const auto unreadable_recovery = kf2::backup::recover_transactions(
        unreadable_recovery_store, unreadable_recovery_root);
    CHECK(!unreadable_recovery.has_value());
    CHECK(unreadable_recovery.error().message.find(L"read") !=
          std::wstring::npos);
    CHECK(CloseHandle(locked_recovery) != FALSE);
    CHECK(read_bytes(unreadable_recovery_target) == "replacement");
    CHECK(read_bytes(unreadable_recovery_applied.value().backup.journal_path)
              .find("state=replacement_started") != std::string::npos);

    const auto listed = store.list_backups();
    CHECK(listed.has_value());
    CHECK(listed.value().size() >= 2);

    write_bytes(standalone.value().journal_path,
                "version=1\nnotstate=complete\nid=" +
                    standalone.value().id + "\n");
    const auto malformed_journal_recovery =
        kf2::backup::recover_transactions(store, config_root);
    CHECK(malformed_journal_recovery.has_value());
    CHECK(!fs::exists(standalone.value().journal_path));
    CHECK(fs::exists(fs::path{
        standalone.value().journal_path.wstring() + L".corrupt"}));
    write_journal(standalone.value(), "complete");

    const auto expect_invalid_journal_quarantined =
        [&](const std::string& bytes) {
            write_bytes(standalone.value().journal_path, bytes);
            const auto recovery =
                kf2::backup::recover_transactions(store, config_root);
            return recovery.has_value() &&
                   !fs::exists(standalone.value().journal_path) &&
                   has_quarantined_copy(standalone.value().journal_path, bytes);
        };
    const auto valid_id = standalone.value().id;
    CHECK(expect_invalid_journal_quarantined(
        "version=1\nstate=complete\nstate=replacement_started\nid=" +
        valid_id + "\n"));
    CHECK(expect_invalid_journal_quarantined(
        "version=1\nstate=complete\nid=" + std::string(64, 'f') + "\n"));
    CHECK(expect_invalid_journal_quarantined(
        "state=complete\nid=" + valid_id + "\n"));
    CHECK(expect_invalid_journal_quarantined(
        "version=2\nstate=complete\nid=" + valid_id + "\n"));
    CHECK(expect_invalid_journal_quarantined(
        "version=1\nstate=complete\n"));
    CHECK(expect_invalid_journal_quarantined(
        "version=1\nstate=replace"));
    CHECK(expect_invalid_journal_quarantined(
        "version=1\nstate=complete\nid=" + valid_id + "\ngarbage\n"));
    CHECK(expect_invalid_journal_quarantined(std::string(4097, 'x')));
    write_journal(standalone.value(), "complete");

    const auto recovered = kf2::backup::recover_transactions(store, config_root);
    CHECK(recovered.has_value());
    CHECK(recovered.value().outcome == kf2::backup::RecoveryOutcome::clean);

    const auto object_path = applied.value().backup.snapshots[0].object_path;
    fs::resize_file(object_path, 16U * 1024U * 1024U + 1U);
    const auto oversized_object = store.verify(applied.value().backup);
    CHECK(!oversized_object.has_value());
    CHECK(oversized_object.error().code == kf2::ErrorCode::access_denied);
    write_bytes(object_path, original);
    write_bytes(object_path, "corrupt");
    write_journal(applied.value().backup, "replacement_started");
    CHECK(!kf2::backup::recover_transactions(store, config_root).has_value());
    write_bytes(object_path, original);
    write_journal(applied.value().backup, "complete");

    const auto foreign_root = root / L"ForeignConfig";
    fs::create_directories(foreign_root);
    write_journal(applied.value().backup, "replacement_started");
    const auto unbound = kf2::backup::recover_transactions(store, std::nullopt);
    CHECK(!unbound.has_value());
    CHECK(unbound.error().code == kf2::ErrorCode::access_denied);
    const auto foreign_recovery =
        kf2::backup::recover_transactions(store, foreign_root);
    CHECK(!foreign_recovery.has_value());
    CHECK(foreign_recovery.error().code == kf2::ErrorCode::access_denied);
    write_journal(applied.value().backup, "complete");
    const auto foreign_restore = kf2::backup::restore_backup(
        store, applied.value().backup.id, foreign_root, {.game_running = false});
    CHECK(!foreign_restore.has_value());
    CHECK(foreign_restore.error().code == kf2::ErrorCode::access_denied);

    const auto original_manifest = read_bytes(applied.value().backup.manifest_path);
    write_bytes(applied.value().backup.manifest_path,
                original_manifest + "root=00\n");
    CHECK(!store.load_backup(applied.value().backup.id).has_value());
    write_bytes(applied.value().backup.manifest_path, original_manifest);
    CHECK(store.load_backup(applied.value().backup.id).has_value());

    auto boundary_manifest = replace_manifest_file_field(
        original_manifest, 1, "0");
    boundary_manifest = replace_manifest_file_field(
        std::move(boundary_manifest), 3, "16777216");
    CHECK(!boundary_manifest.empty());
    write_bytes(applied.value().backup.manifest_path, boundary_manifest);
    const auto lower_upper_boundaries =
        store.load_backup(applied.value().backup.id);
    CHECK(lower_upper_boundaries.has_value());
    CHECK(lower_upper_boundaries.value().snapshots[0].size == 0);
    CHECK(lower_upper_boundaries.value().snapshots[0].desired_size ==
          16U * 1024U * 1024U);

    boundary_manifest = replace_manifest_file_field(
        original_manifest, 1, "16777216");
    boundary_manifest = replace_manifest_file_field(
        std::move(boundary_manifest), 3, "0");
    CHECK(!boundary_manifest.empty());
    write_bytes(applied.value().backup.manifest_path, boundary_manifest);
    const auto upper_lower_boundaries =
        store.load_backup(applied.value().backup.id);
    CHECK(upper_lower_boundaries.has_value());
    CHECK(upper_lower_boundaries.value().snapshots[0].size ==
          16U * 1024U * 1024U);
    CHECK(upper_lower_boundaries.value().snapshots[0].desired_size == 0);

    constexpr std::array<std::string_view, 11> invalid_sizes{
        "", "18446744073709551616", "123junk", "123.5", "+123",
        "-1", " 123", "123 ", "1e2", "00", "0123"};
    for (const std::size_t field_index : {1U, 3U}) {
        for (const auto value : invalid_sizes) {
            const auto malformed = replace_manifest_file_field(
                original_manifest, field_index, value);
            CHECK(!malformed.empty());
            write_bytes(applied.value().backup.manifest_path, malformed);
            CHECK(!store.load_backup(applied.value().backup.id).has_value());
        }
    }
    write_bytes(applied.value().backup.manifest_path, original_manifest);
    CHECK(store.load_backup(applied.value().backup.id).has_value());

    write_bytes(target, "foreign change");
    write_journal(applied.value().backup, "replacement_started");
    const auto conflict = kf2::backup::recover_transactions(store, config_root);
    CHECK(!conflict.has_value());
    CHECK(conflict.error().code == kf2::ErrorCode::stale_data);
    write_bytes(target, original);
    write_journal(applied.value().backup, "complete");

    const std::string missing_id(64, '0');
    const auto orphan = store.state_root() / L"backups/journals" /
        (std::wstring(64, L'0') + L".journal");
    write_bytes(orphan, "version=1\nstate=complete\nid=" + missing_id + "\n");
    CHECK(!kf2::backup::recover_transactions(store, config_root).has_value());
    write_bytes(orphan, "version=1\nstate=replacement_started\nid=" + missing_id + "\n");
    CHECK(!kf2::backup::recover_transactions(store, config_root).has_value());
    fs::remove(orphan);

    write_journal(applied.value().backup, "backup_complete");
    const auto backup_only = kf2::backup::recover_transactions(store, config_root);
    CHECK(backup_only.has_value());
    CHECK(backup_only.value().outcome == kf2::backup::RecoveryOutcome::clean);

    preview.items.push_back({
        kf2::config::SettingId::target_fps, L"KFEngine.ini", L"Engine.Engine",
        L"MaxSmoothedFrameRate", 62, 120,
        kf2::config::ChangeSource::explicit_user,
        L"test"});
    const auto exported = kf2::backup::export_preview_json(preview);
    CHECK(exported.has_value());
    const auto imported = kf2::backup::import_requested_changes_json(
        R"({"version":1,"changes":[{"id":"target_fps","value":120,"source":"manual","reason":"test"},{"id":"blood_effect_limit","value":20},{"id":"body_wound_decal_lifetime","value":20},{"id":"blood_splatter_lifetime","value":8},{"id":"blood_pool_lifetime","value":15},{"id":"gore_lifetime_multiplier","value":0.75},{"id":"persistent_splats_per_frame","value":50},{"id":"secondary_blood_effects","value":false},{"id":"dynamic_decals","value":false},{"id":"decal_cull_distance_scale","value":0.5},{"id":"dynamic_shadows","value":false},{"id":"drop_particle_distortion","value":true},{"id":"max_shadow_resolution","value":512},{"id":"shadow_texels_per_pixel","value":0.9}]})");
    CHECK(imported.has_value());
    CHECK(imported.value().size() == 14);
    CHECK(imported.value()[0].id == kf2::config::SettingId::target_fps);
    CHECK(std::get<int>(imported.value()[0].value) == 120);
    CHECK(imported.value()[0].reason == L"test");
    CHECK(exported.value().find("\"reason\":\"test\"") != std::string::npos);
    CHECK(imported.value()[1].id == kf2::config::SettingId::blood_effect_limit);
    CHECK(imported.value()[2].id ==
          kf2::config::SettingId::body_wound_decal_lifetime);
    CHECK(imported.value()[3].id == kf2::config::SettingId::blood_splatter_lifetime);
    CHECK(imported.value()[4].id == kf2::config::SettingId::blood_pool_lifetime);
    CHECK(imported.value()[5].id ==
          kf2::config::SettingId::gore_lifetime_multiplier);
    CHECK(std::get<double>(imported.value()[5].value) == 0.75);
    CHECK(imported.value()[6].id ==
          kf2::config::SettingId::persistent_splats_per_frame);
    CHECK(imported.value()[7].id ==
          kf2::config::SettingId::secondary_blood_effects);
    CHECK(imported.value()[8].id == kf2::config::SettingId::dynamic_decals);
    CHECK(imported.value()[9].id ==
          kf2::config::SettingId::decal_cull_distance_scale);
    CHECK(std::get<double>(imported.value()[9].value) == 0.5);
    CHECK(imported.value()[10].id == kf2::config::SettingId::dynamic_shadows);
    CHECK(imported.value()[11].id ==
          kf2::config::SettingId::drop_particle_distortion);
    CHECK(imported.value()[12].id ==
          kf2::config::SettingId::max_shadow_resolution);
    CHECK(imported.value()[13].id ==
          kf2::config::SettingId::shadow_texels_per_pixel);
    CHECK(exported.value().find("\"id\":\"MaxSmoothedFrameRate\"") !=
          std::string::npos);

    kf2::config::ConfigPreview complete_catalog;
    std::size_t catalog_index = 0;
    for (const auto& definition : kf2::config::all_settings()) {
        kf2::config::SettingValue value;
        if (definition.type == kf2::config::SettingType::boolean) value = true;
        else if (definition.type == kf2::config::SettingType::integer) {
            value = definition.allowed_integers.empty()
                ? static_cast<int>(definition.minimum)
                : definition.allowed_integers.front();
        } else value = definition.minimum;
        complete_catalog.items.push_back({
            definition.id, definition.relative_path, definition.section,
            definition.key, value, value,
            kf2::config::ChangeSource::explicit_user,
            catalog_index++ == 0 ? L"quote \" slash \\ newline\nUnicode café" : L"",
            kf2::config::PreviewState::unchanged, true});
    }
    const auto complete_export =
        kf2::backup::export_preview_json(complete_catalog);
    CHECK(complete_export.has_value());
    const auto complete_import =
        kf2::backup::import_requested_changes_json(complete_export.value());
    CHECK(complete_import.has_value());
    CHECK(complete_import.value().size() == kf2::config::all_settings().size());
    CHECK(complete_import.value().front().reason ==
          L"quote \" slash \\ newline\nUnicode café");
    for (std::size_t index = 0; index < complete_import.value().size(); ++index) {
        CHECK(complete_import.value()[index].id ==
              kf2::config::all_settings()[index].id);
        CHECK(complete_import.value()[index].value ==
              complete_catalog.items[index].after);
    }
    CHECK(!kf2::backup::import_requested_changes_json(
        R"({"version":1,"changes":[{"id":"unknown","value":1}]})").has_value());
    CHECK(!kf2::backup::import_requested_changes_json(
        R"({"version":1,"changes":[{"id":"target_fps","value":60},{"id":"target_fps","value":61}]})").has_value());
    CHECK(!kf2::backup::import_requested_changes_json(
        R"({"version":1,"changes":[]} trailing)").has_value());

    const auto healthy_before_corruption = store.list_backups();
    CHECK(healthy_before_corruption.has_value());
    const auto healthy_count = healthy_before_corruption.value().size();
    const auto manifests = store.state_root() / L"backups/manifests";
    const auto stray_manifest = manifests / L"unfinished.manifest";
    write_bytes(stray_manifest, "not a backup manifest");
    const auto listed_around_stray = store.list_backups();
    CHECK(listed_around_stray.has_value());
    CHECK(listed_around_stray.value().size() == healthy_count);
    CHECK(!fs::exists(stray_manifest));
    CHECK(fs::exists(manifests / L"unfinished.manifest.corrupt"));

    const auto truncated_manifest =
        manifests / (std::wstring(64, L'b') + L".manifest");
    write_bytes(truncated_manifest, "version=2\nid=");
    const auto listed_around_truncated = store.list_backups();
    CHECK(listed_around_truncated.has_value());
    CHECK(listed_around_truncated.value().size() == healthy_count);
    CHECK(!fs::exists(truncated_manifest));
    CHECK(fs::exists(fs::path{truncated_manifest.wstring() + L".corrupt"}));

    const std::string mismatched_id(64, 'c');
    auto mismatched_bytes = original_manifest;
    const auto manifest_id = std::string{"id="} + applied.value().backup.id;
    const auto id_offset = mismatched_bytes.find(manifest_id);
    CHECK(id_offset != std::string::npos);
    mismatched_bytes.replace(id_offset, manifest_id.size(), "id=" + mismatched_id);
    const auto mismatched_manifest =
        manifests / (std::wstring(64, L'c') + L".manifest");
    write_bytes(mismatched_manifest, mismatched_bytes);
    const auto listed_around_mismatch = store.list_backups();
    CHECK(listed_around_mismatch.has_value());
    CHECK(listed_around_mismatch.value().size() == healthy_count);
    CHECK(!fs::exists(mismatched_manifest));
    CHECK(fs::exists(fs::path{mismatched_manifest.wstring() + L".corrupt"}));

    const auto unreadable_manifest =
        manifests / (std::wstring(64, L'd') + L".manifest");
    write_bytes(unreadable_manifest, "version=2\nid=");
    HANDLE locked_manifest = CreateFileW(
        unreadable_manifest.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(locked_manifest != INVALID_HANDLE_VALUE);
    const auto blocked_listing = store.list_backups();
    CHECK(!blocked_listing.has_value());
    CHECK(blocked_listing.error().message.find(L"cannot be safely quarantined") !=
          std::wstring::npos);

    const auto orphan_object = store.state_root() / L"backups/objects" /
        (std::wstring(64, L'e') + L".blob");
    write_bytes(orphan_object, "unreferenced");
    const auto blocked_prune = store.prune_verified({.keep_latest = 1});
    CHECK(!blocked_prune.has_value());
    CHECK(fs::exists(orphan_object));
    CHECK(CloseHandle(locked_manifest) != FALSE);

    const auto listed_after_unlock = store.list_backups();
    CHECK(listed_after_unlock.has_value());
    CHECK(listed_after_unlock.value().size() == healthy_count);
    CHECK(!fs::exists(unreadable_manifest));
    CHECK(fs::exists(fs::path{unreadable_manifest.wstring() + L".corrupt"}));

    // Simulate interruption after the journal was pruned but before the
    // corresponding manifest could be removed. The next retention pass must
    // finish this backup and continue with the remaining eligible backups.
    const auto partially_pruned = listed_after_unlock.value().back();
    CHECK(fs::exists(partially_pruned.manifest_path));
    CHECK(fs::remove(partially_pruned.journal_path));

    const auto pruned = store.prune_verified({.keep_latest = 1});
    CHECK(pruned.has_value());
    CHECK(!fs::exists(partially_pruned.manifest_path));
    const auto retained = store.list_backups();
    CHECK(retained.has_value());
    CHECK(retained.value().size() == 1);
    CHECK(store.verify(retained.value().front()).has_value());
    std::set<std::string> referenced_objects;
    for (const auto& snapshot : retained.value().front().snapshots) {
        referenced_objects.insert(snapshot.sha256);
    }
    std::size_t object_count = 0;
    for (const auto& object : fs::directory_iterator(
             store.state_root() / L"backups/objects")) {
        if (object.path().extension() != L".blob") continue;
        ++object_count;
        CHECK(referenced_objects.contains(object.path().stem().string()));
    }
    CHECK(object_count == referenced_objects.size());
    CHECK(!fs::exists(orphan_object));

    const auto blocked = kf2::backup::restore_backup(
        store, applied.value().backup.id, config_root, {.game_running = true});
    CHECK(!blocked.has_value());

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
