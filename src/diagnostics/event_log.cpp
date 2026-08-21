#include "kf2/diagnostics/event_log.hpp"

#include <stdexcept>
#include <limits>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>
#include <Windows.h>

#include "kf2/platform/windows/atomic_file.hpp"

namespace kf2::diagnostics {
namespace {

template <typename String>
void truncate(String& value, std::size_t maximum) {
    if (value.size() > maximum) {
        value.resize(maximum);
    }
}

std::string utf8(std::wstring_view value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string escape(std::string_view value) {
    std::ostringstream output;
    for (const unsigned char character : value) {
        switch (character) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (character < 0x20) {
                    constexpr char digits[] = "0123456789abcdef";
                    output << "\\u00" << digits[character >> 4]
                           << digits[character & 0x0f];
                } else {
                    output << static_cast<char>(character);
                }
        }
    }
    return output.str();
}

void write_events(std::ostringstream& output, const std::vector<Event>& events) {
    bool first = true;
    for (const auto& event : events) {
        if (!first) output << ',';
        first = false;
        const char* severity = event.severity == Severity::error ? "error" :
                               event.severity == Severity::warning ? "warning" : "info";
        output << "{\"sequence\":" << event.sequence
               << ",\"severity\":\"" << severity
               << "\",\"code\":\"" << escape(event.code)
               << "\",\"source\":\"" << escape(utf8(event.source))
               << "\",\"message\":\"" << escape(utf8(event.message))
               << "\",\"repeat_count\":" << event.repeat_count << "}";
    }
}

}  // namespace

EventLog::EventLog(std::size_t capacity,
                   std::filesystem::path persistence_path)
    : capacity_{capacity}, persistence_path_{std::move(persistence_path)} {
    if (capacity == 0) {
        throw std::invalid_argument{"EventLog capacity must be positive"};
    }
    if (!persistence_path_.empty()) {
        std::error_code error;
        const auto parent = persistence_path_.parent_path();
        persistence_ready_ = persistence_path_.is_absolute() &&
            !parent.empty() && std::filesystem::is_directory(parent, error) &&
            !error;
        if (persistence_ready_) {
            std::scoped_lock lock{mutex_};
            persist_locked();
        }
    }
}

void EventLog::append(Event event) {
    truncate(event.code, 64);
    truncate(event.message, 1024);
    truncate(event.source, 128);

    std::scoped_lock lock{mutex_};
    if (stats_.appended != UINT64_MAX) ++stats_.appended;
    if (next_sequence_ == std::numeric_limits<std::uint64_t>::max()) {
        std::uint64_t sequence = 1;
        for (auto& retained : events_) retained.sequence = sequence++;
        next_sequence_ = sequence;
    }
    event.sequence = next_sequence_++;
    if (!events_.empty()) {
        auto& previous = events_.back();
        if (previous.severity == event.severity && previous.code == event.code &&
            previous.message == event.message && previous.source == event.source) {
            if (previous.repeat_count < UINT32_MAX) ++previous.repeat_count;
            if (stats_.deduplicated != UINT64_MAX) ++stats_.deduplicated;
            persist_locked();
            return;
        }
    }
    if (events_.size() == capacity_) {
        events_.pop_front();
        if (stats_.overwritten != UINT64_MAX) ++stats_.overwritten;
    }
    events_.push_back(std::move(event));
    persist_locked();
}

std::vector<Event> EventLog::snapshot() const {
    std::scoped_lock lock{mutex_};
    return {events_.begin(), events_.end()};
}

void EventLog::clear() {
    std::scoped_lock lock{mutex_};
    events_.clear();
    persist_locked();
}

const std::filesystem::path& EventLog::persistence_path() const noexcept {
    return persistence_path_;
}

bool EventLog::persistence_ready() const noexcept {
    std::scoped_lock lock{mutex_};
    return persistence_ready_;
}

EventLogStats EventLog::stats() const noexcept {
    std::scoped_lock lock{mutex_};
    return stats_;
}

void EventLog::persist_locked() noexcept {
    if (!persistence_ready_ || persistence_path_.empty()) return;
    try {
        std::vector<Event> copy{events_.begin(), events_.end()};
        const auto written = platform::windows::atomic_replace_utf8(
            persistence_path_, serialize_events_json(copy));
        if (!written.has_value()) {
            persistence_ready_ = false;
            if (stats_.persistence_failures != UINT64_MAX)
                ++stats_.persistence_failures;
        }
    } catch (...) {
        persistence_ready_ = false;
        if (stats_.persistence_failures != UINT64_MAX)
            ++stats_.persistence_failures;
    }
}

std::string serialize_events_json(const std::vector<Event>& events) {
    std::ostringstream output;
    output << "{\"version\":1,\"events\":[";
    write_events(output, events);
    output << "]}";
    return output.str();
}

std::string serialize_product_report_json(const ProductReport& report) {
    const auto text = [](std::wstring_view value) { return escape(utf8(value)); };
    std::ostringstream output;
    output << "{\"schema\":\"KF2_OPTIMIZER_DIAGNOSTICS_V2\""
           << ",\"build_identity\":\"" << text(report.build_identity) << "\""
           << ",\"runtime\":{\"mode\":\"" << text(report.mode)
           << "\",\"game\":\"" << text(report.game)
           << "\",\"game_session\":\"" << text(report.game_session)
           << "\",\"telemetry\":\"" << text(report.telemetry)
           << "\",\"performance_analysis\":\""
           << text(report.performance_analysis)
           << "\",\"hardware\":\"" << text(report.hardware)
           << "\",\"flex\":\"" << text(report.flex) << "\"";
    if (report.game_pid) output << ",\"game_pid\":" << *report.game_pid;
    else output << ",\"game_pid\":null";
    if (report.game_process_start_id) {
        output << ",\"game_process_start_id\":" << *report.game_process_start_id;
    } else {
        output << ",\"game_process_start_id\":null";
    }
    output << ",\"gameplay\":{\"offline_verified\":"
           << (report.offline_gameplay ? "true" : "false")
           << ",\"zeds_alive\":";
    if (report.zeds_alive) output << *report.zeds_alive;
    else output << "null";
    output << ",\"zeds_remaining\":";
    if (report.zeds_remaining) output << *report.zeds_remaining;
    else output << "null";
    output << ",\"wave_number\":";
    if (report.wave_number) output << *report.wave_number;
    else output << "null";
    output << ",\"wave_total_ai\":";
    if (report.wave_total_ai) output << *report.wave_total_ai;
    else output << "null";
    const auto optional_integer = [&](std::string_view name,
                                      const std::optional<int>& value) {
        output << ",\"" << name << "\":";
        if (value) output << *value;
        else output << "null";
    };
    optional_integer("telemetry_living_zeds", report.telemetry_living_zeds);
    optional_integer("living_classes", report.living_classes);
    optional_integer("living_bosses", report.living_bosses);
    optional_integer("living_visible", report.living_visible);
    optional_integer("living_offscreen", report.living_offscreen);
    optional_integer("living_lod_total", report.living_lod_total);
    optional_integer("living_anim_rate_total", report.living_anim_rate_total);
    optional_integer("living_injured_zones", report.living_injured_zones);
    optional_integer("living_required_bones", report.living_required_bones);
    optional_integer("living_material_slots", report.living_material_slots);
    optional_integer("living_attachments", report.living_attachments);
    optional_integer("living_anim_skipped", report.living_anim_skipped);
    optional_integer("living_bone_atoms_skipped",
                     report.living_bone_atoms_skipped);
    optional_integer("living_bone_interpolation",
                     report.living_bone_interpolation);
    optional_integer("living_kinematic_distance_skipped",
                     report.living_kinematic_distance_skipped);
    optional_integer("living_ticks_offscreen", report.living_ticks_offscreen);
    optional_integer("living_special_moves", report.living_special_moves);
    optional_integer("living_attack_moves", report.living_attack_moves);
    optional_integer("living_grapple_moves", report.living_grapple_moves);
    optional_integer("living_stumbles", report.living_stumbles);
    optional_integer("living_knockdowns", report.living_knockdowns);
    optional_integer("living_hit_reactions", report.living_hit_reactions);
    optional_integer("living_other_special_moves",
                     report.living_other_special_moves);
    optional_integer("corpse_total", report.corpse_total);
    optional_integer("corpse_awake", report.corpse_awake);
    optional_integer("corpse_sleeping", report.corpse_sleeping);
    optional_integer("corpse_other", report.corpse_other);
    optional_integer("corpse_final_pose", report.corpse_final_pose);
    optional_integer("corpse_visible", report.corpse_visible);
    optional_integer("corpse_offscreen", report.corpse_offscreen);
    optional_integer("corpse_lod_total", report.corpse_lod_total);
    optional_integer("corpse_injured_zones", report.corpse_injured_zones);
    optional_integer("corpse_max_age_ms", report.corpse_max_age_ms);
    optional_integer("corpse_limit", report.corpse_limit);
    optional_integer("corpse_offscreen_time_ms",
                     report.corpse_offscreen_time_ms);
    optional_integer("corpse_offscreen_distance",
                     report.corpse_offscreen_distance);
    optional_integer("dismembered_corpses", report.dismembered_corpses);
    optional_integer("dismembered_limbs", report.dismembered_limbs);
    optional_integer("ragdoll_warned_corpses", report.ragdoll_warned_corpses);
    optional_integer("ragdoll_warning_max", report.ragdoll_warning_max);
    const auto optional_boolean = [&](std::string_view name,
                                      const std::optional<bool>& value) {
        output << ",\"" << name << "\":";
        if (value) output << (*value ? "true" : "false");
        else output << "null";
    };
    optional_boolean("corpse_collide_dead", report.corpse_collide_dead);
    optional_boolean("corpse_collide_living", report.corpse_collide_living);
    optional_boolean("corpse_collide_dead_after_sleep",
                     report.corpse_collide_dead_after_sleep);
    optional_boolean("corpse_collide_living_after_sleep",
                     report.corpse_collide_living_after_sleep);
    optional_integer("visible_gibs", report.visible_gibs);
    optional_integer("spray_actors", report.spray_actors);
    optional_integer("fire_spray_actors", report.fire_spray_actors);
    optional_integer("toxic_spray_actors", report.toxic_spray_actors);
    optional_integer("other_spray_actors", report.other_spray_actors);
    optional_integer("explosion_actors", report.explosion_actors);
    optional_integer("damaging_explosion_actors",
                     report.damaging_explosion_actors);
    optional_integer("fire_explosion_actors", report.fire_explosion_actors);
    optional_integer("toxic_explosion_actors", report.toxic_explosion_actors);
    optional_integer("other_damaging_explosion_actors",
                     report.other_damaging_explosion_actors);
    optional_integer("unclassified_explosion_actors",
                     report.unclassified_explosion_actors);
    optional_integer("lingering_explosion_actors",
                     report.lingering_explosion_actors);
    optional_integer("smoke_explosion_actors", report.smoke_explosion_actors);
    optional_integer("bloat_king_fart_explosion_actors",
                     report.bloat_king_fart_explosion_actors);
    optional_integer("smoke_grenade_projectiles",
                     report.smoke_grenade_projectiles);
    optional_integer("puke_mine_projectiles", report.puke_mine_projectiles);
    optional_integer("bloat_king_puke_mine_projectiles",
                     report.bloat_king_puke_mine_projectiles);
    optional_integer("wound_decals", report.wound_decals);
    optional_integer("splatter_decals", report.splatter_decals);
    optional_integer("pool_decals", report.pool_decals);
    optional_integer("impact_decals", report.impact_decals);
    optional_integer("explosion_decals", report.explosion_decals);
    optional_integer("wound_decal_limit", report.wound_decal_limit);
    optional_integer("splatter_decal_limit", report.splatter_decal_limit);
    optional_integer("pool_decal_limit", report.pool_decal_limit);
    optional_integer("impact_decal_limit", report.impact_decal_limit);
    optional_integer("explosion_decal_limit", report.explosion_decal_limit);
    optional_integer("blood_effect_limit", report.blood_effect_limit);
    optional_integer("gore_effect_limit", report.gore_effect_limit);
    optional_integer("wound_lifetime_ms", report.wound_lifetime_ms);
    optional_integer("splatter_lifetime_ms", report.splatter_lifetime_ms);
    optional_integer("pool_lifetime_ms", report.pool_lifetime_ms);
    optional_integer("gib_lifetime_ms", report.gib_lifetime_ms);
    optional_integer("gore_particle_components",
                     report.gore_particle_components);
    optional_integer("gore_particles", report.gore_particles);
    optional_integer("gore_particle_visible_components",
                     report.gore_particle_visible_components);
    optional_integer("gore_particle_lod_total",
                     report.gore_particle_lod_total);
    optional_integer("gore_particle_bounded_components",
                     report.gore_particle_bounded_components);
    optional_integer("world_particle_components",
                     report.world_particle_components);
    optional_integer("world_particles", report.world_particles);
    optional_integer("world_particle_visible_components",
                     report.world_particle_visible_components);
    optional_integer("world_particle_lod_total",
                     report.world_particle_lod_total);
    optional_integer("world_particle_bounded_components",
                     report.world_particle_bounded_components);
    optional_integer("ground_fire_particle_components",
                     report.ground_fire_particle_components);
    optional_integer("ground_fire_particles", report.ground_fire_particles);
    optional_integer("impact_particle_components",
                     report.impact_particle_components);
    optional_integer("impact_particles", report.impact_particles);
    optional_integer("gore_particle_pool_capacity",
                     report.gore_particle_pool_capacity);
    optional_integer("world_particle_pool_capacity",
                     report.world_particle_pool_capacity);
    optional_integer("ground_fire_particle_pool_capacity",
                     report.ground_fire_particle_pool_capacity);
    optional_integer("impact_particle_pool_capacity",
                     report.impact_particle_pool_capacity);
    optional_integer("particle_constant_spawn_emitters",
                     report.particle_constant_spawn_emitters);
    optional_integer("particle_dynamic_spawn_emitters",
                     report.particle_dynamic_spawn_emitters);
    optional_integer("particle_constant_spawn_rate_milli",
                     report.particle_constant_spawn_rate_milli);
    optional_integer("particle_burst_entries", report.particle_burst_entries);
    optional_integer("particle_peak_capacity", report.particle_peak_capacity);
    optional_integer("particle_flex_components",
                     report.particle_flex_components);
    optional_integer("particle_flex_fluid_components",
                     report.particle_flex_fluid_components);
    optional_integer("particle_flex_nonfluid_components",
                     report.particle_flex_nonfluid_components);
    optional_integer("particle_flex_mixed_components",
                     report.particle_flex_mixed_components);
    optional_integer("particle_nonflex_components",
                     report.particle_nonflex_components);
    optional_integer("particle_unclassified_components",
                     report.particle_unclassified_components);
    optional_integer("flex_surrogate_particles",
                     report.flex_surrogate_particles);
    optional_integer("flex_surrogate_lod", report.flex_surrogate_lod);
    optional_boolean("flex_surrogate_active", report.flex_surrogate_active);
    optional_boolean("flex_surrogate_visible", report.flex_surrogate_visible);
    optional_boolean("zed_time_active", report.zed_time_active);
    output << ",\"snapshot_fresh\":"
           << (report.gameplay_snapshot_fresh ? "true" : "false")
           << ",\"oldest_snapshot_age_ms\":";
    if (report.gameplay_snapshot_age_ms) {
        output << *report.gameplay_snapshot_age_ms;
    } else {
        output << "null";
    }
    output << '}';
    output << "},\"optimizer\":{\"profile\":\""
           << text(report.optimizer_profile) << "\",\"quality_policy\":\""
           << text(report.quality_policy) << "\",\"target_fps\":"
           << report.target_fps << ",\"restore_config_after_game\":"
           << (report.restore_config_after_game ? "true" : "false")
           << "},\"overlay\":{\"enabled\":"
           << (report.overlay_enabled ? "true" : "false")
           << ",\"position\":\"" << text(report.overlay_position)
           << "\",\"scale_percent\":" << report.overlay_scale_percent
           << "}"
           << ",\"event_log_stats\":{\"appended\":"
           << report.event_log_stats.appended
           << ",\"deduplicated\":" << report.event_log_stats.deduplicated
           << ",\"overwritten\":" << report.event_log_stats.overwritten
           << ",\"persistence_failures\":"
           << report.event_log_stats.persistence_failures
           << "},\"game_log_stats\":{\"bytes_received\":"
           << report.game_log_stats.bytes_received
           << ",\"lines_processed\":" << report.game_log_stats.lines_processed
           << ",\"oversized_input_resets\":"
           << report.game_log_stats.oversized_input_resets
           << ",\"oversized_line_drops\":"
           << report.game_log_stats.oversized_line_drops
           << "},\"crash_records\":{\"retained\":"
           << report.retained_crash_records
           << ",\"content_included\":false}"
           << ",\"events\":[";
    write_events(output, report.events);
    output << "]}";
    return output.str();
}

std::string serialize_support_bundle_json(
    const ProductReport& report, std::string_view issue72_inventory_json) {
    const bool inventory_is_object = issue72_inventory_json.size() >= 2 &&
        issue72_inventory_json.front() == '{' &&
        issue72_inventory_json.back() == '}';
    std::ostringstream output;
    output << "{\"schema\":\"KF2_OPTIMIZER_SUPPORT_BUNDLE_V1\""
           << ",\"privacy\":\"Local only; no dump, command line, user files or uploaded data\""
           << ",\"diagnostics\":" << serialize_product_report_json(report)
           << ",\"issue72_inventory\":";
    if (inventory_is_object) output << issue72_inventory_json;
    else output << "null";
    output << '}';
    return output.str();
}

}  // namespace kf2::diagnostics
