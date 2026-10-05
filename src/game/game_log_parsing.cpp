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

std::optional<AdaptiveBridgeReceipt> parse_adaptive_bridge_line(
    std::string_view line) {
    constexpr std::string_view marker =
        "KF2OPT_ADAPTIVE_BRIDGE state=";
    const auto marker_offset = line.find(marker);
    if (marker_offset == std::string_view::npos) return std::nullopt;
    const auto state_start = marker_offset + marker.size();
    const auto state_end = line.find_first_of(" \t\r\n", state_start);
    const auto state = line.substr(state_start,
        state_end == std::string_view::npos
            ? std::string_view::npos : state_end - state_start);
    if (state == "blocked" || state == "unavailable") {
        return AdaptiveBridgeReceipt{};
    }
    if (state != "ready" || state_end == std::string_view::npos) {
        return std::nullopt;
    }
    constexpr std::string_view port_marker = " port=";
    auto value = line.substr(state_end);
    if (!value.starts_with(port_marker)) return std::nullopt;
    value.remove_prefix(port_marker.size());
    const auto delimiter = value.find_first_of(" \t\r\n");
    if (delimiter != std::string_view::npos) value = value.substr(0, delimiter);
    unsigned int port = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), port);
    if (error != std::errc{} || end != value.data() + value.size() ||
        port == 0 || port > 65535) {
        return std::nullopt;
    }
    return AdaptiveBridgeReceipt{static_cast<std::uint16_t>(port)};
}

std::optional<std::uint64_t> parse_generation(std::string_view value) {
    std::uint64_t generation = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), generation);
    if (error != std::errc{} || end != value.data() + value.size() ||
        generation == 0) {
        return std::nullopt;
    }
    return generation;
}

std::optional<GameplayUiContextReceipt> parse_gameplay_ui_context_line(
    std::string_view line) {
    constexpr std::string_view marker_v1 =
        "KF2OPT_GAMEPLAY_CONTEXT schema=1 state=";
    constexpr std::string_view marker_v2 =
        "KF2OPT_GAMEPLAY_CONTEXT schema=2 state=";
    const auto v2_offset = line.find(marker_v2);
    const auto v1_offset = line.find(marker_v1);
    const bool schema_v2 = v2_offset != std::string_view::npos;
    const auto marker_offset = schema_v2 ? v2_offset : v1_offset;
    if (marker_offset == std::string_view::npos) return std::nullopt;
    const auto marker = schema_v2 ? marker_v2 : marker_v1;
    const auto state_start = marker_offset + marker.size();
    const auto state_end = line.find_first_of(" \t\r\n", state_start);
    const auto state = line.substr(
        state_start, state_end == std::string_view::npos
            ? std::string_view::npos : state_end - state_start);
    GameplayUiContext context;
    if (state == "gameplay") context = GameplayUiContext::gameplay;
    else if (state == "menu") context = GameplayUiContext::menu;
    else if (state == "trader") context = GameplayUiContext::trader;
    else if (state == "unavailable") context = GameplayUiContext::unavailable;
    else return std::nullopt;
    if (!schema_v2) return GameplayUiContextReceipt{context};

    constexpr std::string_view net_marker = " net_mode=";
    constexpr std::string_view map_marker = " map=";
    constexpr std::string_view generation_marker = " generation=";
    const auto net_offset = line.find(net_marker, state_end);
    const auto map_offset = line.find(map_marker, net_offset);
    const auto generation_offset = line.find(generation_marker, map_offset);
    if (state_end == std::string_view::npos ||
        net_offset == std::string_view::npos ||
        map_offset == std::string_view::npos ||
        generation_offset == std::string_view::npos) {
        return std::nullopt;
    }
    const auto net_mode = line.substr(
        net_offset + net_marker.size(),
        map_offset - (net_offset + net_marker.size()));
    const auto map = line.substr(
        map_offset + map_marker.size(),
        generation_offset - (map_offset + map_marker.size()));
    auto generation_text = line.substr(
        generation_offset + generation_marker.size());
    const auto delimiter = generation_text.find_first_of(" \t\r\n");
    if (delimiter != std::string_view::npos) {
        generation_text = generation_text.substr(0, delimiter);
    }
    const auto generation = parse_generation(generation_text);
    if ((net_mode != "NM_Client" && net_mode != "NM_ListenServer") ||
        !safe_token(map, 128) || !generation) {
        return std::nullopt;
    }
    return GameplayUiContextReceipt{
        context, net_mode, map, generation};
}

std::optional<OptimizerSessionContextReceipt>
parse_optimizer_session_context_line(std::string_view line) {
    constexpr std::string_view marker_v1 =
        "KF2OPT_SESSION_CONTEXT schema=1 state=";
    constexpr std::string_view marker_v2 =
        "KF2OPT_SESSION_CONTEXT schema=2 state=";
    constexpr std::string_view net_marker = " net_mode=";
    constexpr std::string_view map_marker = " map=";
    const auto v2_offset = line.find(marker_v2);
    const auto v1_offset = line.find(marker_v1);
    const bool schema_v2 = v2_offset != std::string_view::npos;
    const auto marker_offset = schema_v2 ? v2_offset : v1_offset;
    if (marker_offset == std::string_view::npos) return std::nullopt;
    const auto marker = schema_v2 ? marker_v2 : marker_v1;
    const auto state_start = marker_offset + marker.size();
    const auto net_offset = line.find(net_marker, state_start);
    if (net_offset == std::string_view::npos) return std::nullopt;
    const auto map_offset = line.find(map_marker, net_offset + net_marker.size());
    if (map_offset == std::string_view::npos) return std::nullopt;
    const auto state = line.substr(state_start, net_offset - state_start);
    const auto net_mode = line.substr(
        net_offset + net_marker.size(),
        map_offset - (net_offset + net_marker.size()));
    auto map = line.substr(map_offset + map_marker.size());
    std::optional<std::uint64_t> generation;
    if (schema_v2) {
        constexpr std::string_view generation_marker = " generation=";
        const auto generation_offset = map.find(generation_marker);
        if (generation_offset == std::string_view::npos) return std::nullopt;
        auto generation_text = map.substr(
            generation_offset + generation_marker.size());
        const auto generation_delimiter =
            generation_text.find_first_of(" \t\r\n");
        if (generation_delimiter != std::string_view::npos) {
            generation_text = generation_text.substr(0, generation_delimiter);
        }
        generation = parse_generation(generation_text);
        if (!generation) return std::nullopt;
        map = map.substr(0, generation_offset);
    }
    const auto delimiter = map.find_first_of(" \t\r\n");
    if (delimiter != std::string_view::npos) map = map.substr(0, delimiter);
    const bool client = state == "online_client_read_only" &&
        net_mode == "NM_Client";
    const bool host = state == "online_host_read_only" &&
        net_mode == "NM_ListenServer";
    if ((!client && !host) || !safe_token(map, 128)) return std::nullopt;
    return OptimizerSessionContextReceipt{state, net_mode, map, generation};
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

std::optional<double> parse_seconds_after(
    std::string_view line, std::string_view marker) {
    const auto position = line.find(marker);
    if (position == std::string_view::npos) return std::nullopt;
    auto value = line.substr(position + marker.size());
    const auto delimiter = value.find_first_of(" \t\r\n");
    if (delimiter != std::string_view::npos) value = value.substr(0, delimiter);
    double seconds = 0.0;
    const auto parsed = std::from_chars(
        value.data(), value.data() + value.size(), seconds,
        std::chars_format::general);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
        !std::isfinite(seconds) || seconds < 0.0 || seconds > 3600.0) {
        return std::nullopt;
    }
    return seconds;
}

std::optional<bool> apply_online_corpse_line(
    GameLogSession& session, std::string_view line,
    std::uint64_t observed_at_ns) {
    constexpr std::string_view capability_marker =
        "KF2OPT_ONLINE_CORPSE state=";
    constexpr std::string_view sleep_action_marker =
        "KF2OPT_ONLINE_CORPSE_ACTION state=sleep ";
    constexpr std::string_view capacity_action_marker =
        "KF2OPT_ONLINE_CORPSE_ACTION state=capacity ";
    constexpr std::string_view freeze_action_marker =
        "KF2OPT_ONLINE_CORPSE_ACTION state=freeze ";
    constexpr std::string_view restore_action_marker =
        "KF2OPT_ONLINE_CORPSE_ACTION state=restored ";
    constexpr std::string_view lod_action_marker =
        "KF2OPT_ONLINE_CORPSE_ACTION state=lod ";
    constexpr std::string_view skeleton_action_marker =
        "KF2OPT_ONLINE_CORPSE_ACTION state=skeleton ";
    if (line.find(" local_only=true readback=verified") ==
        std::string_view::npos) {
        return std::nullopt;
    }
    if (const auto offset = line.find(capability_marker);
        offset != std::string_view::npos) {
        auto payload = line.substr(offset + capability_marker.size());
        const auto state_end = payload.find(' ');
        if (state_end == std::string_view::npos) return std::nullopt;
        const auto state = payload.substr(0, state_end);
        if (state != "available" && state != "populated" && state != "pool") {
            return std::nullopt;
        }
        constexpr std::string_view pool_marker = " pool=";
        constexpr std::string_view maximum_marker = " maximum=";
        const auto pool_at = payload.find(pool_marker, state_end);
        const auto maximum_at = payload.find(maximum_marker, state_end);
        if (pool_at == std::string_view::npos ||
            maximum_at == std::string_view::npos || maximum_at <= pool_at) {
            return std::nullopt;
        }
        const auto pool = parse_bounded_count(payload.substr(
            pool_at + pool_marker.size(),
            maximum_at - (pool_at + pool_marker.size())));
        auto maximum_text = payload.substr(maximum_at + maximum_marker.size());
        const auto maximum_end = maximum_text.find(' ');
        if (maximum_end != std::string_view::npos) {
            maximum_text = maximum_text.substr(0, maximum_end);
        }
        const auto maximum = parse_bounded_count(maximum_text);
        // The actual pool can temporarily exceed its configured ceiling.
        if (!pool || !maximum) return std::nullopt;
        const bool changed = session.online_corpse_pool != pool ||
            session.online_corpse_maximum != maximum ||
            session.online_corpse_pool_observed_ns != observed_at_ns;
        session.online_corpse_pool = *pool;
        session.online_corpse_maximum = *maximum;
        session.online_corpse_pool_observed_ns = observed_at_ns;
        if (state != "pool") {
            session.online_corpse_capability_observed_ns = observed_at_ns;
        }
        return changed;
    }
    if (line.find(sleep_action_marker) != std::string_view::npos &&
        line.find(" awake=false ") != std::string_view::npos) {
        const bool changed = !session.online_corpse_sleep_verified;
        session.online_corpse_sleep_verified = true;
        session.online_corpse_action_observed_ns = observed_at_ns;
        return changed;
    }
    if (line.find(freeze_action_marker) != std::string_view::npos &&
        line.find(" physics=PHYS_None ") != std::string_view::npos &&
        line.find(" collision=false ") != std::string_view::npos &&
        line.find(" rigid_body_block=false ") != std::string_view::npos &&
        line.find(" tick_disabled=true ") != std::string_view::npos) {
        const bool changed = !session.online_corpse_freeze_verified;
        session.online_corpse_freeze_verified = true;
        session.online_corpse_action_observed_ns = observed_at_ns;
        return changed;
    }
    if (line.find(restore_action_marker) != std::string_view::npos &&
        line.find(" physics=PHYS_RigidBody ") != std::string_view::npos &&
        line.find(" collision=original ") != std::string_view::npos &&
        line.find(" tick=original ") != std::string_view::npos) {
        const bool changed = !session.online_corpse_restore_verified;
        session.online_corpse_restore_verified = true;
        session.online_corpse_action_observed_ns = observed_at_ns;
        return changed;
    }
    if (line.find(lod_action_marker) != std::string_view::npos &&
        line.find(" target_lod=") != std::string_view::npos &&
        line.find(" fixed_minimum=true ") != std::string_view::npos) {
        const bool changed = !session.online_corpse_lod_verified;
        session.online_corpse_lod_verified = true;
        session.online_corpse_action_observed_ns = observed_at_ns;
        return changed;
    }
    if (line.find(skeleton_action_marker) != std::string_view::npos &&
        line.find(" skip_asleep=true ") != std::string_view::npos &&
        line.find(" no_skeleton_update=true ") != std::string_view::npos &&
        line.find(" fixed_minimum=true ") != std::string_view::npos) {
        const bool changed = !session.online_corpse_skeleton_verified;
        session.online_corpse_skeleton_verified = true;
        session.online_corpse_action_observed_ns = observed_at_ns;
        return changed;
    }
    if (line.find(capacity_action_marker) != std::string_view::npos) {
        constexpr std::string_view before_marker = " pool_before=";
        constexpr std::string_view after_marker = " pool_after=";
        constexpr std::string_view maximum_marker = " maximum=";
        const auto before_at = line.find(before_marker);
        const auto after_at = line.find(after_marker);
        const auto maximum_at = line.find(maximum_marker);
        if (before_at == std::string_view::npos ||
            after_at == std::string_view::npos ||
            maximum_at == std::string_view::npos ||
            !(before_at < after_at && after_at < maximum_at)) {
            return std::nullopt;
        }
        const auto before = parse_bounded_count(line.substr(
            before_at + before_marker.size(),
            after_at - (before_at + before_marker.size())));
        const auto after = parse_bounded_count(line.substr(
            after_at + after_marker.size(),
            maximum_at - (after_at + after_marker.size())));
        auto maximum_text = line.substr(maximum_at + maximum_marker.size());
        const auto maximum_end = maximum_text.find(' ');
        if (maximum_end != std::string_view::npos) {
            maximum_text = maximum_text.substr(0, maximum_end);
        }
        const auto maximum = parse_bounded_count(maximum_text);
        if (!before || !after || !maximum || *before != *after + 1 ||
            *after < *maximum) {
            return std::nullopt;
        }
        const bool changed = !session.online_corpse_capacity_verified ||
            session.online_corpse_pool != after ||
            session.online_corpse_maximum != maximum ||
            session.online_corpse_pool_observed_ns != observed_at_ns;
        session.online_corpse_capacity_verified = true;
        session.online_corpse_pool = *after;
        session.online_corpse_maximum = *maximum;
        session.online_corpse_pool_observed_ns = observed_at_ns;
        session.online_corpse_action_observed_ns = observed_at_ns;
        return changed;
    }
    return std::nullopt;
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
    auto map = payload.substr(0, first_separator);
    if (const auto server_separator = map.rfind('/');
        server_separator != std::string_view::npos) {
        const auto server = map.substr(0, server_separator);
        map.remove_prefix(server_separator + 1);
        if (server.find(':') == std::string_view::npos ||
            !detail::safe_token(server, 256, true)) {
            return std::nullopt;
        }
    }
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
