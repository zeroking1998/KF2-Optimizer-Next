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

int main() {
    const auto root = std::filesystem::path{KF2_TEST_ROOT};
    std::error_code ec; std::filesystem::remove_all(root, ec);
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
    CHECK(kf2::flex::restore_offline_lab(game, state, false).has_value());
    write(state / "flex-lab-transaction.marker",
          "schema=1\noriginal_sha256="
          "aa4b0053991bf30f9c68dc88286c97de92031511ef7a79ae89ea8d7233809b3f\n");
    auto legacy_cleaned = kf2::flex::recover_offline_lab(game, state, false);
    CHECK(legacy_cleaned.has_value() && legacy_cleaned.value());
    CHECK(!std::filesystem::exists(state / "flex-lab-transaction.marker"));
    CHECK(read(game / "flexRelease_x64.dll") == "original-runtime");
    CHECK(!std::filesystem::exists(game / "flexRelease_original.dll"));
    CHECK(!kf2::flex::restore_offline_lab(game, state, false).has_value());
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
    CHECK(!kf2::flex::restore_offline_lab(game, state, false).has_value());
    write(state / "flexRelease_x64.pre-lab.dll", "original-runtime");
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
    return 0;
}
