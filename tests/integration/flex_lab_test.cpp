#include "kf2/flex/flex_lab.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

#define CHECK(x) do { if (!(x)) { std::cerr << "check failed line " << __LINE__ << '\n'; return 1; } } while (0)

static std::string read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary); return {std::istreambuf_iterator<char>{f}, {}};
}
static void write(const std::filesystem::path& p, const char* value) {
    std::ofstream f(p, std::ios::binary); f << value;
}

static std::filesystem::path race_active;
static std::filesystem::path race_forwarder;
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
    return 0;
}
