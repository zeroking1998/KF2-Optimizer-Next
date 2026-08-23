#include "kf2/game/game_log_session.hpp"
#include "game_log_session_internal.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>

namespace kf2::game {
namespace detail {

bool safe_token(std::string_view value, std::size_t maximum,
                bool allow_colon = false) {
    if (value.empty() || value.size() > maximum) return false;
    return std::ranges::all_of(value, [allow_colon](unsigned char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') ||
               character == '_' || character == '-' || character == '.' ||
               (allow_colon && character == ':');
    });
}

bool equals_ascii_case_insensitive(std::string_view left,
                                   std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lower = [](unsigned char character) {
            return character >= 'A' && character <= 'Z'
                ? static_cast<unsigned char>(character + ('a' - 'A'))
                : character;
        };
        if (lower(static_cast<unsigned char>(left[index])) !=
            lower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

std::optional<std::uint16_t> parse_adaptive_bridge_line(
    std::string_view line) {
    constexpr std::string_view marker =
        "KF2OPT_ADAPTIVE_BRIDGE state=ready port=";
    const auto marker_offset = line.find(marker);
    if (marker_offset == std::string_view::npos) return std::nullopt;
    auto value = line.substr(marker_offset + marker.size());
    const auto delimiter = value.find_first_of(" \t\r\n");
    if (delimiter != std::string_view::npos) value = value.substr(0, delimiter);
    unsigned int port = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() ||
        port == 0 || port > 65535) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(port);
}

std::optional<double> parse_real(std::string_view text) {
    double value = 0.0;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value,
        std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        !std::isfinite(value) || value < 0.0 || value > 100.0) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> parse_integer(std::string_view text) {
    int value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value < 0 || value > 100) {
        return std::nullopt;
    }
    return value;
}

std::optional<int> parse_bounded_count(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) {
        text.remove_suffix(1);
    }
    int value = 0;
    const auto parsed = std::from_chars(
        text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        value < 0 || value > 100'000) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::pair<bool, int>> parse_zed_count_line(
    std::string_view line) {
    constexpr std::string_view remaining_marker =
        "@@@@ ZED COUNT DEBUG: MyKFGRI.AIRemaining =";
    constexpr std::string_view alive_marker =
        "@@@@ ZED COUNT DEBUG: AIAliveCount =";
    if (const auto position = line.find(remaining_marker);
        position != std::string_view::npos) {
        const auto value = parse_bounded_count(
            line.substr(position + remaining_marker.size()));
        if (value) return std::pair{true, *value};
    }
    if (const auto position = line.find(alive_marker);
        position != std::string_view::npos) {
        const auto value = parse_bounded_count(
            line.substr(position + alive_marker.size()));
        if (value) return std::pair{false, *value};
    }
    return std::nullopt;
}

std::optional<std::pair<int, int>> parse_wave_snapshot_line(
    std::string_view line) {
    constexpr std::string_view marker =
        "KFAISpawnManager.SetupNextWave() NextWave:";
    constexpr std::string_view total_marker = "WaveTotalAI:";
    const auto position = line.find(marker);
    if (position == std::string_view::npos) return std::nullopt;
    const auto value_start = position + marker.size();
    const auto total_position = line.find(total_marker, value_start);
    if (total_position == std::string_view::npos) return std::nullopt;
    const auto wave_index = parse_bounded_count(
        line.substr(value_start, total_position - value_start));
    const auto total = parse_bounded_count(
        line.substr(total_position + total_marker.size()));
    if (!wave_index || *wave_index > 255 || !total) return std::nullopt;
    return std::pair{*wave_index, *total};
}

}  // namespace detail

std::optional<GameLogSession> parse_load_map_line(std::string_view line) {
    constexpr std::string_view marker = "Log: LoadMap: ";
    const auto marker_position = line.find(marker);
    if (marker_position == std::string_view::npos) return std::nullopt;
    auto payload = line.substr(marker_position + marker.size());
    if (payload.empty() || payload.size() > detail::kMaximumLineBytes) return std::nullopt;

    const auto first_separator = payload.find('?');
    const auto map = payload.substr(0, first_separator);
    if (!detail::safe_token(map, 128)) return std::nullopt;

    GameLogSession result;
    result.map.assign(map);
    result.main_menu = detail::equals_ascii_case_insensitive(map, "KFMainMenu");
    result.phase = result.main_menu ? GameLogPhase::main_menu
                                    : GameLogPhase::map_loaded;
    if (first_separator == std::string_view::npos) return result;

    std::size_t offset = first_separator + 1;
    while (offset <= payload.size()) {
        const auto end = payload.find('?', offset);
        const auto field = payload.substr(
            offset, end == std::string_view::npos ? payload.size() - offset
                                                  : end - offset);
        const auto equals = field.find('=');
        if (equals != std::string_view::npos) {
            const auto name = field.substr(0, equals);
            const auto value = field.substr(equals + 1);
            if (name == "Game" && detail::safe_token(value, 256, true)) {
                result.game_class = std::string{value};
            } else if (name == "Difficulty") {
                result.difficulty = detail::parse_real(value);
            } else if (name == "GameLength") {
                result.game_length = detail::parse_integer(value);
            }
        }
        if (end == std::string_view::npos) break;
        offset = end + 1;
    }
    return result;
}

std::optional<std::string> parse_net_mode_line(std::string_view line) {
    constexpr std::string_view marker = "ScriptLog: WI.NetMode:";
    const auto marker_position = line.find(marker);
    if (marker_position == std::string_view::npos) return std::nullopt;
    auto value = line.substr(marker_position + marker.size());
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    constexpr std::array<std::string_view, 4> known_modes{
        "NM_Standalone", "NM_DedicatedServer", "NM_ListenServer", "NM_Client"};
    if (!detail::safe_token(value, 32) ||
        std::ranges::find(known_modes, value) == known_modes.end()) {
        return std::nullopt;
    }
    return std::string{value};
}

}  // namespace kf2::game
