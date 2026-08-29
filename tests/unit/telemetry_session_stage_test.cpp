#include <cstdlib>
#include <iostream>

#include "features/telemetry/telemetry_session_stage.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using namespace kf2::telemetry_pipeline;
    CHECK(game_restart_handoff_timeout_ns(false) == 10'000'000'000ULL);
    CHECK(game_restart_handoff_timeout_ns(true) == 300'000'000'000ULL);
    SessionGateInput input;
    CHECK(classify_session_gate(input) ==
          SessionDisposition::waiting_for_process);

    input.process_bound = true;
    input.same_process_running = true;
    CHECK(classify_session_gate(input) ==
          SessionDisposition::waiting_for_window);

    input.same_process_running = false;
    CHECK(classify_session_gate(input) ==
          SessionDisposition::session_ended);

    input.same_process_running = true;
    input.window_ready = true;
    CHECK(classify_session_gate(input) ==
          SessionDisposition::waiting_for_scene);

    input.scene_ready = true;
    CHECK(classify_session_gate(input) ==
          SessionDisposition::waiting_for_scene);

    input.present_source_bound = true;
    CHECK(classify_session_gate(input) == SessionDisposition::ready);

    input.reconnecting = true;
    CHECK(classify_session_gate(input) == SessionDisposition::reconnecting);

    input.reconnecting = false;
    input.process_identity_matches = false;
    CHECK(classify_session_gate(input) ==
          SessionDisposition::session_ended);

    CHECK(classify_bound_process_transition(true, true) ==
          BoundProcessTransition::same_process);
    CHECK(classify_bound_process_transition(false, true) ==
          BoundProcessTransition::replacement_process);
    CHECK(classify_bound_process_transition(false, false) ==
          BoundProcessTransition::ended);

    SilentPresentInput silent;
    silent.scene_ready = true;
    silent.session_bound = true;
    silent.session_started_ns = 10;
    silent.now_ns = 10 + kSilentPresentRestartNs - 1;
    CHECK(!should_reconnect_silent_present(silent));
    silent.now_ns++;
    CHECK(should_reconnect_silent_present(silent));
    silent.restart_count = kMaximumPresentRestarts;
    CHECK(!should_reconnect_silent_present(silent));
    silent.restart_count = 0;
    silent.fps = 1.0;
    CHECK(!should_reconnect_silent_present(silent));
    silent.fps.reset();
    silent.reason = kf2::telemetry::UnavailableReason::stale;
    CHECK(!should_reconnect_silent_present(silent));
    silent.reason = kf2::telemetry::UnavailableReason::discontinuity;
    CHECK(!should_reconnect_silent_present(silent));
    silent.reason = kf2::telemetry::UnavailableReason::source_failure;
    CHECK(!should_reconnect_silent_present(silent));
    silent.reason = kf2::telemetry::UnavailableReason::no_samples;
    silent.scene_ready = false;
    CHECK(!should_reconnect_silent_present(silent));

    RestartHandoffInput restart;
    CHECK(classify_restart_handoff(restart) ==
          RestartHandoffDisposition::inactive);
    restart.pending = true;
    restart.deadline_ns = kGameRestartHandoffNs;
    restart.now_ns = kGameRestartHandoffNs - 1;
    CHECK(classify_restart_handoff(restart) ==
          RestartHandoffDisposition::waiting);
    restart.verified_process_found = true;
    restart.same_process_identity = true;
    CHECK(classify_restart_handoff(restart) ==
          RestartHandoffDisposition::previous_process_resumed);
    restart.same_process_identity = false;
    CHECK(classify_restart_handoff(restart) ==
          RestartHandoffDisposition::replacement_found);
    restart.now_ns = kGameRestartHandoffNs + 1;
    CHECK(classify_restart_handoff(restart) ==
          RestartHandoffDisposition::replacement_found);
    restart.verified_process_found = false;
    restart.now_ns = kGameRestartHandoffNs - 1;
    CHECK(classify_restart_handoff(restart) ==
          RestartHandoffDisposition::waiting);
    restart.now_ns = kGameRestartHandoffNs;
    CHECK(classify_restart_handoff(restart) ==
          RestartHandoffDisposition::expired);
    return EXIT_SUCCESS;
}
