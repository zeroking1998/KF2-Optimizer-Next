#include "kf2/game/game_log_session.hpp"

#include <cmath>
#include <limits>

namespace kf2::game {
namespace {

std::wstring ascii_wide(std::string_view text) {
    return {text.begin(), text.end()};
}

std::wstring compact_real(double value) {
    const double rounded = std::round(value);
    if (std::abs(value - rounded) < 0.000001 &&
        rounded >= static_cast<double>(std::numeric_limits<int>::min()) &&
        rounded <= static_cast<double>(std::numeric_limits<int>::max())) {
        return std::to_wstring(static_cast<int>(rounded));
    }
    auto result = std::to_wstring(value);
    while (result.size() > 2 && result.back() == L'0') result.pop_back();
    if (!result.empty() && result.back() == L'.') result.pop_back();
    return result;
}

}  // namespace

std::wstring describe_game_log_session(const GameLogSession& session) {
    std::wstring result = session.main_menu
        ? L"KF2 main menu"
        : L"KF2 session: " + ascii_wide(session.map);
    if (session.game_class) {
        const auto separator = session.game_class->rfind('.');
        result += L" | mode: " + ascii_wide(session.game_class->substr(
            separator == std::string::npos ? 0 : separator + 1));
    }
    if (session.difficulty) {
        result += L" | difficulty code: " + compact_real(*session.difficulty);
    }
    if (session.game_length) {
        result += L" | length code: " + std::to_wstring(*session.game_length);
    }
    if (!session.main_menu) {
        result += session.phase == GameLogPhase::match_ended
            ? L" | state: match ended" : L" | state: map loaded";
    }
    if (session.net_mode && *session.net_mode == "NM_Standalone" &&
        session.phase == GameLogPhase::map_loaded) {
        if (session.zeds_alive) {
            result += L" | last confirmed living Zeds: " +
                      std::to_wstring(*session.zeds_alive);
        }
        if (session.zeds_remaining) {
            result += L" | last confirmed wave remaining: " +
                      std::to_wstring(*session.zeds_remaining);
        }
        if (session.wave_number && session.wave_total_ai) {
            result += L" | last confirmed wave: " +
                      std::to_wstring(*session.wave_number) + L" (" +
                      std::to_wstring(*session.wave_total_ai) + L" total AI)";
        }
        if (session.telemetry_living_zeds &&
            session.telemetry_corpse_total) {
            result += L" | probe: " +
                std::to_wstring(*session.telemetry_living_zeds) +
                L" living, " +
                std::to_wstring(*session.telemetry_corpse_total) +
                L" corpses";
            if (session.telemetry_corpse_awake &&
                session.telemetry_corpse_sleeping) {
                result += L" (" +
                    std::to_wstring(*session.telemetry_corpse_awake) +
                    L" active/" +
                    std::to_wstring(*session.telemetry_corpse_sleeping) +
                    L" sleeping)";
            }
            if (session.telemetry_dismembered_limbs) {
                result += L", " +
                    std::to_wstring(*session.telemetry_dismembered_limbs) +
                    L" detached limbs";
            }
            if (session.telemetry_ragdoll_warned_corpses) {
                result += L", " +
                    std::to_wstring(*session.telemetry_ragdoll_warned_corpses) +
                    L" ragdoll warnings";
            }
            if (session.telemetry_visible_gibs) {
                result += L", " +
                    std::to_wstring(*session.telemetry_visible_gibs) + L" gibs";
            }
            if (session.telemetry_wound_decals &&
                session.telemetry_splatter_decals &&
                session.telemetry_pool_decals &&
                session.telemetry_impact_decals &&
                session.telemetry_explosion_decals) {
                result += L", " + std::to_wstring(
                    *session.telemetry_wound_decals +
                    *session.telemetry_splatter_decals +
                    *session.telemetry_pool_decals +
                    *session.telemetry_impact_decals +
                    *session.telemetry_explosion_decals) + L" decals";
            }
            if (session.telemetry_gore_particles &&
                session.telemetry_world_particles) {
                result += L", particles " +
                    std::to_wstring(*session.telemetry_gore_particles) +
                    L" gore/" +
                    std::to_wstring(*session.telemetry_world_particles) +
                    L" world";
            }
            if (session.telemetry_ground_fire_particle_components &&
                session.telemetry_impact_particle_components) {
                result += L" (" + std::to_wstring(
                    *session.telemetry_ground_fire_particle_components) +
                    L" ground-fire/" + std::to_wstring(
                    *session.telemetry_impact_particle_components) +
                    L" impact components)";
            }
            if (session.telemetry_spray_actors &&
                session.telemetry_explosion_actors) {
                result += L", effects " +
                    std::to_wstring(*session.telemetry_spray_actors) +
                    L" spray/" +
                    std::to_wstring(*session.telemetry_explosion_actors) +
                    L" explosion actors";
            }
            if (session.telemetry_zed_time_active.value_or(false)) {
                result += L", Zed Time active";
            }
        }
    }
    if (session.net_mode) {
        if (*session.net_mode == "NM_Standalone") {
            result += L" | connection: offline";
        } else if (*session.net_mode == "NM_Client") {
            result += L" | connection: network client";
        } else if (*session.net_mode == "NM_ListenServer") {
            result += L" | connection: listen server";
        } else if (*session.net_mode == "NM_DedicatedServer") {
            result += L" | connection: dedicated server";
        }
    }
    result += L" (read-only active launch log)";
    return result;
}

bool game_log_is_active_gameplay(const GameLogSession& session) noexcept {
    return !session.main_menu && !session.map.empty() &&
           session.phase == GameLogPhase::map_loaded;
}

bool game_log_is_offline_gameplay(const GameLogSession& session) noexcept {
    return game_log_is_active_gameplay(session) && session.net_mode &&
           *session.net_mode == "NM_Standalone";
}

bool game_log_observation_is_fresh(
    const std::optional<int>& value, std::uint64_t observed_at_ns,
    std::uint64_t now_ns, std::uint64_t maximum_age_ns) noexcept {
    return value.has_value() && observed_at_ns != 0 && now_ns >= observed_at_ns &&
           now_ns - observed_at_ns <= maximum_age_ns;
}

}  // namespace kf2::game
