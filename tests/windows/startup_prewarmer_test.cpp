#include "kf2/game/startup_prewarmer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "FAIL line " << __LINE__ << ": " #condition "\n"; ++failures; \
} } while (false)

void write_file(const std::filesystem::path& path, std::size_t bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    const std::string data(bytes, 'x');
    output.write(data.data(), static_cast<std::streamsize>(data.size()));
}

void write_sparse_file(const std::filesystem::path& path,
                       std::uintmax_t bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.close();
    std::filesystem::resize_file(path, bytes);
}
}  // namespace

int main(int argc, char** argv) {
    using namespace kf2::game;
    const std::string map_cycles =
        "[KFGame.KFGameInfo]\r\n"
        "bUseMapList=True\r\n"
        "ActiveMapCycle=1\r\n"
        "GameMapCycles=(Maps=(\"KF-Airship\",\"KF-BioticsLab\"))\r\n"
        "GameMapCycles=(Maps=(\"KF-Outpost\",\"KF-Prison\",\"KF-Rig\"))\r\n";
    CHECK(next_map_from_game_config(map_cycles, L"KF-Prison") ==
          std::optional<std::wstring>{L"KF-Rig"});
    CHECK(next_map_from_game_config(map_cycles, L"KF-Rig") ==
          std::optional<std::wstring>{L"KF-Outpost"});
    CHECK(!next_map_from_game_config(map_cycles, L"KF-Unknown"));
    CHECK(!next_map_from_game_config(
        "[KFGame.KFGameInfo]\nbUseMapList=False\nActiveMapCycle=0\n"
        "GameMapCycles=(Maps=(\"KF-Airship\",\"KF-Rig\"))\n",
        L"KF-Airship"));
    CHECK(!next_map_from_game_config(
        "[KFGame.KFGameInfo]\nbUseMapList=True\nActiveMapCycle=4\n"
        "GameMapCycles=(Maps=(\"KF-Airship\",\"KF-Rig\"))\n",
        L"KF-Airship"));
    constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t mib = 1024ULL * 1024ULL;
    CHECK(startup_prewarm_budget(StorageKind::rotational, 2 * gib) == 0);
    CHECK(startup_prewarm_budget(StorageKind::rotational, 3 * gib) ==
          256 * mib);
    CHECK(startup_prewarm_budget(StorageKind::solid_state, 32 * gib) ==
          2 * gib);
    CHECK(startup_prewarm_budget(StorageKind::rotational, 32 * gib) ==
          4 * gib);
    CHECK(startup_prewarm_file_budget(16 * mib) == 16 * mib);
    CHECK(startup_prewarm_file_budget(1024 * mib) == 512 * mib);
    CHECK(startup_prewarm_budget(StorageKind::unknown, 32 * gib) == 0);

    const auto root = std::filesystem::temp_directory_path() /
        L"kf2-startup-prewarmer-test";
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);
    write_file(root / L"KFGame/BrewedPC/GlobalShaderCache-PC-D3D-SM5.bin",
               1024);
    write_file(root / L"KFGame/BrewedPC/Engine.u", 2048);
    CHECK(build_startup_prewarm_plan(
        root, StorageKind::solid_state, 4 * gib).size() == 2);
    CHECK(build_startup_prewarm_plan(
        root, StorageKind::unknown, 4 * gib).empty());
    CHECK(build_startup_prewarm_plan(
        root, StorageKind::rotational, 2 * gib).empty());
    const auto plan = build_startup_prewarm_plan(
        root, StorageKind::rotational, 4 * gib);
    CHECK(plan.size() == 2);
    CHECK(plan[0].bytes == 1024);
    CHECK(plan[1].bytes == 2048);

    const auto fair_root = std::filesystem::temp_directory_path() /
        L"kf2-startup-prewarmer-fair-plan-test";
    std::filesystem::remove_all(fair_root, cleanup_error);
    write_sparse_file(
        fair_root / L"KFGame/BrewedPC/EngineDebugMaterials.upk", 48 * mib);
    write_sparse_file(
        fair_root / L"KFGame/BrewedPC/RefShaderCache-PC-D3D-SM5.upk",
        96 * mib);
    write_sparse_file(fair_root / L"KFGame/Movies/MenuBG.bik", 96 * mib);
    write_file(fair_root / L"KFGame/BrewedPC/Maps/KFMainMenu.kfm", 4096);
    const auto fair_plan = build_startup_prewarm_plan(
        fair_root, StorageKind::solid_state, 4 * gib);
    CHECK(fair_plan.size() == 4);
    if (fair_plan.size() == 4) {
        CHECK(fair_plan[0].path.filename() == L"KFMainMenu.kfm");
        CHECK(fair_plan[0].bytes == 4096);
        CHECK(fair_plan[1].bytes == 48 * mib);
        CHECK(fair_plan[2].bytes == 96 * mib);
        CHECK(fair_plan[3].bytes == 96 * mib);
    }
    const auto rotational_plan = build_startup_prewarm_plan(
        fair_root, StorageKind::rotational, 4 * gib);
    CHECK(rotational_plan.size() == 4);
    if (rotational_plan.size() == 4) {
        CHECK(rotational_plan[0].path.filename() == L"KFMainMenu.kfm");
        CHECK(rotational_plan[0].bytes == 4096);
        CHECK(rotational_plan[1].bytes == 48 * mib);
        CHECK(rotational_plan[2].bytes == 96 * mib);
        CHECK(rotational_plan[3].bytes == 96 * mib);
    }

    const auto map_root = fair_root /
        L"KFGame/BrewedPC/Maps/BioticsLab";
    write_file(map_root / L"SND_BioticsLab.kfm", 1024);
    write_file(map_root / L"LIGHTS_BioticsLab.kfm", 2048);
    write_sparse_file(map_root / L"KF-BioticsLab.kfm", 80 * mib);
    const auto map_plan = build_startup_prewarm_plan(
        fair_root, StorageKind::solid_state, 8 * gib, L"KF-BioticsLab");
    CHECK(map_plan.size() == 7);
    if (map_plan.size() == 7) {
        CHECK(map_plan[0].path.filename() == L"LIGHTS_BioticsLab.kfm");
        CHECK(map_plan[1].path.filename() == L"SND_BioticsLab.kfm");
        CHECK(map_plan[2].path.filename() == L"KFMainMenu.kfm");
        CHECK(map_plan[3].path.filename() == L"KF-BioticsLab.kfm");
        CHECK(map_plan[3].bytes == 80 * mib);
        CHECK(map_plan[4].path.filename() == L"EngineDebugMaterials.upk");
    }
    const auto extension_plan = build_startup_prewarm_plan(
        fair_root, StorageKind::solid_state, 8 * gib, L"KF-BioticsLab.kfm");
    CHECK(extension_plan.size() == map_plan.size());
    const auto map_only_plan = build_startup_prewarm_plan(
        fair_root, StorageKind::solid_state, 8 * gib, L"KF-BioticsLab",
        false);
    CHECK(map_only_plan.size() == 3);
    CHECK(std::ranges::all_of(
        map_only_plan, [&map_root](const StartupPrewarmFile& file) {
            return file.path.parent_path() == map_root;
        }));
    const auto constrained_map_plan = build_startup_prewarm_plan(
        fair_root, StorageKind::solid_state, 3 * gib, L"KF-BioticsLab");
    const auto selected_map = std::find_if(
        constrained_map_plan.begin(), constrained_map_plan.end(),
        [](const StartupPrewarmFile& file) {
            return file.path.filename() == L"KF-BioticsLab.kfm";
        });
    CHECK(selected_map != constrained_map_plan.end());
    if (selected_map != constrained_map_plan.end()) {
        CHECK(selected_map->bytes == 80 * mib);
    }
    CHECK(build_startup_prewarm_plan(
        fair_root, StorageKind::solid_state, 8 * gib,
        L"../KF-BioticsLab").size() == fair_plan.size());

    const auto duplicate_map_root = fair_root /
        L"KFGame/BrewedPC/Maps/WorkshopCopy";
    write_file(duplicate_map_root / L"KF-BioticsLab.kfm", 4096);
    CHECK(build_startup_prewarm_plan(
        fair_root, StorageKind::solid_state, 8 * gib,
        L"KF-BioticsLab").size() == fair_plan.size());

    const auto before = std::filesystem::last_write_time(plan[0].path);
    StartupPrewarmer prewarmer;
    prewarmer.start(root, {
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::rotational,
        .available_memory_override = 4 * gib,
    });
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (prewarmer.snapshot().state == StartupPrewarmState::complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    const auto complete = prewarmer.snapshot();
    CHECK(complete.state == StartupPrewarmState::complete);
    CHECK(complete.bytes_planned == 3072);
    CHECK(complete.bytes_read == 3072);
    CHECK(complete.files_read == 2);
    CHECK(std::filesystem::last_write_time(plan[0].path) == before);

    StartupPrewarmer solid_state;
    solid_state.start(root, {
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::solid_state,
        .available_memory_override = 4 * gib,
    });
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (solid_state.snapshot().state ==
            StartupPrewarmState::complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    CHECK(solid_state.snapshot().state == StartupPrewarmState::complete);
    CHECK(solid_state.snapshot().bytes_read == 3072);

    StartupPrewarmer cancelled;
    cancelled.start(root, {
        .idle_delay = std::chrono::seconds{5},
        .storage_override = StorageKind::rotational,
        .available_memory_override = 4 * gib,
    });
    cancelled.request_stop();
    cancelled.stop_and_wait();
    CHECK(cancelled.snapshot().state == StartupPrewarmState::cancelled);
    CHECK(cancelled.snapshot().bytes_read == 0);

    if (argc > 1) {
        const std::filesystem::path real_root{argv[1]};
        const auto kind = storage_kind_for_path(real_root);
        std::cout << "STORAGE_KIND=" << static_cast<int>(kind) << '\n';
        StartupPrewarmer real;
        const auto started = std::chrono::steady_clock::now();
        StartupPrewarmOptions real_options{
            .idle_delay = std::chrono::milliseconds{0}};
        if (argc > 2) {
            const std::filesystem::path map_argument{argv[2]};
            real_options.map_name = map_argument.wstring();
        }
        real.start(real_root, std::move(real_options));
        for (int attempt = 0; attempt < 2000; ++attempt) {
            const auto state = real.snapshot().state;
            if (state == StartupPrewarmState::complete ||
                state == StartupPrewarmState::skipped_unknown_storage ||
                state == StartupPrewarmState::skipped_low_memory ||
                state == StartupPrewarmState::skipped_no_files) break;
            std::this_thread::sleep_for(std::chrono::milliseconds{5});
        }
        const auto measured = real.snapshot();
        const auto elapsed = std::chrono::duration_cast<
            std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
        std::cout << "PREWARM_STATE=" << static_cast<int>(measured.state)
                  << " BYTES=" << measured.bytes_read
                  << " FILES=" << measured.files_read
                  << " ELAPSED_MS=" << elapsed << '\n';
    }

    std::filesystem::remove_all(root, cleanup_error);
    std::filesystem::remove_all(fair_root, cleanup_error);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
