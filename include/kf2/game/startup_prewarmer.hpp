#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace kf2::game {

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
};

[[nodiscard]] std::uint64_t startup_prewarm_budget(
    StorageKind storage, std::uint64_t available_memory_bytes) noexcept;
[[nodiscard]] std::vector<StartupPrewarmFile> build_startup_prewarm_plan(
    const std::filesystem::path& install_root, StorageKind storage,
    std::uint64_t available_memory_bytes);
[[nodiscard]] StorageKind storage_kind_for_path(
    const std::filesystem::path& path) noexcept;

class StartupPrewarmer final {
public:
    StartupPrewarmer();
    ~StartupPrewarmer();
    StartupPrewarmer(const StartupPrewarmer&) = delete;
    StartupPrewarmer& operator=(const StartupPrewarmer&) = delete;

    void start(std::filesystem::path install_root,
               StartupPrewarmOptions options = {});
    void request_stop() noexcept;
    void stop_and_wait() noexcept;
    [[nodiscard]] StartupPrewarmSnapshot snapshot() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace kf2::game
