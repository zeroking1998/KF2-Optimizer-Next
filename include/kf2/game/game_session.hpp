#pragma once

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <memory>

#include "kf2/core/result.hpp"

namespace kf2::game {

struct GameProcessIdentity;

// Pins the verified kernel object, so exits and PID reuse need only a
// nonblocking wait rather than reopening and querying creation time.
class GameProcessHandle final {
public:
    ~GameProcessHandle();
    GameProcessHandle(const GameProcessHandle&) = delete;
    GameProcessHandle& operator=(const GameProcessHandle&) = delete;
    [[nodiscard]] HANDLE get(const GameProcessIdentity& identity) const noexcept;
    [[nodiscard]] bool metrics_readable() const noexcept { return metrics_readable_; }

private:
    friend Result<GameProcessIdentity> bind_game_process(
        std::uint32_t, const std::filesystem::path&);
    GameProcessHandle(HANDLE handle, std::uint32_t pid, std::uint64_t start,
                      bool metrics_readable) noexcept;
    const HANDLE handle_;
    const std::uint32_t pid_;
    const std::uint64_t start_;
    const bool metrics_readable_;
};

struct GameProcessIdentity {
    std::uint32_t pid{0};
    std::uint64_t process_start_id{0};
    std::filesystem::path executable;
    std::shared_ptr<const GameProcessHandle> native_process;
};

enum class WindowUnavailableReason {
    none, hidden, minimized, cloaked, invalid_geometry, not_foreground
};

struct GameWindowState {
    HWND window{};
    GameProcessIdentity process;
    RECT client_bounds{};
    RECT monitor_work_bounds{};
    bool visible{false};
    bool minimized{false};
    bool cloaked{false};
    bool foreground{false};
    WindowUnavailableReason reason{WindowUnavailableReason::invalid_geometry};
};

[[nodiscard]] Result<GameProcessIdentity> bind_game_process(
    std::uint32_t pid, const std::filesystem::path& expected_executable);
[[nodiscard]] bool is_game_process_current(
    const GameProcessIdentity& process) noexcept;
[[nodiscard]] Result<GameWindowState> inspect_game_window(
    const GameProcessIdentity& process, HWND window);
[[nodiscard]] bool is_game_area_covered(const GameWindowState& state,
                                        const RECT& area);
[[nodiscard]] Result<GameProcessIdentity> find_running_game_process(
    const std::filesystem::path& expected_executable);
// A write guard: true includes inspection failures, not only verified games.
[[nodiscard]] bool game_process_may_be_running(
    const std::filesystem::path& expected_executable);
[[nodiscard]] Result<HWND> find_game_window(const GameProcessIdentity& process);

#ifdef KF2_GAME_PROCESS_TESTING
namespace detail {
struct ProcessQueryCounts {
    std::uint32_t opens{0};
    std::uint32_t creation_queries{0};
};
[[nodiscard]] ProcessQueryCounts process_query_counts_for_testing() noexcept;
using ProcessSnapshotFunction = HANDLE (WINAPI*)(DWORD, DWORD);
void set_process_snapshot_for_testing(ProcessSnapshotFunction function) noexcept;
}
#endif

}  // namespace kf2::game
