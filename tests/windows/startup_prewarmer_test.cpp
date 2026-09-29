#include "kf2/game/startup_prewarmer.hpp"
#include "kf2/platform/windows/state_environment.hpp"

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <system_error>
#include <thread>
#include <utility>

namespace {
static_assert(!noexcept(
    std::declval<kf2::game::StartupPrewarmer&>().stop_and_wait()));

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
    HANDLE output = CreateFileW(
        path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        throw std::system_error(
            static_cast<int>(GetLastError()), std::system_category());
    }
    DWORD returned = 0;
    FILE_SET_SPARSE_BUFFER sparse{TRUE};
    LARGE_INTEGER size{};
    size.QuadPart = static_cast<LONGLONG>(bytes);
    const bool configured = DeviceIoControl(
        output, FSCTL_SET_SPARSE, &sparse, sizeof(sparse), nullptr, 0,
        &returned, nullptr) != FALSE;
    const bool resized = configured &&
        SetFilePointerEx(output, size, nullptr, FILE_BEGIN) != FALSE &&
        SetEndOfFile(output) != FALSE;
    const DWORD error = resized ? ERROR_SUCCESS : GetLastError();
    CloseHandle(output);
    if (!resized) {
        throw std::system_error(
            static_cast<int>(error), std::system_category());
    }
}

kf2::game::StartupPrewarmSnapshot wait_for_terminal(
    kf2::game::StartupPrewarmer& prewarmer) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        const auto current = prewarmer.snapshot();
        if (current.state == kf2::game::StartupPrewarmState::complete ||
            current.state == kf2::game::StartupPrewarmState::cancelled ||
            kf2::game::startup_prewarm_retryable(current.state)) {
            return current;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return prewarmer.snapshot();
}
}  // namespace

int main(int argc, char** argv) {
    using namespace kf2::game;
    constexpr DWORD extent_count = 300;
    constexpr std::size_t extent_bytes =
        offsetof(VOLUME_DISK_EXTENTS, Extents) +
        static_cast<std::size_t>(extent_count) * sizeof(DISK_EXTENT);
    static_assert(extent_bytes > detail::kInitialVolumeExtentBufferBytes);
    std::vector<std::byte> extent_storage(extent_bytes);
    VOLUME_DISK_EXTENTS extent_header{};
    extent_header.NumberOfDiskExtents = extent_count;
    std::memcpy(extent_storage.data(), &extent_header,
                offsetof(VOLUME_DISK_EXTENTS, Extents));
    for (DWORD index = 0; index < extent_count; ++index) {
        DISK_EXTENT extent{};
        extent.DiskNumber = index % 3;
        std::memcpy(
            extent_storage.data() + offsetof(VOLUME_DISK_EXTENTS, Extents) +
                static_cast<std::size_t>(index) * sizeof(DISK_EXTENT),
            &extent, sizeof(extent));
    }
    const auto parsed_extents = detail::parse_volume_disk_extents(
        extent_storage, extent_storage.size());
    const std::optional<std::vector<std::uint32_t>> expected_disks{
        std::vector<std::uint32_t>{0, 1, 2}};
    CHECK(parsed_extents == expected_disks);
    CHECK(!detail::parse_volume_disk_extents(
        extent_storage, extent_storage.size() - 1));
    CHECK(!detail::parse_volume_disk_extents(
        extent_storage, extent_storage.size() + 1));

    std::vector<std::byte> single_extent_storage(
        offsetof(VOLUME_DISK_EXTENTS, Extents) + sizeof(DISK_EXTENT));
    VOLUME_DISK_EXTENTS single_extent_header{};
    single_extent_header.NumberOfDiskExtents = 1;
    std::memcpy(single_extent_storage.data(), &single_extent_header,
                offsetof(VOLUME_DISK_EXTENTS, Extents));
    DISK_EXTENT single_extent{};
    single_extent.DiskNumber = 7;
    std::memcpy(single_extent_storage.data() +
                    offsetof(VOLUME_DISK_EXTENTS, Extents),
                &single_extent, sizeof(single_extent));
    const std::optional<std::vector<std::uint32_t>> expected_single_disk{
        std::vector<std::uint32_t>{7}};
    CHECK(detail::parse_volume_disk_extents(
              single_extent_storage, single_extent_storage.size()) ==
          expected_single_disk);

    DWORD zero_extents = 0;
    std::memcpy(single_extent_storage.data(), &zero_extents,
                sizeof(zero_extents));
    CHECK(!detail::parse_volume_disk_extents(
        single_extent_storage, single_extent_storage.size()));

    CHECK(map_prewarm_request_from_log_line(
        "[12.3] ScriptLog: KF2OPT_MAP_SELECTION schema=1 state=menu "
        "map=KF-BurningParis\r") ==
        std::optional<std::wstring>{L"KF-BurningParis"});
    CHECK(map_prewarm_request_from_log_line(
        "ScriptLog: KF2OPT_MAP_SELECTION schema=1 state=vote "
        "map=KF-CastleVolter") ==
        std::optional<std::wstring>{L"KF-CastleVolter"});
    CHECK(!map_prewarm_request_from_log_line(
        "Log: LoadMap: KF-CastleVolter"));
    CHECK(map_prewarm_request_from_log_line(
        "ScriptLog: KF2OPT_MAP_SELECTION schema=1 state=vote "
        "map=KF-CastleVolter") ==
        std::optional<std::wstring>{L"KF-CastleVolter"});
    CHECK(!map_prewarm_request_from_log_line(
        "KF2OPT_MAP_SELECTION schema=1 state=menu map=../unsafe"));
    CHECK(!map_prewarm_request_from_log_line(
        "KF2OPT_MAP_SELECTION schema=2 state=menu map=KF-Airship"));
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
    CHECK(startup_prewarm_retryable(
        StartupPrewarmState::skipped_unknown_storage));
    CHECK(startup_prewarm_retryable(
        StartupPrewarmState::skipped_low_memory));
    CHECK(startup_prewarm_retryable(
        StartupPrewarmState::skipped_no_files));
    CHECK(startup_prewarm_retryable(StartupPrewarmState::failed));
    CHECK(!startup_prewarm_retryable(StartupPrewarmState::idle));
    CHECK(!startup_prewarm_retryable(StartupPrewarmState::complete));
    CHECK(!startup_prewarm_retryable(StartupPrewarmState::cancelled));

    const auto process_suffix = std::to_wstring(GetCurrentProcessId());
    const auto root = std::filesystem::temp_directory_path() /
        (L"kf2-startup-prewarmer-test-" + process_suffix);
    std::error_code cleanup_error;
    std::filesystem::remove_all(root, cleanup_error);

    auto long_root = std::filesystem::temp_directory_path() /
        (L"kf2-startup-prewarmer-long-path-" + process_suffix);
    while (long_root.wstring().size() < MAX_PATH + 32) {
        long_root /= L"long-path-segment";
    }
    const auto native_long_root =
        kf2::platform::windows::extended_length_path(long_root);
    std::filesystem::create_directories(native_long_root, cleanup_error);
    CHECK(!cleanup_error);
    CHECK(storage_kind_for_path(long_root) ==
          storage_kind_for_path(std::filesystem::temp_directory_path()));
    std::filesystem::remove_all(native_long_root, cleanup_error);
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
    if (plan.size() != 2) {
        std::filesystem::remove_all(root, cleanup_error);
        return EXIT_FAILURE;
    }
    CHECK(plan[0].bytes == 1024);
    CHECK(plan[1].bytes == 2048);

    const auto fair_root = std::filesystem::temp_directory_path() /
        (L"kf2-startup-prewarmer-fair-plan-test-" + process_suffix);
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

    const auto cancellation_root = std::filesystem::temp_directory_path() /
        (L"kf2-map-prewarm-cancellation-test-" + process_suffix);
    std::filesystem::remove_all(cancellation_root, cleanup_error);
    write_file(cancellation_root /
        L"KFGame/BrewedPC/Maps/Cancel/KF-Cancel.kfm", 4096);
    detail::set_startup_prewarm_discovery_delay_for_testing(
        std::chrono::milliseconds{750});
    StartupPrewarmer discovery_cancelled;
    discovery_cancelled.start(cancellation_root, {
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::solid_state,
        .available_memory_override = 4 * gib,
        .map_name = L"KF-Cancel",
        .include_common_startup_files = false,
    });
    for (int attempt = 0; attempt < 200 &&
         detail::startup_prewarm_discovery_steps_for_testing() == 0;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    CHECK(detail::startup_prewarm_discovery_steps_for_testing() > 0);
    const auto cancel_started = std::chrono::steady_clock::now();
    discovery_cancelled.request_stop();
    discovery_cancelled.stop_and_wait();
    const auto cancel_elapsed = std::chrono::duration_cast<
        std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - cancel_started);
    detail::set_startup_prewarm_discovery_delay_for_testing(
        std::chrono::milliseconds{0});
    CHECK(cancel_elapsed < std::chrono::milliseconds{250});
    CHECK(discovery_cancelled.snapshot().state ==
          StartupPrewarmState::cancelled);

    detail::set_startup_prewarm_discovery_delay_for_testing(
        std::chrono::milliseconds{750});
    StartupPrewarmer replaced_discovery;
    replaced_discovery.start(cancellation_root, {
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::solid_state,
        .available_memory_override = 4 * gib,
        .map_name = L"KF-Cancel",
        .include_common_startup_files = false,
    });
    for (int attempt = 0; attempt < 200 &&
         detail::startup_prewarm_discovery_steps_for_testing() == 0;
         ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    CHECK(detail::startup_prewarm_discovery_steps_for_testing() > 0);
    const auto replacement_started = std::chrono::steady_clock::now();
    replaced_discovery.start(root, {
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::unknown,
        .available_memory_override = 4 * gib,
    });
    const auto replacement_elapsed = std::chrono::duration_cast<
        std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - replacement_started);
    detail::set_startup_prewarm_discovery_delay_for_testing(
        std::chrono::milliseconds{0});
    CHECK(replacement_elapsed < std::chrono::milliseconds{250});
    CHECK(wait_for_terminal(replaced_discovery).state ==
          StartupPrewarmState::skipped_unknown_storage);

    const auto before = std::filesystem::last_write_time(plan[0].path);
    StartupPrewarmer prewarmer;
    prewarmer.start(root, {
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::rotational,
        .available_memory_override = 4 * gib,
        .collect_diagnostics = true,
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
    CHECK(complete.diagnostics.has_value());
    if (complete.diagnostics) {
        CHECK(complete.diagnostics->storage == StorageKind::rotational);
        CHECK(complete.diagnostics->files_planned == 2);
        CHECK(complete.diagnostics->files_attempted == 2);
        CHECK(complete.diagnostics->file_open_failures == 0);
        CHECK(complete.diagnostics->file_read_failures == 0);
    }
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
    CHECK(!solid_state.snapshot().diagnostics.has_value());

    StartupPrewarmer failed_then_recovered;
    detail::fail_next_startup_prewarm_plan();
    failed_then_recovered.start(root, {
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::solid_state,
        .available_memory_override = 4 * gib,
    });
    CHECK(wait_for_terminal(failed_then_recovered).state ==
          StartupPrewarmState::failed);
    failed_then_recovered.start(root, {
        .idle_delay = std::chrono::milliseconds{0},
        .storage_override = StorageKind::solid_state,
        .available_memory_override = 4 * gib,
    });
    CHECK(wait_for_terminal(failed_then_recovered).state ==
          StartupPrewarmState::complete);

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

    const auto retry_root = std::filesystem::temp_directory_path() /
        (L"kf2-map-prewarm-retry-test-" + process_suffix);
    std::filesystem::remove_all(retry_root, cleanup_error);
    StartupPrewarmer retry;
    const auto retry_options = [](StorageKind storage,
                                  std::uint64_t memory) {
        return StartupPrewarmOptions{
            .idle_delay = std::chrono::milliseconds{0},
            .storage_override = storage,
            .available_memory_override = memory,
            .map_name = L"KF-Retry",
            .include_common_startup_files = false,
        };
    };
    retry.start(retry_root, retry_options(StorageKind::solid_state, 4 * gib));
    CHECK(wait_for_terminal(retry).state ==
          StartupPrewarmState::skipped_no_files);
    write_file(retry_root /
        L"KFGame/BrewedPC/Maps/Retry/KF-Retry.kfm", 4096);
    retry.start(retry_root, retry_options(StorageKind::solid_state, 4 * gib));
    const auto appeared = wait_for_terminal(retry);
    CHECK(appeared.state == StartupPrewarmState::complete);
    CHECK(appeared.files_read == 1);

    retry.start(retry_root, retry_options(StorageKind::unknown, 4 * gib));
    CHECK(wait_for_terminal(retry).state ==
          StartupPrewarmState::skipped_unknown_storage);
    retry.start(retry_root, retry_options(StorageKind::solid_state, 4 * gib));
    CHECK(wait_for_terminal(retry).state == StartupPrewarmState::complete);

    retry.start(retry_root, retry_options(StorageKind::solid_state, 2 * gib));
    CHECK(wait_for_terminal(retry).state ==
          StartupPrewarmState::skipped_low_memory);
    retry.start(retry_root, retry_options(StorageKind::solid_state, 4 * gib));
    CHECK(wait_for_terminal(retry).state == StartupPrewarmState::complete);

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
    std::filesystem::remove_all(retry_root, cleanup_error);
    std::filesystem::remove_all(cancellation_root, cleanup_error);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
