#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "kf2/game/game_log_session.hpp"

namespace kf2::game::detail {

inline constexpr std::size_t kMaximumLineBytes = 16 * 1024;

[[nodiscard]] bool equals_ascii_case_insensitive(
    std::string_view left, std::string_view right);
[[nodiscard]] std::optional<std::pair<bool, int>> parse_zed_count_line(
    std::string_view line);
[[nodiscard]] std::optional<std::pair<int, int>> parse_wave_snapshot_line(
    std::string_view line);
[[nodiscard]] std::optional<std::uint16_t> parse_adaptive_bridge_line(
    std::string_view line);
[[nodiscard]] std::optional<bool> apply_offline_telemetry_line(
    GameLogSession& session, std::string_view line,
    std::uint64_t observed_at_ns);
void clear_offline_telemetry_snapshot(GameLogSession& session) noexcept;
void clear_gameplay_snapshot(GameLogSession& session) noexcept;

}  // namespace kf2::game::detail
