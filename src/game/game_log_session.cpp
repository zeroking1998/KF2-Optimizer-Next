#include "kf2/game/game_log_session.hpp"
#include "game_log_session_internal.hpp"

#include <limits>

namespace kf2::game {
namespace {

void add_saturated(std::uint64_t& value, std::uint64_t amount = 1) noexcept {
    value = amount > std::numeric_limits<std::uint64_t>::max() - value
        ? std::numeric_limits<std::uint64_t>::max() : value + amount;
}

}  // namespace

bool game_log_reports_engine_exit(std::string_view text) noexcept {
    return text.find("] Exit: Exiting.") != std::string_view::npos ||
           text.find("Log: Log file closed,") != std::string_view::npos ||
           text.find("Critical: appError called:") != std::string_view::npos ||
           text.find("Log: appRequestExit(1)") != std::string_view::npos;
}

bool game_log_requests_settings_restart(std::string_view text) noexcept {
    return text.find("Log: Restarting by request") !=
           std::string_view::npos;
}

std::optional<GameLogSession> GameLogSessionParser::feed(
    std::string_view bytes, std::uint64_t observed_at_ns) {
    if (bytes.empty()) return std::nullopt;
    add_saturated(stats_.bytes_received, bytes.size());
    if (bytes.size() > detail::kMaximumLineBytes * 4) {
        pending_.clear();
        add_saturated(stats_.oversized_input_resets);
        add_saturated(stats_.oversized_line_drops);
        return std::nullopt;
    }
    if (pending_.size() + bytes.size() > detail::kMaximumLineBytes * 4) {
        pending_.clear();
        add_saturated(stats_.oversized_input_resets);
    }
    pending_.append(bytes);
    std::optional<GameLogSession> changed;
    for (;;) {
        const auto newline = pending_.find('\n');
        if (newline == std::string::npos) {
            if (pending_.size() > detail::kMaximumLineBytes) {
                pending_.clear();
                add_saturated(stats_.oversized_line_drops);
            }
            break;
        }
        auto line = std::string_view{pending_}.substr(0, newline);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        add_saturated(stats_.lines_processed);
        if (line.size() <= detail::kMaximumLineBytes) {
            if (auto parsed = parse_load_map_line(line); parsed) {
                if (!current_ || *parsed != *current_) changed = *parsed;
                current_ = *parsed;
            } else if (auto mode = parse_net_mode_line(line); mode && current_) {
                if (current_->net_mode != mode) {
                    current_->net_mode = std::move(*mode);
                    if (*current_->net_mode != "NM_Standalone") {
                        detail::clear_gameplay_snapshot(*current_);
                    }
                    changed = *current_;
                }
            } else if (const auto port =
                           detail::parse_adaptive_bridge_line(line);
                       current_ && !current_->main_menu &&
                       current_->phase != GameLogPhase::match_ended && port) {
                if (current_->telemetry_control_port != port) {
                    current_->telemetry_control_port = port;
                    changed = *current_;
                }
            } else if (current_ && current_->net_mode == "NM_Standalone") {
                if (const auto count = detail::parse_zed_count_line(line);
                    count) {
                    auto& target = count->first ? current_->zeds_remaining
                                                : current_->zeds_alive;
                    auto& observed = count->first
                        ? current_->zeds_remaining_observed_ns
                        : current_->zeds_alive_observed_ns;
                    const bool value_changed = target != count->second;
                    target = count->second;
                    if (observed_at_ns != 0) observed = observed_at_ns;
                    if (value_changed) {
                        changed = *current_;
                    }
                } else if (const auto wave = detail::parse_wave_snapshot_line(line);
                           wave && current_->game_class &&
                           detail::equals_ascii_case_insensitive(
                               *current_->game_class,
                               "KFGameContent.KFGameInfo_Survival")) {
                    const int wave_number = wave->first + 1;
                    const bool value_changed =
                        current_->wave_number != wave_number ||
                        current_->wave_total_ai != wave->second;
                    current_->wave_number = wave_number;
                    current_->wave_total_ai = wave->second;
                    if (observed_at_ns != 0) {
                        current_->wave_observed_ns = observed_at_ns;
                    }
                    if (value_changed) changed = *current_;
                } else if (const auto telemetry_changed =
                               detail::apply_offline_telemetry_line(
                                   *current_, line, observed_at_ns);
                           telemetry_changed) {
                    if (*telemetry_changed) changed = *current_;
                } else if (!current_->main_menu &&
                           current_->phase != GameLogPhase::match_ended &&
                           line.find("ScriptLog: KFGameInfo_") !=
                               std::string_view::npos &&
                           line.find(" - MatchEnded.BeginState") !=
                               std::string_view::npos) {
                    current_->phase = GameLogPhase::match_ended;
                    detail::clear_gameplay_snapshot(*current_);
                    changed = *current_;
                }
            } else if (current_ && !current_->main_menu &&
                       current_->phase != GameLogPhase::match_ended &&
                       line.find("ScriptLog: KFGameInfo_") !=
                           std::string_view::npos &&
                       line.find(" - MatchEnded.BeginState") !=
                           std::string_view::npos) {
                current_->phase = GameLogPhase::match_ended;
                detail::clear_gameplay_snapshot(*current_);
                changed = *current_;
            }
        } else {
            add_saturated(stats_.oversized_line_drops);
        }
        pending_.erase(0, newline + 1);
    }
    return changed;
}

std::optional<GameLogSession> GameLogSessionParser::expire_observations(
    std::uint64_t now_ns, std::uint64_t maximum_age_ns) noexcept {
    if (!current_ || now_ns == 0) return std::nullopt;
    bool changed = false;
    const auto expire = [&](std::optional<int>& value,
                            std::uint64_t& observed_at_ns) {
        if (!value || observed_at_ns == 0 || now_ns < observed_at_ns ||
            now_ns - observed_at_ns <= maximum_age_ns) {
            return;
        }
        value.reset();
        observed_at_ns = 0;
        changed = true;
    };
    expire(current_->zeds_remaining, current_->zeds_remaining_observed_ns);
    expire(current_->zeds_alive, current_->zeds_alive_observed_ns);
    if ((current_->wave_number || current_->wave_total_ai) &&
        current_->wave_observed_ns != 0 && now_ns >= current_->wave_observed_ns &&
        now_ns - current_->wave_observed_ns > maximum_age_ns) {
        current_->wave_number.reset();
        current_->wave_total_ai.reset();
        current_->wave_observed_ns = 0;
        changed = true;
    }
    if (current_->telemetry_observed_ns != 0 &&
        now_ns >= current_->telemetry_observed_ns &&
        now_ns - current_->telemetry_observed_ns > maximum_age_ns) {
        detail::clear_offline_telemetry_snapshot(*current_);
        changed = true;
    }
    return changed ? current_ : std::nullopt;
}

void GameLogSessionParser::reset() noexcept {
    pending_.clear();
    current_.reset();
    stats_ = {};
}

GameLogParserStats GameLogSessionParser::stats() const noexcept {
    return stats_;
}

const std::optional<GameLogSession>& GameLogSessionParser::current() const noexcept {
    return current_;
}

}  // namespace kf2::game
