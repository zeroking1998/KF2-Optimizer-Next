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
    return 0;
}
