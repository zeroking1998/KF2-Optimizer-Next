#pragma once

#include "features/telemetry/telemetry_frame.hpp"

namespace kf2::telemetry_pipeline {

enum class CorpseTelemetryState { unavailable, available, stale };

// Presentation cache only. Never copy cached values into an AdaptiveSample.
class CorpseTelemetryTracker final {
public:
    static constexpr std::uint64_t grace_ns = 10'000'000'000ULL;
    struct Result {
        CorpseTelemetryState state{CorpseTelemetryState::unavailable};
        std::optional<int> runtime_limit;
        const char* event{nullptr};
    };

    void reset() { *this = {}; }

    Result observe(const TelemetryFrame& frame, bool permitted = true) {
        const auto old_state = state_;
        const auto now = frame.observed_at_ns;
        const bool identity_changed = identity_ != frame.identity;
        if (identity_changed || (last_clock_ && now < last_clock_)) reset();
        identity_ = frame.identity;
        last_clock_ = now;
        const auto& game = frame.gameplay;
        const bool explicit_online = game && game->net_mode &&
            (*game->net_mode == "NM_Client" || *game->net_mode == "NM_ListenServer" ||
             *game->net_mode == "NM_DedicatedServer");
        if (!permitted || !frame.identity.pid || !frame.identity.process_start_id ||
            explicit_online || (game && game->telemetry_observed_ns > now)) {
            limit_.reset();
            state_ = CorpseTelemetryState::unavailable;
            gap_since_.reset();
            last_verified_ = now; // Old samples cannot undo a rejection.
        } else {
            // A replacement provider is not a temporary missing field. Never
            // carry its predecessor's readback across a port/counter reset.
            const bool provider_changed = game &&
                ((port_ && game->telemetry_control_port &&
                  port_ != game->telemetry_control_port) ||
                 (sample_ && game->telemetry_sample &&
                  *game->telemetry_sample < *sample_));
            if (provider_changed) {
                limit_.reset();
                gap_since_.reset();
                state_ = CorpseTelemetryState::unavailable;
            }
            if (game && game->telemetry_control_port) port_ = game->telemetry_control_port;
            if (game && game->telemetry_sample) sample_ = game->telemetry_sample;
            const bool map_changed = game && !map_.empty() && game->map != map_;
            if (game) map_ = game->map;
            const bool fresh = game && frame.active_gameplay && frame.offline_gameplay &&
                game->net_mode == "NM_Standalone" && !game->main_menu &&
                game->phase != game::GameLogPhase::match_ended &&
                game->telemetry_sample.value_or(0) > 0 &&
                game->telemetry_corpse_limit && *game->telemetry_corpse_limit >= 0 &&
                game->telemetry_corpse_total && *game->telemetry_corpse_total >= 0 &&
                game->telemetry_observed_ns != 0 && now >= game->telemetry_observed_ns &&
                now - game->telemetry_observed_ns <= game::kGameLogObservationFreshnessNs &&
                ((!map_changed && state_ == CorpseTelemetryState::available) ||
                 game->telemetry_observed_ns > last_verified_);
            if (fresh) {
                limit_ = game->telemetry_corpse_limit;
                last_verified_ = game->telemetry_observed_ns;
                gap_since_.reset();
                state_ = CorpseTelemetryState::available;
            } else if (limit_) {
                if (!gap_since_) {
                    // Account for a long pause between evaluations as well.
                    gap_since_ = now;
                    if (now >= last_verified_ &&
                        now - last_verified_ > game::kGameLogObservationFreshnessNs)
                        gap_since_ = last_verified_ + game::kGameLogObservationFreshnessNs;
                }
                if (now - *gap_since_ < grace_ns) {
                    state_ = CorpseTelemetryState::stale;
                } else {
                    state_ = CorpseTelemetryState::unavailable;
                    limit_.reset();
                }
            }
        }
        const char* event = nullptr;
        if (state_ != old_state) {
            if (state_ == CorpseTelemetryState::stale)
                event = "CORPSE_TELEMETRY_STALE";
            else if (state_ == CorpseTelemetryState::available)
                event = old_state == CorpseTelemetryState::unavailable || identity_changed
                    ? "CORPSE_TELEMETRY_AVAILABLE" : "CORPSE_TELEMETRY_RECOVERED";
            else event = "CORPSE_TELEMETRY_UNAVAILABLE";
        }
        return {state_, limit_, event};
    }

private:
    telemetry::SampleIdentity identity_{};
    CorpseTelemetryState state_{CorpseTelemetryState::unavailable};
    std::optional<int> limit_;
    std::optional<int> sample_;
    std::optional<std::uint16_t> port_;
    std::optional<std::uint64_t> gap_since_;
    std::uint64_t last_verified_{0}, last_clock_{0};
    std::string map_;
};

} // namespace kf2::telemetry_pipeline
