#pragma once

#include <Windows.h>

#include <cstdint>
#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::game {

struct GameProcessIdentity {
    std::uint32_t pid{0};
    std::uint64_t process_start_id{0};
    std::filesystem::path executable;
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
    bool fully_occluded{false};
    WindowUnavailableReason reason{WindowUnavailableReason::invalid_geometry};
};

[[nodiscard]] Result<GameProcessIdentity> bind_game_process(
    std::uint32_t pid, const std::filesystem::path& expected_executable);
[[nodiscard]] Result<GameWindowState> inspect_game_window(
    const GameProcessIdentity& process, HWND window);
[[nodiscard]] bool is_game_area_covered(const GameWindowState& state,
                                        const RECT& area);
[[nodiscard]] Result<GameProcessIdentity> find_running_game_process(
    const std::filesystem::path& expected_executable);
[[nodiscard]] Result<HWND> find_game_window(const GameProcessIdentity& process);

}  // namespace kf2::game
