#include "kf2/flex/flex_lab.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

#include <Windows.h>

#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

#define CHECK(x) do { if (!(x)) { std::cerr << "check failed line " << __LINE__ << '\n'; return 1; } } while (0)

static std::string read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>{f}, {}};
}
static void write(const std::filesystem::path& p, const char* value) {
    std::ofstream f(p, std::ios::binary); f << value;
}

static std::filesystem::path race_active;
static std::filesystem::path race_forwarder;
static std::filesystem::path denied_status_path;
static std::filesystem::path replacement_path;
static bool mutation_succeeded = false;

static void replace_during_read(const std::filesystem::path& path) {
    mutation_succeeded = ReplaceFileW(
        path.c_str(), replacement_path.c_str(), nullptr,
        REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != FALSE;
}

static bool fail_selected_status(const std::filesystem::path& path,
                                 std::error_code& error) {
    if (path == denied_status_path) {
        error = std::make_error_code(std::errc::permission_denied);
        return false;
    }
    return std::filesystem::exists(path, error);
}

static void replace_source_at_checkpoint(
    kf2::flex::LabInstallTestCheckpoint checkpoint) {
    if (checkpoint == kf2::flex::LabInstallTestCheckpoint::sources_hashed &&
        !race_active.empty()) {
        write(race_active, "replacement-runtime");
    }
    if (checkpoint == kf2::flex::LabInstallTestCheckpoint::marker_written &&
        !race_forwarder.empty()) {
        write(race_forwarder, "replacement-forwarder");
    }
}

static int check_installation_ownership(const std::filesystem::path& root) {
    const auto owner = root / L"owner-\u00e4";
    const auto other = root / "other";
    const auto state = root / "state";
    const auto forwarder = root / "flexRelease_x64.forwarder-lab.dll";
    std::filesystem::create_directories(owner);
    std::filesystem::create_directories(other);
    write(owner / "flexRelease_x64.dll", "original-runtime");
    write(forwarder, "forwarder");
    CHECK(kf2::flex::install_offline_lab(
        {owner, state, forwarder, false, true, true, false}).has_value());
    const auto marker = state / "flex-lab-transaction.marker";
    const auto marker_before = read(marker);
    const auto backup = state / "flexRelease_x64.pre-lab.dll";
    // Even identical original DLL hashes do not identify their installation.
    for (const bool same_runtime : {false, true}) {
        write(other / "flexRelease_x64.dll",
              same_runtime ? "forwarder" : "other-runtime");
        write(other / "flexRelease_original.dll", "original-runtime");
        const auto other_before = read(other / "flexRelease_x64.dll");
        for (const bool recover : {false, true}) {
            std::wstring details = L"previous details";
            const auto result = recover
                ? kf2::flex::recover_offline_lab(other, state, false, &details)
                : kf2::flex::restore_offline_lab(other, state, false, &details);
            CHECK(!result.has_value());
            CHECK(details.empty());
            CHECK(read(other / "flexRelease_x64.dll") == other_before);
            CHECK(read(other / "flexRelease_original.dll") == "original-runtime");
            CHECK(read(owner / "flexRelease_x64.dll") == "forwarder");
            CHECK(read(owner / "flexRelease_original.dll") == "original-runtime");
            CHECK(read(marker) == marker_before);
            CHECK(read(backup) == "original-runtime");
        }
    }
    // Missing and old ownerless markers must retain all recovery evidence.
    for (const int schema : {0, 1, 2}) {
        std::string ownerless;
        if (schema == 1) {
            ownerless = "schema=1\noriginal_sha256="
                "aa4b0053991bf30f9c68dc88286c97de92031511ef7a79ae89ea8d7233809b3f\n";
        } else if (schema == 2) {
            ownerless = "schema=2\nstate=installed\noriginal_sha256="
                "aa4b0053991bf30f9c68dc88286c97de92031511ef7a79ae89ea8d7233809b3f\n"
                "forwarder_sha256=" +
                kf2::security::sha256_hex("forwarder").value() + "\n";
        }
        if (schema == 0) CHECK(std::filesystem::remove(marker));
        else write(marker, ownerless.c_str());
        for (const auto& game : {owner, other}) {
            const auto active_before = read(game / "flexRelease_x64.dll");
            for (const bool recover : {false, true}) {
                const auto result = recover
                    ? kf2::flex::recover_offline_lab(game, state, false)
                    : kf2::flex::restore_offline_lab(game, state, false);
                CHECK(!result.has_value());
                CHECK(read(game / "flexRelease_x64.dll") == active_before);
                CHECK(read(game / "flexRelease_original.dll") == "original-runtime");
                CHECK(read(marker) == ownerless);
                CHECK(read(backup) == "original-runtime");
            }
        }
    }
    const std::string malformed_markers[] = {
        marker_before + "owner_sha256=" + std::string(64, '0') + "\n",
        marker_before.substr(0, marker_before.find("owner_sha256=")),
        marker_before.substr(0, marker_before.find("owner_sha256=")) +
            "owner_sha256=" + std::string(64, '0') + "\n",
    };
    for (const auto& malformed : malformed_markers) {
        write(marker, malformed.c_str());
        CHECK(!kf2::flex::restore_offline_lab(owner, state, false).has_value());
        CHECK(!kf2::flex::recover_offline_lab(owner, state, false).has_value());
        CHECK(read(marker) == malformed);
        CHECK(read(owner / "flexRelease_x64.dll") == "forwarder");
        CHECK(read(owner / "flexRelease_original.dll") == "original-runtime");
    }
    write(marker, marker_before.c_str());
    // Path casing and Unicode names retain the same owner.
    auto owner_alias = owner.wstring();
    for (auto& character : owner_alias) {
        if (character >= L'a' && character <= L'z') character -= L'a' - L'A';
    }
    const auto restored = kf2::flex::restore_offline_lab(owner_alias, state, false);
    if (!restored.has_value()) std::wcerr << restored.error().message << '\n';
    CHECK(restored.has_value());
    CHECK(read(owner / "flexRelease_x64.dll") == "original-runtime");
    CHECK(!std::filesystem::exists(owner / "flexRelease_original.dll"));
    CHECK(!std::filesystem::exists(marker));
    CHECK(read(other / "flexRelease_x64.dll") == "forwarder");
    CHECK(read(other / "flexRelease_original.dll") == "original-runtime");
    // Replacing a directory at the same path must not inherit its ownership.
    CHECK(kf2::flex::install_offline_lab(
        {owner, state, forwarder, false, true, true, false}).has_value());
    const auto displaced_owner = root / "displaced-owner";
    const auto replacement_marker = read(marker);
    std::filesystem::rename(owner, displaced_owner);
    std::filesystem::create_directory(owner);
    write(owner / "flexRelease_x64.dll", "original-runtime");
    CHECK(!kf2::flex::restore_offline_lab(owner, state, false).has_value());
    CHECK(!kf2::flex::recover_offline_lab(owner, state, false).has_value());
    CHECK(read(marker) == replacement_marker);
    CHECK(read(owner / "flexRelease_x64.dll") == "original-runtime");
    CHECK(read(displaced_owner / "flexRelease_x64.dll") == "forwarder");
    CHECK(kf2::flex::restore_offline_lab(displaced_owner, state, false).has_value());
    CHECK(read(displaced_owner / "flexRelease_x64.dll") == "original-runtime");
    CHECK(!std::filesystem::exists(marker));
    return 0;
}

static int check_redundant_recovery(const std::filesystem::path& root) {
    struct Case {
        const char* name;
        const char* backup;
        const char* original;
        bool recover;
        bool succeeds;
        bool lock_backup{false};
        bool deny_backup_status{false};
        bool lock_active{false};
        bool remove_marker{false};
    };
    const Case cases[] = {
        {"valid-copies", "original-runtime", "original-runtime", false, true},
        {"missing-backup", nullptr, "original-runtime", false, true},
        {"corrupt-backup", "corrupt", "original-runtime", false, true},
        {"corrupt-original", "original-runtime", "corrupt", false, true},
        {"missing-original", "original-runtime", nullptr, false, true},
        {"both-invalid", "corrupt-backup", "corrupt-original", false, false},
        {"missing-both", nullptr, nullptr, false, false},
        {"missing-backup-corrupt-original", nullptr, "corrupt", false, false},
        {"startup-recovery", "corrupt", "original-runtime", true, true},
        {"startup-both-invalid", "corrupt", "corrupt", true, false},
        {"locked-backup", "original-runtime", "original-runtime", false, true,
         true},
        {"unknown-backup-status", "original-runtime", "original-runtime",
         false, true, false, true},
        {"blocked-replacement", "corrupt", "original-runtime", false, false,
         false, false, true},
        {"markerless-valid", "original-runtime", "original-runtime", false,
         false, false, false, false, true},
        {"markerless-locked-backup", "original-runtime", "original-runtime",
         false, false, true, false, false, true},
    };
    for (const auto& scenario : cases) {
        const auto directory = root / scenario.name;
        const auto game = directory / "game";
        const auto state = directory / "state";
        const auto forwarder = directory / "flexRelease_x64.forwarder-lab.dll";
        const auto backup = state / "flexRelease_x64.pre-lab.dll";
        const auto original = game / "flexRelease_original.dll";
        const auto marker = state / "flex-lab-transaction.marker";
        std::filesystem::create_directories(game);
        write(game / "flexRelease_x64.dll", "original-runtime");
        write(forwarder, "forwarder");
        CHECK(kf2::flex::install_offline_lab(
            {game, state, forwarder, false, true, true, false}).has_value());
        if (scenario.backup != nullptr) write(backup, scenario.backup);
        else CHECK(std::filesystem::remove(backup));
        if (scenario.original != nullptr) write(original, scenario.original);
        else CHECK(std::filesystem::remove(original));
        if (scenario.remove_marker) CHECK(std::filesystem::remove(marker));
        const auto marker_before = read(marker);
        HANDLE locked = INVALID_HANDLE_VALUE;
        if (scenario.lock_backup) {
            locked = CreateFileW(backup.c_str(), GENERIC_READ, 0, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            CHECK(locked != INVALID_HANDLE_VALUE);
        }
        if (scenario.lock_active) {
            locked = CreateFileW((game / "flexRelease_x64.dll").c_str(),
                GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            CHECK(locked != INVALID_HANDLE_VALUE);
        }
        if (scenario.deny_backup_status) {
            denied_status_path = backup;
            kf2::flex::set_lab_status_hook_for_testing(&fail_selected_status);
        }
        std::wstring details = L"Previous recovery details";
        const auto result = scenario.recover
            ? kf2::flex::recover_offline_lab(game, state, false, &details)
            : kf2::flex::restore_offline_lab(game, state, false, &details);
        kf2::flex::set_lab_status_hook_for_testing(nullptr);
        denied_status_path.clear();
        if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);
        CHECK(result.has_value() == scenario.succeeds);
        if (scenario.succeeds) {
            CHECK(result.value());
            CHECK(read(game / "flexRelease_x64.dll") == "original-runtime");
            CHECK(!std::filesystem::exists(marker));
            CHECK(!std::filesystem::exists(original));
            const bool backup_used = scenario.backup != nullptr &&
                std::string{scenario.backup} == "original-runtime" &&
                !scenario.lock_backup && !scenario.deny_backup_status;
            CHECK(details.find(backup_used ? L"verified state backup"
                                          : L"verified in-game original") !=
                  std::wstring::npos);
            if (!backup_used) CHECK(details.find(L"State backup rejected:") !=
                                    std::wstring::npos);
            if (scenario.original == nullptr ||
                std::string{scenario.original} != "original-runtime") {
                CHECK(details.find(L"In-game original rejected:") !=
                      std::wstring::npos);
            }
        } else {
            CHECK(details.empty());
            CHECK(read(game / "flexRelease_x64.dll") == "forwarder");
            CHECK(read(marker) == marker_before);
            CHECK(read(original) == (scenario.original != nullptr
                ? scenario.original : ""));
        }
        CHECK(read(backup) == (scenario.backup != nullptr
            ? scenario.backup : ""));
    }
    return 0;
}

int main() {
    const auto root = std::filesystem::path{KF2_TEST_ROOT};
    std::error_code ec; std::filesystem::remove_all(root, ec);
    CHECK(check_installation_ownership(root / "ownership") == 0);
    const auto game = root / "game"; const auto state = root / "state";
    std::filesystem::create_directories(game); std::filesystem::create_directories(state);
    const auto forwarder = root / "flexRelease_x64.forwarder-lab.dll";
    write(game / "flexRelease_x64.dll", "original-runtime"); write(forwarder, "forwarder");
    denied_status_path = state / "flex-lab-transaction.marker";
    kf2::flex::set_lab_status_hook_for_testing(&fail_selected_status);
    const auto inaccessible = kf2::flex::recover_offline_lab(
        game, state, false);
    kf2::flex::set_lab_status_hook_for_testing(nullptr);
    denied_status_path.clear();
    CHECK(!inaccessible.has_value());
    CHECK(inaccessible.error().code == kf2::ErrorCode::io_failure);
    CHECK(inaccessible.error().native_code != 0);
    kf2::flex::LabTransactionOptions o{game, state, forwarder, false, true, true, false};
    auto installed = kf2::flex::install_offline_lab(o);
    CHECK(installed.has_value() && installed.value().installed);
    CHECK(read(game / "flexRelease_x64.dll") == "forwarder");
    CHECK(read(game / "flexRelease_original.dll") == "original-runtime");
    CHECK(read(state / "flexRelease_x64.pre-lab.dll") == "original-runtime");
    const auto installed_marker = read(state / "flex-lab-transaction.marker");
    CHECK(installed_marker.find(installed.value().original_sha256) !=
          std::string::npos);
    CHECK(installed_marker.find(installed.value().forwarder_sha256) !=
          std::string::npos);
    auto retained = kf2::flex::recover_offline_lab(game, state, false);
    CHECK(retained.has_value() && !retained.value());
    CHECK(read(game / "flexRelease_x64.dll") == "forwarder");
    std::wstring blocked_details = L"Previous recovery details";
    CHECK(!kf2::flex::restore_offline_lab(
        game, state, true, &blocked_details).has_value());
    CHECK(blocked_details.empty());
    CHECK(read(state / "flex-lab-transaction.marker") == installed_marker);
    CHECK(kf2::flex::restore_offline_lab(game, state, false).has_value());
    write(state / "flex-lab-transaction.marker",
          "schema=1\noriginal_sha256="
          "aa4b0053991bf30f9c68dc88286c97de92031511ef7a79ae89ea8d7233809b3f\n");
    auto legacy_cleaned = kf2::flex::recover_offline_lab(game, state, false);
    CHECK(!legacy_cleaned.has_value());
    CHECK(std::filesystem::exists(state / "flex-lab-transaction.marker"));
    CHECK(read(game / "flexRelease_x64.dll") == "original-runtime");
    CHECK(!std::filesystem::exists(game / "flexRelease_original.dll"));
    CHECK(!kf2::flex::restore_offline_lab(game, state, false).has_value());
    CHECK(std::filesystem::remove(state / "flex-lab-transaction.marker"));
    o.simulate_failure_after_install = true;
    CHECK(!kf2::flex::install_offline_lab(o).has_value());
    CHECK(read(game / "flexRelease_x64.dll") == "original-runtime");
    o.simulate_failure_after_install = false; o.game_running = true;
    CHECK(!kf2::flex::install_offline_lab(o).has_value());
    o.game_running = false; o.offline_confirmed = false;
    CHECK(!kf2::flex::install_offline_lab(o).has_value());
    o.offline_confirmed = true;
    CHECK(kf2::flex::install_offline_lab(o).has_value());
    write(state / "flexRelease_x64.pre-lab.dll", "tampered");
    CHECK(kf2::flex::restore_offline_lab(game, state, false).has_value());
    CHECK(read(game / "flexRelease_x64.dll") == "original-runtime");
    CHECK(kf2::flex::install_offline_lab(o).has_value());
    auto still_installed = kf2::flex::recover_offline_lab(game, state, false);
    CHECK(still_installed.has_value() && !still_installed.value());
    CHECK(read(game / "flexRelease_x64.dll") == "forwarder");

    const auto active_race_root = root / "active-race";
    const auto active_race_game = active_race_root / "game";
    const auto active_race_state = active_race_root / "state";
    const auto active_race_forwarder =
        active_race_root / "flexRelease_x64.forwarder-lab.dll";
    std::filesystem::create_directories(active_race_game);
    std::filesystem::create_directories(active_race_state);
    write(active_race_game / "flexRelease_x64.dll", "original-runtime");
    write(active_race_forwarder, "forwarder");
    race_active = active_race_game / "flexRelease_x64.dll";
    kf2::flex::set_lab_install_test_hook(replace_source_at_checkpoint);
    const auto active_race = kf2::flex::install_offline_lab(
        {active_race_game, active_race_state, active_race_forwarder,
         false, true, true, false});
    kf2::flex::set_lab_install_test_hook(nullptr);
    race_active.clear();
    CHECK(!active_race.has_value());
    CHECK(read(active_race_game / "flexRelease_x64.dll") ==
          "replacement-runtime");
    CHECK(read(active_race_state / "flexRelease_x64.pre-lab.dll") ==
          "replacement-runtime");
    CHECK(!std::filesystem::exists(
        active_race_state / "flex-lab-transaction.marker"));
    CHECK(!std::filesystem::exists(
        active_race_game / "flexRelease_original.dll"));

    const auto forwarder_race_root = root / "forwarder-race";
    const auto forwarder_race_game = forwarder_race_root / "game";
    const auto forwarder_race_state = forwarder_race_root / "state";
    const auto forwarder_race_source =
        forwarder_race_root / "flexRelease_x64.forwarder-lab.dll";
    std::filesystem::create_directories(forwarder_race_game);
    std::filesystem::create_directories(forwarder_race_state);
    write(forwarder_race_game / "flexRelease_x64.dll", "original-runtime");
    write(forwarder_race_source, "forwarder");
    race_forwarder = forwarder_race_source;
    kf2::flex::set_lab_install_test_hook(replace_source_at_checkpoint);
    const auto forwarder_race = kf2::flex::install_offline_lab(
        {forwarder_race_game, forwarder_race_state, forwarder_race_source,
         false, true, true, false});
    kf2::flex::set_lab_install_test_hook(nullptr);
    race_forwarder.clear();
    CHECK(!forwarder_race.has_value());
    CHECK(read(forwarder_race_game / "flexRelease_x64.dll") ==
          "original-runtime");
    CHECK(read(forwarder_race_state / "flexRelease_x64.pre-lab.dll") ==
          "original-runtime");
    CHECK(!std::filesystem::exists(
        forwarder_race_state / "flex-lab-transaction.marker"));
    CHECK(!std::filesystem::exists(
        forwarder_race_game / "flexRelease_original.dll"));

    const auto hash_race_root = root / "hash-race";
    const auto hash_race_input = hash_race_root / "flexRelease_x64.dll";
    replacement_path = hash_race_root / "replacement.dll";
    std::filesystem::create_directories(hash_race_root);
    write(hash_race_input, "original-runtime");
    write(replacement_path, "replacement-runtime");
    mutation_succeeded = false;
    kf2::security::set_sha256_file_read_hook_for_testing(
        &replace_during_read);
    const auto replaced_hash = kf2::security::sha256_file_hex(
        hash_race_input, 16U * 1024U * 1024U);
    kf2::security::set_sha256_file_read_hook_for_testing(nullptr);
    CHECK(mutation_succeeded);
    CHECK(!replaced_hash.has_value());
    CHECK(replaced_hash.error().code == kf2::ErrorCode::stale_data);

    const auto oversized_root = root / "oversized-runtime";
    const auto oversized_game = oversized_root / "game";
    const auto oversized_state = oversized_root / "state";
    const auto oversized_forwarder =
        oversized_root / "flexRelease_x64.forwarder-lab.dll";
    std::filesystem::create_directories(oversized_game);
    std::filesystem::create_directories(oversized_state);
    write(oversized_game / "flexRelease_x64.dll", "runtime");
    std::filesystem::resize_file(
        oversized_game / "flexRelease_x64.dll", 16U * 1024U * 1024U + 1U);
    write(oversized_forwarder, "forwarder");
    const auto oversized_install = kf2::flex::install_offline_lab(
        {oversized_game, oversized_state, oversized_forwarder,
         false, true, true, false});
    CHECK(!oversized_install.has_value());
    CHECK(oversized_install.error().code == kf2::ErrorCode::access_denied);
    CHECK(!std::filesystem::exists(
        oversized_state / "flex-lab-transaction.marker"));

    const auto marker_root = root / "oversized-marker";
    const auto marker_game = marker_root / "game";
    const auto marker_state = marker_root / "state";
    const auto marker_forwarder =
        marker_root / "flexRelease_x64.forwarder-lab.dll";
    std::filesystem::create_directories(marker_game);
    std::filesystem::create_directories(marker_state);
    write(marker_game / "flexRelease_x64.dll", "original-runtime");
    write(marker_forwarder, "forwarder");
    CHECK(kf2::flex::install_offline_lab(
        {marker_game, marker_state, marker_forwarder,
         false, true, true, false}).has_value());
    write(marker_state / "flex-lab-transaction.marker", "x");
    std::filesystem::resize_file(
        marker_state / "flex-lab-transaction.marker", 4U * 1024U + 1U);
    const auto oversized_marker = kf2::flex::restore_offline_lab(
        marker_game, marker_state, false);
    CHECK(!oversized_marker.has_value());
    CHECK(oversized_marker.error().code == kf2::ErrorCode::access_denied);
    CHECK(read(marker_game / "flexRelease_x64.dll") == "forwarder");

    const auto linked_marker_root = root / "linked-marker";
    const auto linked_marker_game = linked_marker_root / "game";
    const auto linked_marker_state = linked_marker_root / "state";
    const auto linked_marker_forwarder =
        linked_marker_root / "flexRelease_x64.forwarder-lab.dll";
    std::filesystem::create_directories(linked_marker_game);
    std::filesystem::create_directories(linked_marker_state);
    write(linked_marker_game / "flexRelease_x64.dll", "original-runtime");
    write(linked_marker_forwarder, "forwarder");
    CHECK(kf2::flex::install_offline_lab(
        {linked_marker_game, linked_marker_state, linked_marker_forwarder,
         false, true, true, false}).has_value());
    const auto linked_marker =
        linked_marker_state / "flex-lab-transaction.marker";
    const auto marker_alias = linked_marker_state / "marker-alias";
    CHECK(CreateHardLinkW(marker_alias.c_str(), linked_marker.c_str(), nullptr));
    const auto unsafe_marker = kf2::flex::restore_offline_lab(
        linked_marker_game, linked_marker_state, false);
    CHECK(!unsafe_marker.has_value());
    CHECK(unsafe_marker.error().code == kf2::ErrorCode::access_denied);
    CHECK(read(linked_marker_game / "flexRelease_x64.dll") == "forwarder");

    const auto marker_race_root = root / "marker-race";
    const auto marker_race_game = marker_race_root / "game";
    const auto marker_race_state = marker_race_root / "state";
    const auto marker_race_forwarder =
        marker_race_root / "flexRelease_x64.forwarder-lab.dll";
    std::filesystem::create_directories(marker_race_game);
    std::filesystem::create_directories(marker_race_state);
    write(marker_race_game / "flexRelease_x64.dll", "original-runtime");
    write(marker_race_forwarder, "forwarder");
    CHECK(kf2::flex::install_offline_lab(
        {marker_race_game, marker_race_state, marker_race_forwarder,
         false, true, true, false}).has_value());
    const auto marker_race_path =
        marker_race_state / "flex-lab-transaction.marker";
    replacement_path = marker_race_state / "replacement.marker";
    const auto marker_race_bytes = read(marker_race_path);
    write(replacement_path, marker_race_bytes.c_str());
    mutation_succeeded = false;
    kf2::platform::windows::set_bounded_read_hook_for_testing(
        &replace_during_read);
    const auto replaced_marker = kf2::flex::restore_offline_lab(
        marker_race_game, marker_race_state, false);
    kf2::platform::windows::set_bounded_read_hook_for_testing(nullptr);
    CHECK(mutation_succeeded);
    CHECK(!replaced_marker.has_value());
    CHECK(replaced_marker.error().code == kf2::ErrorCode::stale_data);
    CHECK(read(marker_race_game / "flexRelease_x64.dll") == "forwarder");
    CHECK(check_redundant_recovery(root / "redundant-recovery") == 0);
    return 0;
}
