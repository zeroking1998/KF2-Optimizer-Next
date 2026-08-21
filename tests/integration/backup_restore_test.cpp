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

    const auto listed = store.list_backups();
    CHECK(listed.has_value());
    CHECK(listed.value().size() >= 2);
    const auto recovered = kf2::backup::recover_transactions(store, config_root);
    CHECK(recovered.has_value());
    CHECK(recovered.value().outcome == kf2::backup::RecoveryOutcome::clean);

    const auto object_path = applied.value().backup.snapshots[0].object_path;
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

    const auto pruned = store.prune_verified({.keep_latest = 1});
    CHECK(pruned.has_value());
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

    const auto blocked = kf2::backup::restore_backup(
        store, applied.value().backup.id, config_root, {.game_running = true});
    CHECK(!blocked.has_value());

    fs::remove_all(root);
    return EXIT_SUCCESS;
}
