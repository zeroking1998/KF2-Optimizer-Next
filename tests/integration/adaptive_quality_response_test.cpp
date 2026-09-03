#include <cstdlib>
#include <iostream>

#include "features/telemetry/telemetry_adaptive_stage.hpp"
#include "kf2/telemetry/present_source.hpp"

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __LINE__ << ": " #condition << '\n'; return EXIT_FAILURE; \
} } while (false)

int main() {
    using namespace kf2;
    using namespace kf2::telemetry_pipeline;
    constexpr std::uint64_t receipt_ns = 20'000'000'000ULL;
    const telemetry::SampleIdentity identity{42, 9001};
    for (const int target : {30, 60, 86, 122, 211, 240}) {
        for (const bool recovered : {true, false}) {
            telemetry::PresentSource source{identity, 2048};
            CHECK(source.start().has_value());
            optimizer::AdaptiveGovernor governor;
            optimizer::AdaptivePolicy policy;
            policy.target_fps = target;
            TelemetryFrame frame;
            frame.identity = identity;
            frame.adapter_luid = 77;
            frame.active_gameplay = true;
            frame.offline_gameplay = true;
            frame.evidence.cpu_percent = 35.0;
            frame.evidence.gpu_percent = 80.0;
            frame.gameplay.emplace();
            frame.gameplay->map = "KF-Outpost";
            frame.gameplay->net_mode = "NM_Standalone";
            frame.gameplay->phase = game::GameLogPhase::map_loaded;
            AdaptiveSampleContext context;
            context.current_quality = 80;
            context.current_map = "KF-Outpost";
            context.map_generation = 1;
            optimizer::AdaptiveDecision before;
            // An earlier severe stall remains in the normal UI windows.
            for (std::uint64_t at = receipt_ns - 4'000'000'000ULL;
                 at <= receipt_ns; at += 125'000'000ULL) {
                CHECK(source.ingest({identity, at, 1, true, 0}));
                frame.observed_at_ns = at;
                frame.frames = source.drain(at, 2'000'000'000ULL);
                frame.gameplay->telemetry_observed_ns = frame.observed_at_ns;
                frame.gameplay->telemetry_sample = 1;
                const auto built = build_adaptive_sample(frame, context);
                before = governor.evaluate(policy, built.sample, at);
            }
            CHECK(before.state == optimizer::AdaptiveControllerState::emergency);
            CHECK(before.current_frame_pressure);
            // Model one accepted, authenticated quality receipt.
            governor.notify_quality_applied(receipt_ns);
            const auto interval = recovered
                ? 1'000'000'000ULL / static_cast<std::uint64_t>(target)
                : 125'000'000ULL;
            bool corrected_again = false;
            for (std::uint64_t elapsed = interval;
                 elapsed <= 2'000'000'000ULL; elapsed += interval) {
                const auto now = receipt_ns + elapsed;
                CHECK(source.ingest({identity, now, 1, true, 0}));
                frame.observed_at_ns = now;
                frame.frames = source.drain(now, 2'000'000'000ULL);
                frame.gameplay->telemetry_observed_ns = now;
                context.decision_frames = source.drain(
                    now, 2'000'000'000ULL, receipt_ns);
                const auto built = build_adaptive_sample(frame, context);
                const auto decision = governor.evaluate(policy, built.sample, now);
                AdaptiveRuntimeControlInput input;
                input.state = decision.state;
                input.data_quality = decision.data.quality;
                input.current_quality = 80;
                input.current_frame_pressure = decision.current_frame_pressure;
                input.current_resource_pressure = decision.current_resource_pressure;
                input.active_gameplay = true;
                input.verified_offline = true;
                input.bridge_available = true;
                input.now_ns = now;
                input.last_applied_ns = receipt_ns;
                input.sample_timestamp_ns = built.sample.timestamp_ns;
                const auto next = select_adaptive_runtime_control(input);
                if (elapsed < 1'000'000'000ULL || recovered) CHECK(!next);
                if (next) corrected_again = true;
            }
            CHECK(corrected_again == !recovered);
            if (recovered) {
                // The fix must not erase the user's historical low-FPS display.
                CHECK(frame.frames.one_percent_low_fps.has_value());
                CHECK(*frame.frames.one_percent_low_fps < 10.0);
                CHECK(*context.decision_frames->one_percent_low_fps >= target - 0.01);
            }
        }
    }
    // Gameplay regression: at target 50, sequence 87 reported live/average
    // 50.01, p95 20.93 ms, lows 46.28/45.35, and zero stutters. Merely
    // keeping these modest percentile deviations for 3.5 s must not request
    // the effects 20 -> 10 change seen in sequence 88.
    for (int target = 30; target <= 240; ++target) {
        for (const bool severe_tail : {false, true}) {
            optimizer::AdaptiveGovernor governor;
            optimizer::AdaptivePolicy policy;
            policy.target_fps = target;
            governor.notify_quality_applied(receipt_ns);
            TelemetryFrame frame;
            frame.identity = identity;
            frame.adapter_luid = 77;
            frame.active_gameplay = true;
            frame.offline_gameplay = true;
            frame.evidence.cpu_percent = 2.02;
            frame.evidence.gpu_percent = 66.63;
            frame.evidence.process_gpu_percent = 66.63;
            frame.gameplay.emplace();
            frame.gameplay->map = "KF-Outpost";
            frame.gameplay->net_mode = "NM_Standalone";
            frame.gameplay->phase = game::GameLogPhase::map_loaded;
            frame.frames.quality = telemetry::SampleQuality::good;
            frame.frames.fps = target * (50.01 / 50.0);
            frame.frames.average_fps = frame.frames.fps;
            frame.frames.frame_time_ms = 1000.0 / *frame.frames.fps;
            frame.frames.p95_ms = 20.93 * 50.0 / target;
            frame.frames.sustained_one_percent_low_fps =
                target * (severe_tail ? 0.80 : 46.28 / 50.0);
            frame.frames.one_percent_low_fps =
                target * (severe_tail ? 0.80 : 45.35 / 50.0);
            AdaptiveSampleContext context;
            context.current_quality = 20;
            context.current_map = "KF-Outpost";
            context.map_generation = 1;
            bool requested = false;
            for (std::uint64_t elapsed = 200'000'000ULL;
                 elapsed <= 8'000'000'000ULL; elapsed += 200'000'000ULL) {
                const auto now = receipt_ns + elapsed;
                frame.observed_at_ns = now;
                frame.gameplay->telemetry_observed_ns = now;
                const auto built = build_adaptive_sample(frame, context);
                const auto decision = governor.evaluate(policy, built.sample, now);
                CHECK(decision.data.quality == optimizer::AdaptiveDataQuality::valid);
                CHECK(!decision.current_resource_pressure);
                if (!severe_tail) {
                    CHECK(!decision.current_frame_pressure);
                    CHECK(!decision.quality_recovery_eligible);
                }
                AdaptiveRuntimeControlInput input;
                input.state = decision.state;
                input.data_quality = decision.data.quality;
                input.current_quality = 20;
                input.current_frame_pressure = decision.current_frame_pressure;
                input.current_resource_pressure = decision.current_resource_pressure;
                input.active_gameplay = true;
                input.verified_offline = true;
                input.bridge_available = true;
                input.now_ns = now;
                input.last_applied_ns = receipt_ns;
                input.sample_timestamp_ns = built.sample.timestamp_ns;
                const auto next = select_adaptive_runtime_control(input);
                if (!severe_tail) CHECK(!next);
                requested = requested || next.has_value();
            }
            CHECK(requested == severe_tail);
        }
    }
    return EXIT_SUCCESS;
}
