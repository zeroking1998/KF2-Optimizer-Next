#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace kf2::game {

namespace detail {
inline constexpr std::size_t kInitialVolumeExtentBufferBytes = 4096;
inline constexpr std::size_t kMaximumVolumeExtentBufferBytes = 1024 * 1024;
[[nodiscard]] std::optional<std::vector<std::uint32_t>>
parse_volume_disk_extents(std::span<const std::byte> storage,
                          std::size_t returned_bytes) noexcept;
}  // namespace detail

enum class StorageKind { unknown, solid_state, rotational };

enum class StartupPrewarmState {
    idle,
    waiting,
    running,
    complete,
    cancelled,
    skipped_unknown_storage,
    skipped_low_memory,
    skipped_no_files,
    failed,
};

struct StartupPrewarmFile {
    std::filesystem::path path;
    std::uint64_t bytes{0};
};

struct StartupPrewarmSnapshot {
    StartupPrewarmState state{StartupPrewarmState::idle};
    std::uint64_t bytes_planned{0};
    std::uint64_t bytes_read{0};
    std::uint32_t files_read{0};
};

struct StartupPrewarmOptions {
    std::chrono::milliseconds idle_delay{std::chrono::seconds{2}};
    std::optional<StorageKind> storage_override;
    std::optional<std::uint64_t> available_memory_override;
    std::wstring map_name;
    bool include_common_startup_files{true};
};

[[nodiscard]] std::uint64_t startup_prewarm_budget(
    StorageKind storage, std::uint64_t available_memory_bytes) noexcept;
[[nodiscard]] std::uint64_t startup_prewarm_file_budget(
    std::uint64_t file_size_bytes) noexcept;
[[nodiscard]] std::vector<StartupPrewarmFile> build_startup_prewarm_plan(
    const std::filesystem::path& install_root, StorageKind storage,
    std::uint64_t available_memory_bytes,
    std::wstring_view map_name = {},
    bool include_common_startup_files = true);
[[nodiscard]] StorageKind storage_kind_for_path(
    const std::filesystem::path& path) noexcept;
[[nodiscard]] bool startup_prewarm_retryable(
    StartupPrewarmState state) noexcept;
[[nodiscard]] std::optional<std::wstring> map_prewarm_request_from_log_line(
    std::string_view line);

class StartupPrewarmer final {
public:
    StartupPrewarmer();
    ~StartupPrewarmer();
    StartupPrewarmer(const StartupPrewarmer&) = delete;
    StartupPrewarmer& operator=(const StartupPrewarmer&) = delete;

    void start(std::filesystem::path install_root,
               StartupPrewarmOptions options = {});
    void request_stop() noexcept;
    void stop_and_wait();
    [[nodiscard]] StartupPrewarmSnapshot snapshot() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

#ifdef KF2_STARTUP_PREWARMER_TESTING
namespace detail {
void fail_next_startup_prewarm_plan() noexcept;
}
#endif

}  // namespace kf2::game
