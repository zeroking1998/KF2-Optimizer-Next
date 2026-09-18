#include "kf2/game/startup_prewarmer.hpp"

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cstddef>
#include <cwctype>
#include <stop_token>
#include <string>
#include <thread>

namespace kf2::game {
namespace {

constexpr std::uint64_t kMiB = 1024ULL * 1024ULL;
constexpr std::uint64_t kGiB = 1024ULL * kMiB;
constexpr std::uint64_t kRotationalPrewarmBytes = 4ULL * kGiB;
constexpr std::uint64_t kSolidStatePrewarmBytes = 2ULL * kGiB;
constexpr std::uint64_t kMemoryReserveBytes = 2ULL * kGiB;
constexpr std::uint64_t kSmallFileThresholdBytes = 32ULL * kMiB;
constexpr std::uint64_t kLargeFileSliceBytes = 512ULL * kMiB;
constexpr std::size_t kReadBufferBytes = 256U * 1024U;

constexpr std::array<std::wstring_view, 8> kStartupFiles{
    L"KFGame/BrewedPC/GlobalShaderCache-PC-D3D-SM5.bin",
    L"KFGame/BrewedPC/Engine.u",
    L"KFGame/BrewedPC/KFGame.u",
    L"KFGame/BrewedPC/KFGameContent.u",
    L"KFGame/BrewedPC/Maps/KFMainMenu.kfm",
    L"KFGame/BrewedPC/EngineDebugMaterials.upk",
    L"KFGame/BrewedPC/RefShaderCache-PC-D3D-SM5.upk",
    L"KFGame/Movies/MenuBG.bik",
};

std::uint64_t available_physical_memory() noexcept {
    MEMORYSTATUSEX status{sizeof(status)};
    return GlobalMemoryStatusEx(&status) ? status.ullAvailPhys : 0;
}

std::optional<std::vector<std::uint32_t>> disk_numbers_for_path(
    const std::filesystem::path& path) noexcept {
    wchar_t volume_root[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), volume_root, MAX_PATH)) {
        return std::nullopt;
    }
    const std::wstring_view root{volume_root};
    if (root.size() < 2 || root[1] != L':') return std::nullopt;

    const std::wstring volume_device = L"\\\\.\\" +
        std::wstring{root.substr(0, 2)};
    HANDLE volume = CreateFileW(
        volume_device.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (volume == INVALID_HANDLE_VALUE) return std::nullopt;

    std::array<std::byte, 4096> storage{};
    DWORD returned = 0;
    const BOOL queried = DeviceIoControl(
        volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
        storage.data(), static_cast<DWORD>(storage.size()), &returned, nullptr);
    CloseHandle(volume);
    if (!queried || returned < sizeof(VOLUME_DISK_EXTENTS)) {
        return std::nullopt;
    }

    const auto* extents = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(
        storage.data());
    const std::size_t required = offsetof(VOLUME_DISK_EXTENTS, Extents) +
        static_cast<std::size_t>(extents->NumberOfDiskExtents) *
            sizeof(DISK_EXTENT);
    if (extents->NumberOfDiskExtents == 0 || returned < required) {
        return std::nullopt;
    }
    std::vector<std::uint32_t> result;
    result.reserve(extents->NumberOfDiskExtents);
    for (DWORD index = 0; index < extents->NumberOfDiskExtents; ++index) {
        const auto number = extents->Extents[index].DiskNumber;
        if (std::find(result.begin(), result.end(), number) == result.end()) {
            result.push_back(number);
        }
    }
    return result;
}

std::optional<bool> disk_incurs_seek_penalty(std::uint32_t number) noexcept {
    const std::wstring device = L"\\\\.\\PhysicalDrive" +
        std::to_wstring(number);
    HANDLE disk = CreateFileW(
        device.c_str(), 0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (disk == INVALID_HANDLE_VALUE) return std::nullopt;
    STORAGE_PROPERTY_QUERY query{};
    query.PropertyId = StorageDeviceSeekPenaltyProperty;
    query.QueryType = PropertyStandardQuery;
    DEVICE_SEEK_PENALTY_DESCRIPTOR descriptor{};
    DWORD returned = 0;
    const BOOL queried = DeviceIoControl(
        disk, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query),
        &descriptor, sizeof(descriptor), &returned, nullptr);
    CloseHandle(disk);
    if (!queried || returned < sizeof(descriptor) ||
        descriptor.Version < sizeof(descriptor) ||
        descriptor.Size < sizeof(descriptor)) {
        return std::nullopt;
    }
    return descriptor.IncursSeekPenalty != FALSE;
}

bool wait_interruptibly(std::stop_token stop,
                        std::chrono::milliseconds duration) noexcept {
    constexpr auto slice = std::chrono::milliseconds{50};
    while (duration > std::chrono::milliseconds::zero()) {
        if (stop.stop_requested()) return false;
        const auto current = std::min(duration, slice);
        std::this_thread::sleep_for(current);
        duration -= current;
    }
    return !stop.stop_requested();
}

bool ascii_iequals(std::wstring_view left, std::wstring_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::towlower(left[index]) != std::towlower(right[index])) {
            return false;
        }
    }
    return true;
}

bool safe_map_name(std::wstring_view map_name) noexcept {
    if (map_name.empty() || map_name.size() > 128) return false;
    return std::all_of(map_name.begin(), map_name.end(), [](wchar_t value) {
        return (value >= L'a' && value <= L'z') ||
               (value >= L'A' && value <= L'Z') ||
               (value >= L'0' && value <= L'9') || value == L'-' ||
               value == L'_';
    });
}

std::string_view trim_ascii(std::string_view value) noexcept {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                              value.front() == '\r')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                              value.back() == '\r')) {
        value.remove_suffix(1);
    }
    return value;
}

bool ascii_iequals(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lower = [](unsigned char value) {
            return value >= 'A' && value <= 'Z'
                ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
        };
        if (lower(static_cast<unsigned char>(left[index])) !=
            lower(static_cast<unsigned char>(right[index]))) return false;
    }
    return true;
}

std::optional<std::string_view> assignment_value(
    std::string_view line, std::string_view wanted_key) noexcept {
    const auto equals = line.find('=');
    if (equals == std::string_view::npos ||
        !ascii_iequals(trim_ascii(line.substr(0, equals)), wanted_key)) {
        return std::nullopt;
    }
    auto value = trim_ascii(line.substr(equals + 1));
    const auto comment = value.find_first_of(";#");
    if (comment != std::string_view::npos) {
        value = trim_ascii(value.substr(0, comment));
    }
    return value;
}

std::vector<std::wstring> cycle_maps(std::string_view value) {
    std::vector<std::wstring> result;
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto begin = value.find('"', offset);
        if (begin == std::string_view::npos) break;
        const auto end = value.find('"', begin + 1);
        if (end == std::string_view::npos) return {};
        const auto token = value.substr(begin + 1, end - begin - 1);
        std::wstring wide;
        wide.reserve(token.size());
        for (const unsigned char character : token) {
            if (character > 0x7f) return {};
            wide.push_back(static_cast<wchar_t>(character));
        }
        if (!safe_map_name(wide)) return {};
        result.push_back(std::move(wide));
        offset = end + 1;
    }
    return result;
}

std::vector<std::filesystem::path> map_packages(
    const std::filesystem::path& install_root,
    std::wstring_view requested_map) {
    std::wstring map_name{requested_map};
    if (map_name.size() > 4 &&
        ascii_iequals(std::wstring_view{map_name}.substr(map_name.size() - 4),
                      L".kfm")) {
        map_name.resize(map_name.size() - 4);
    }
    if (!safe_map_name(map_name)) return {};

    const auto maps_root = install_root / L"KFGame/BrewedPC/Maps";
    std::error_code error;
    std::vector<std::filesystem::path> matches;
    std::filesystem::recursive_directory_iterator iterator{
        maps_root, std::filesystem::directory_options::skip_permission_denied,
        error};
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end) {
        const auto entry = *iterator;
        iterator.increment(error);
        if (!entry.is_regular_file(error) || error) {
            error.clear();
            continue;
        }
        const auto& path = entry.path();
        if (ascii_iequals(path.extension().wstring(), L".kfm") &&
            ascii_iequals(path.stem().wstring(), map_name)) {
            matches.push_back(path);
        }
    }
    if (matches.size() != 1) return {};

    std::vector<std::filesystem::path> result;
    std::filesystem::directory_iterator sibling{
        matches.front().parent_path(),
        std::filesystem::directory_options::skip_permission_denied, error};
    const std::filesystem::directory_iterator sibling_end;
    while (!error && sibling != sibling_end) {
        const auto entry = *sibling;
        sibling.increment(error);
        if (!entry.is_regular_file(error) || error) {
            error.clear();
            continue;
        }
        if (ascii_iequals(entry.path().extension().wstring(), L".kfm")) {
            result.push_back(entry.path());
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

}  // namespace

std::optional<std::wstring> next_map_from_game_config(
    std::string_view ini_bytes, std::wstring_view current_map) {
    if (!safe_map_name(current_map) || ini_bytes.find('\0') !=
            std::string_view::npos) {
        return std::nullopt;
    }
    bool in_game_info = false;
    bool use_map_list = false;
    std::optional<std::size_t> active_cycle;
    std::vector<std::vector<std::wstring>> cycles;
    std::size_t offset = 0;
    while (offset <= ini_bytes.size()) {
        const auto end = ini_bytes.find('\n', offset);
        auto line = trim_ascii(ini_bytes.substr(
            offset, end == std::string_view::npos
                ? ini_bytes.size() - offset : end - offset));
        if (!line.empty() && line.front() == '[' && line.back() == ']') {
            in_game_info = ascii_iequals(
                trim_ascii(line.substr(1, line.size() - 2)),
                "KFGame.KFGameInfo");
        } else if (in_game_info && !line.empty() &&
                   line.front() != ';' && line.front() != '#') {
            if (const auto map_list_value =
                    assignment_value(line, "bUseMapList")) {
                use_map_list = ascii_iequals(*map_list_value, "true") ||
                    *map_list_value == "1";
            } else if (const auto cycle_index_value =
                           assignment_value(line, "ActiveMapCycle")) {
                std::size_t parsed = 0;
                const auto conversion = std::from_chars(
                    cycle_index_value->data(),
                    cycle_index_value->data() + cycle_index_value->size(),
                    parsed);
                if (conversion.ec == std::errc{} &&
                    conversion.ptr == cycle_index_value->data() +
                        cycle_index_value->size()) {
                    active_cycle = parsed;
                }
            } else if (const auto cycle_value =
                           assignment_value(line, "GameMapCycles")) {
                auto maps = cycle_maps(*cycle_value);
                if (maps.empty()) return std::nullopt;
                cycles.push_back(std::move(maps));
            }
        }
        if (end == std::string_view::npos) break;
        offset = end + 1;
    }
    if (!use_map_list || !active_cycle || *active_cycle >= cycles.size()) {
        return std::nullopt;
    }
    const auto& maps = cycles[*active_cycle];
    std::optional<std::size_t> current_index;
    for (std::size_t index = 0; index < maps.size(); ++index) {
        if (!ascii_iequals(maps[index], current_map)) continue;
        if (current_index) return std::nullopt;
        current_index = index;
    }
    if (!current_index || maps.size() < 2) return std::nullopt;
    return maps[(*current_index + 1) % maps.size()];
}

std::uint64_t startup_prewarm_budget(
    StorageKind storage, std::uint64_t available_memory_bytes) noexcept {
    if (storage == StorageKind::unknown) return 0;
    if (available_memory_bytes <= kMemoryReserveBytes) return 0;
    const auto storage_limit = storage == StorageKind::rotational
        ? kRotationalPrewarmBytes : kSolidStatePrewarmBytes;
    return std::min(storage_limit,
                    (available_memory_bytes - kMemoryReserveBytes) / 4ULL);
}

std::uint64_t startup_prewarm_file_budget(
    std::uint64_t file_size_bytes) noexcept {
    if (file_size_bytes <= kSmallFileThresholdBytes) return file_size_bytes;
    return std::min(file_size_bytes, kLargeFileSliceBytes);
}

std::vector<StartupPrewarmFile> build_startup_prewarm_plan(
    const std::filesystem::path& install_root, StorageKind storage,
    std::uint64_t available_memory_bytes, std::wstring_view map_name,
    bool include_common_startup_files) {
    std::vector<StartupPrewarmFile> result;
    std::uint64_t remaining = startup_prewarm_budget(
        storage, available_memory_bytes);
    if (remaining == 0) return result;

    struct Candidate {
        std::filesystem::path path;
        std::uint64_t bytes;
        bool large;
        bool map_specific;
    };
    std::vector<Candidate> candidates;
    candidates.reserve(kStartupFiles.size() + 8);
    const auto append_candidate = [&candidates](
        const std::filesystem::path& path, bool map_specific) {
        if (std::any_of(candidates.begin(), candidates.end(),
                        [&path](const Candidate& candidate) {
                            return candidate.path == path;
                        })) {
            return;
        }
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error) return;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0) return;
        candidates.push_back(
            {path, size, size > kSmallFileThresholdBytes, map_specific});
    };
    if (include_common_startup_files) {
        for (const auto relative : kStartupFiles) {
            append_candidate(install_root / relative, false);
        }
    }
    for (const auto& path : map_packages(install_root, map_name)) {
        append_candidate(path, true);
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& left, const Candidate& right) {
                         if (left.large != right.large) return !left.large;
                         if (left.map_specific != right.map_specific) {
                             return left.map_specific;
                         }
                         return false;
                     });
    for (const auto& candidate : candidates) {
        const auto file_limit = startup_prewarm_file_budget(candidate.bytes);
        const auto selected = std::min(
            {candidate.bytes, file_limit, remaining});
        if (selected == 0) break;
        result.push_back({candidate.path, selected});
        remaining -= selected;
        if (remaining == 0) break;
    }
    return result;
}

StorageKind storage_kind_for_path(
    const std::filesystem::path& path) noexcept {
    const auto disks = disk_numbers_for_path(path);
    if (!disks || disks->empty()) return StorageKind::unknown;
    for (const auto number : *disks) {
        const auto penalty = disk_incurs_seek_penalty(number);
        if (!penalty) return StorageKind::unknown;
        if (*penalty) return StorageKind::rotational;
    }
    return StorageKind::solid_state;
}

struct StartupPrewarmer::Impl final {
    std::jthread worker;
    std::atomic<StartupPrewarmState> state{StartupPrewarmState::idle};
    std::atomic<std::uint64_t> bytes_planned{0};
    std::atomic<std::uint64_t> bytes_read{0};
    std::atomic<std::uint32_t> files_read{0};

    void run(std::stop_token stop, const std::filesystem::path& install_root,
             const StartupPrewarmOptions& options) noexcept {
        state = StartupPrewarmState::waiting;
        if (!wait_interruptibly(stop, options.idle_delay)) {
            state = StartupPrewarmState::cancelled;
            return;
        }
        const auto storage = options.storage_override.value_or(
            storage_kind_for_path(install_root));
        if (storage == StorageKind::unknown) {
            state = StartupPrewarmState::skipped_unknown_storage;
            return;
        }
        const auto memory = options.available_memory_override.value_or(
            available_physical_memory());
        if (startup_prewarm_budget(storage, memory) == 0) {
            state = StartupPrewarmState::skipped_low_memory;
            return;
        }
        const auto plan = build_startup_prewarm_plan(
            install_root, storage, memory, options.map_name,
            options.include_common_startup_files);
        if (plan.empty()) {
            state = StartupPrewarmState::skipped_no_files;
            return;
        }
        std::uint64_t planned = 0;
        for (const auto& file : plan) planned += file.bytes;
        bytes_planned = planned;
        state = StartupPrewarmState::running;

        HANDLE thread = GetCurrentThread();
        const bool background = SetThreadPriority(
            thread, THREAD_MODE_BACKGROUND_BEGIN) != FALSE;
        std::array<std::byte, kReadBufferBytes> buffer{};
        for (const auto& file : plan) {
            if (stop.stop_requested()) break;
            HANDLE input = CreateFileW(
                file.path.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (input == INVALID_HANDLE_VALUE) continue;
            std::uint64_t remaining = file.bytes;
            bool read_any = false;
            while (remaining > 0 && !stop.stop_requested()) {
                const auto requested = static_cast<DWORD>(
                    std::min<std::uint64_t>(buffer.size(), remaining));
                DWORD actual = 0;
                if (!ReadFile(input, buffer.data(), requested, &actual,
                              nullptr) || actual == 0) break;
                read_any = true;
                bytes_read.fetch_add(actual, std::memory_order_relaxed);
                remaining -= actual;
            }
            CloseHandle(input);
            if (read_any) files_read.fetch_add(1, std::memory_order_relaxed);
        }
        if (background) {
            static_cast<void>(SetThreadPriority(
                thread, THREAD_MODE_BACKGROUND_END));
        }
        state = stop.stop_requested()
            ? StartupPrewarmState::cancelled
            : StartupPrewarmState::complete;
    }
};

StartupPrewarmer::StartupPrewarmer()
    : implementation_{std::make_unique<Impl>()} {}
StartupPrewarmer::~StartupPrewarmer() { stop_and_wait(); }

void StartupPrewarmer::start(std::filesystem::path install_root,
                             StartupPrewarmOptions options) {
    stop_and_wait();
    implementation_->state = StartupPrewarmState::idle;
    implementation_->bytes_planned = 0;
    implementation_->bytes_read = 0;
    implementation_->files_read = 0;
    implementation_->worker = std::jthread{
        [impl = implementation_.get(), root = std::move(install_root),
         options](std::stop_token stop) { impl->run(stop, root, options); }};
}

void StartupPrewarmer::request_stop() noexcept {
    if (implementation_ && implementation_->worker.joinable()) {
        implementation_->worker.request_stop();
    }
}

void StartupPrewarmer::stop_and_wait() noexcept {
    if (!implementation_ || !implementation_->worker.joinable()) return;
    implementation_->worker.request_stop();
    implementation_->worker.join();
}

StartupPrewarmSnapshot StartupPrewarmer::snapshot() const noexcept {
    if (!implementation_) return {};
    return {
        implementation_->state.load(std::memory_order_acquire),
        implementation_->bytes_planned.load(std::memory_order_relaxed),
        implementation_->bytes_read.load(std::memory_order_relaxed),
        implementation_->files_read.load(std::memory_order_relaxed),
    };
}

}  // namespace kf2::game
