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
    CHECK(should_rediscover_game_log(false, false, 10, 10));
    CHECK(should_rediscover_game_log(true, true, 10, 10));
    CHECK(should_rediscover_game_log(true, false, 10, 11));
    CHECK(!should_rediscover_game_log(true, false, 10, 10));
    CHECK(should_scan_for_game_process(100, 0, false,
        kIdleProcessDiscoveryIntervalNs));
    CHECK(!should_scan_for_game_process(
        kIdleProcessDiscoveryIntervalNs - 1, 1, false,
        kIdleProcessDiscoveryIntervalNs));
    CHECK(should_scan_for_game_process(
        kIdleProcessDiscoveryIntervalNs + 1, 1, false,
        kIdleProcessDiscoveryIntervalNs));
    CHECK(should_scan_for_game_process(10, 20, false,
        kMaximumIdleProcessDiscoveryIntervalNs));
    CHECK(!should_scan_for_game_process(100, 99, true,
        kMaximumIdleProcessDiscoveryIntervalNs));
    CHECK(should_scan_for_game_process(100, 0, true,
        kMaximumIdleProcessDiscoveryIntervalNs));
    CHECK(should_scan_for_game_process(kIdleProcessDiscoveryIntervalNs + 99,
        99, true, kMaximumIdleProcessDiscoveryIntervalNs));
    CHECK(!should_scan_for_game_process(kIdleProcessDiscoveryIntervalNs + 98,
        99, true, kMaximumIdleProcessDiscoveryIntervalNs));
    CHECK(!should_scan_for_game_process(kIdleProcessDiscoveryIntervalNs + 99,
        99, false, kMaximumIdleProcessDiscoveryIntervalNs));
    CHECK(should_scan_for_game_process(kMaximumIdleProcessDiscoveryIntervalNs + 99,
        99, false, kMaximumIdleProcessDiscoveryIntervalNs));
    CHECK(next_idle_process_discovery_interval_ns(500'000'000ULL) == 1'000'000'000ULL);
    CHECK(next_idle_process_discovery_interval_ns(1'000'000'000ULL) == 2'000'000'000ULL);
    CHECK(next_idle_process_discovery_interval_ns(2'000'000'000ULL) == 4'000'000'000ULL);
    CHECK(next_idle_process_discovery_interval_ns(4'000'000'000ULL) == 5'000'000'000ULL);
    CHECK(next_idle_process_discovery_interval_ns(5'000'000'000ULL) == 5'000'000'000ULL);
    CHECK(next_idle_process_discovery_interval_ns(UINT64_MAX) == 5'000'000'000ULL);
    // Count the policy-authorized process snapshots for a whole idle hour,
    // without waiting an hour or enumerating the test machine's processes.
    std::uint64_t idle_last_scan = 0;
    std::uint64_t idle_interval = kIdleProcessDiscoveryIntervalNs;
    unsigned int idle_scans = 0;
    for (std::uint64_t tick = 1; tick <= 7200; ++tick) {
        const auto now = tick * 500'000'000ULL;
        if (should_scan_for_game_process(now, idle_last_scan, false, idle_interval)) {
            idle_last_scan = now;
            idle_interval = next_idle_process_discovery_interval_ns(idle_interval);
            ++idle_scans;
        }
    }
    std::cout << "Idle hour: " << idle_scans << " scheduled process scans\n";
    CHECK(idle_scans <= 730);
    CHECK(idle_scans == 722);
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

    SilentPresentInput silent;
    silent.scene_ready = true;
    silent.session_started_ns = 10;
    silent.now_ns = 10 + kSilentPresentRestartNs - 1;
    CHECK(!should_reconnect_silent_present(silent));
    silent.now_ns++;
    CHECK(should_reconnect_silent_present(silent));
    silent.now_ns = 9;
    CHECK(!should_reconnect_silent_present(silent));
    silent.session_started_ns = 0;
    silent.now_ns = kSilentPresentRestartNs;
    CHECK(!should_reconnect_silent_present(silent));
    silent.session_started_ns = 10;
    silent.now_ns = 10 + kSilentPresentRestartNs;
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
