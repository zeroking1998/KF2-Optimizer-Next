#pragma once

#include <optional>

#include "kf2/game/game_session.hpp"
#include "kf2/telemetry/telemetry_snapshot.hpp"

namespace kf2::app {
struct UiRuntime;
}

namespace kf2::telemetry_pipeline {

enum class SessionDisposition {
    waiting_for_process,
    waiting_for_window,
    waiting_for_scene,
    ready,
    reconnecting,
    session_ended,
};

enum class BoundProcessTransition {
    same_process,
    replacement_process,
    ended,
};

enum class AdaptiveControlFailureDisposition {
    failed,
    game_ended,
};

[[nodiscard]] constexpr AdaptiveControlFailureDisposition
classify_adaptive_control_failure(bool verified_process_running,
                                  bool engine_exit_reported) noexcept {
    return !verified_process_running || engine_exit_reported
        ? AdaptiveControlFailureDisposition::game_ended
        : AdaptiveControlFailureDisposition::failed;
}

[[nodiscard]] constexpr BoundProcessTransition
classify_bound_process_transition(bool same_process_running,
                                  bool verified_process_running) noexcept {
    if (same_process_running) return BoundProcessTransition::same_process;
    return verified_process_running
        ? BoundProcessTransition::replacement_process
        : BoundProcessTransition::ended;
}

struct SessionGateInput final {
    bool process_bound{false};
    bool process_identity_matches{true};
    bool same_process_running{true};
    bool window_ready{false};
    bool scene_ready{false};
    bool present_source_bound{false};
    bool reconnecting{false};
};

struct SessionStageResult final {
    SessionDisposition disposition{SessionDisposition::waiting_for_process};
    std::optional<game::GameWindowState> window;
};

inline constexpr std::uint64_t kSilentPresentRestartNs = 3'000'000'000ULL;
inline constexpr unsigned int kMaximumPresentRestarts = 2;
inline constexpr std::uint64_t kGameRestartHandoffNs = 10'000'000'000ULL;
inline constexpr std::uint64_t kNewSettingsRestartHandoffNs =
    300'000'000'000ULL;

[[nodiscard]] constexpr std::uint64_t game_restart_handoff_timeout_ns(
    bool new_settings_restart) noexcept {
    return new_settings_restart ? kNewSettingsRestartHandoffNs
                                : kGameRestartHandoffNs;
}

enum class RestartHandoffDisposition {
    inactive,
    waiting,
    previous_process_resumed,
    replacement_found,
    expired,
};

struct RestartHandoffInput final {
    bool pending{false};
    bool verified_process_found{false};
    bool same_process_identity{false};
    std::uint64_t deadline_ns{0};
    std::uint64_t now_ns{0};
};

[[nodiscard]] constexpr RestartHandoffDisposition classify_restart_handoff(
    const RestartHandoffInput& input) noexcept {
    if (!input.pending) return RestartHandoffDisposition::inactive;
    if (input.verified_process_found) {
        return input.same_process_identity
            ? RestartHandoffDisposition::previous_process_resumed
            : RestartHandoffDisposition::replacement_found;
    }
    if (input.deadline_ns != 0 && input.now_ns >= input.deadline_ns) {
        return RestartHandoffDisposition::expired;
    }
    return RestartHandoffDisposition::waiting;
}

struct SilentPresentInput final {
    bool scene_ready{false};
    bool session_bound{false};
    std::optional<double> fps;
    ::kf2::telemetry::UnavailableReason reason{
        ::kf2::telemetry::UnavailableReason::no_samples};
    std::uint64_t session_started_ns{0};
    std::uint64_t now_ns{0};
    unsigned int restart_count{0};
};

[[nodiscard]] constexpr bool should_reconnect_silent_present(
    const SilentPresentInput& input) noexcept {
    return input.scene_ready && input.session_bound && !input.fps &&
        input.reason == ::kf2::telemetry::UnavailableReason::no_samples &&
        input.session_started_ns != 0 &&
        input.now_ns >= input.session_started_ns &&
        input.now_ns - input.session_started_ns >= kSilentPresentRestartNs &&
        input.restart_count < kMaximumPresentRestarts;
}

[[nodiscard]] constexpr SessionDisposition classify_session_gate(
    const SessionGateInput& input) noexcept {
    if (!input.process_bound) {
        return SessionDisposition::waiting_for_process;
    }
    if (!input.process_identity_matches ||
        (!input.window_ready && !input.same_process_running)) {
        return SessionDisposition::session_ended;
    }
    if (!input.window_ready) {
        return SessionDisposition::waiting_for_window;
    }
    if (!input.scene_ready || !input.present_source_bound) {
        return SessionDisposition::waiting_for_scene;
    }
    return input.reconnecting ? SessionDisposition::reconnecting
                              : SessionDisposition::ready;
}

void attach_session_sources(app::UiRuntime& runtime);
void refresh_session_gate(app::UiRuntime& runtime);
void revalidate_bound_process(app::UiRuntime& runtime);
[[nodiscard]] SessionStageResult inspect_bound_session(
    app::UiRuntime& runtime);

}  // namespace kf2::telemetry_pipeline
