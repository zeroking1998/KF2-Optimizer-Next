#include "kf2/game/startup_prewarmer.hpp"

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
}  // namespace

int main(int argc, char** argv) {
    using namespace kf2::game;
    constexpr std::uint64_t gib = 1024ULL * 1024ULL * 1024ULL;
    constexpr std::uint64_t mib = 1024ULL * 1024ULL;
    CHECK(startup_prewarm_budget(StorageKind::rotational, 2 * gib) == 0);
    CHECK(startup_prewarm_budget(StorageKind::rotational, 3 * gib) ==
          256 * mib);
    CHECK(startup_prewarm_budget(StorageKind::solid_state, 32 * gib) ==
          128 * mib);
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
        real.start(real_root, {.idle_delay = std::chrono::milliseconds{0}});
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
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
