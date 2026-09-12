#include <cstdlib>
#include <iostream>

#include "features/telemetry/telemetry_adaptive_stage.hpp"
#include "kf2/telemetry/present_source.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __LINE__ << ": " #condition << '\n'; return EXIT_FAILURE; \
} } while (false)

int main() {
    using namespace kf2;
    using namespace kf2::telemetry_pipeline;
    constexpr std::uint64_t receipt_ns = 20'000'000'000ULL;
    const telemetry::SampleIdentity identity{42, 9001};
    // A LoadMap interval can contain very slow presents for any duration.
    // Rebase at the first provider tick before admitting quality decisions.
    for (const bool recovered : {true, false}) {
        telemetry::PresentSource source{identity, 2048};
        CHECK(source.start().has_value());
        optimizer::AdaptiveGovernor governor;
        optimizer::AdaptivePolicy policy;
        policy.target_fps = 60;
        TelemetryFrame frame;
        frame.identity = identity;
        frame.adapter_luid = 77;
        frame.active_gameplay = true;
        frame.offline_gameplay = false;
        frame.gameplay.emplace();
        frame.gameplay->map = "KF-CastleVolter";
        AdaptiveSampleContext context;
        context.current_map = "KF-BioticsLab";
        context.last_telemetry_sample = 500;
        std::uint64_t boundary = 0;
        bool pressure = false;
        const auto ready_ns = receipt_ns + 40'000'000'000ULL;
        for (auto now = receipt_ns; now <= ready_ns + 5'000'000'000ULL;) {
            CHECK(source.ingest({identity, now, 1, true, 0}));
            frame.observed_at_ns = now;
            frame.frames = source.drain(now, 2'000'000'000ULL);
            if (now >= ready_ns) {
                frame.offline_gameplay = true;
                frame.gameplay->net_mode = "NM_Standalone";
                frame.gameplay->telemetry_sample = 1;
                frame.gameplay->telemetry_observed_ns = now;
            }
            context.decision_frames = source.drain(now, 2'000'000'000ULL, boundary);
            const auto built = build_adaptive_sample(frame, context);
            context.current_map = built.map;
            context.map_generation = built.map_generation;
            context.last_telemetry_sample = built.telemetry_sample;
            if (requires_fresh_frame_window(built)) {
                boundary = now;
                governor.reset();
                CHECK(now <= ready_ns);
            } else {
                CHECK(now > ready_ns);
                const auto decision = governor.evaluate(policy, built.sample, now);
                pressure = pressure || decision.current_frame_pressure;
                if (recovered) CHECK(!decision.current_frame_pressure);
            }
            now += now < ready_ns || !recovered ? 125'000'000ULL : 16'666'667ULL;
        }
        CHECK(pressure == !recovered);
    }
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
            context.last_telemetry_sample = 1;
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
            const auto observation_span = recovered
                ? 2'000'000'000ULL : 8'000'000'000ULL;
            for (std::uint64_t elapsed = interval;
                 elapsed <= observation_span; elapsed += interval) {
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
                // This scenario verifies the post-receipt response window,
                // not attribution. Supply an evidenced target so an unknown
                // resource cannot intentionally suppress the quality action.
                input.primary_resource = optimizer::ResourceKind::gpu;
                input.primary_confidence = 0.80;
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
                if (elapsed < 7'000'000'000ULL || recovered) CHECK(!next);
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
    // Escape/Trader transition frames remain available to the overlay, while
    // Adaptive resumes from a bounded gameplay-only window. A real stall that
    // occurs after that boundary must still be actionable.
    {
        telemetry::PresentSource source{identity, 2048};
        CHECK(source.start().has_value());
        const auto menu_open_ns = receipt_ns + 1'000'000'000ULL;
        const auto gameplay_return_ns = receipt_ns + 3'000'000'000ULL;
        for (auto at = receipt_ns; at < menu_open_ns;
             at += 16'666'667ULL) {
            CHECK(source.ingest({identity, at, 1, true, 0}));
        }
        for (auto at = menu_open_ns; at < gameplay_return_ns;
             at += 125'000'000ULL) {
            CHECK(source.ingest({identity, at, 1, true, 0}));
        }
        for (auto at = gameplay_return_ns + 16'666'667ULL;
             at <= gameplay_return_ns + 2'000'000'000ULL;
             at += 16'666'667ULL) {
            CHECK(source.ingest({identity, at, 1, true, 0}));
        }
        const auto now = gameplay_return_ns + 2'000'000'000ULL;
        const auto overlay_frames = source.drain(now, 2'000'000'000ULL);
        const auto adaptive_frames = source.drain(
            now, 2'000'000'000ULL, gameplay_return_ns);
        CHECK(overlay_frames.one_percent_low_fps.has_value());
        CHECK(*overlay_frames.one_percent_low_fps < 10.0);
        CHECK(adaptive_frames.one_percent_low_fps.has_value());
        CHECK(*adaptive_frames.one_percent_low_fps > 59.0);

        optimizer::AdaptiveGovernor governor;
        optimizer::AdaptivePolicy policy;
        policy.target_fps = 60;
        TelemetryFrame frame;
        frame.identity = identity;
        frame.observed_at_ns = now;
        frame.active_gameplay = true;
        frame.offline_gameplay = true;
        frame.frames = overlay_frames;
        frame.gameplay.emplace();
        frame.gameplay->map = "KF-Outpost";
        frame.gameplay->net_mode = "NM_Standalone";
        frame.gameplay->phase = game::GameLogPhase::map_loaded;
        frame.gameplay->telemetry_sample = 10;
        frame.gameplay->telemetry_observed_ns = now;
        AdaptiveSampleContext context;
        context.current_map = "KF-Outpost";
        context.map_generation = 1;
        context.last_telemetry_sample = 10;
        context.decision_frames = adaptive_frames;
        const auto clean = build_adaptive_sample(frame, context);
        CHECK(!governor.evaluate(policy, clean.sample, now)
                   .current_frame_pressure);

        bool gameplay_stall_detected = false;
        for (auto at = now + 125'000'000ULL;
             at <= now + 4'000'000'000ULL; at += 125'000'000ULL) {
            CHECK(source.ingest({identity, at, 1, true, 0}));
            frame.observed_at_ns = at;
            frame.frames = source.drain(at, 2'000'000'000ULL);
            frame.gameplay->telemetry_observed_ns = at;
            context.decision_frames = source.drain(
                at, 2'000'000'000ULL, gameplay_return_ns);
            const auto stalled = build_adaptive_sample(frame, context);
            gameplay_stall_detected = gameplay_stall_detected ||
                governor.evaluate(policy, stalled.sample, at)
                    .current_frame_pressure;
        }
        CHECK(gameplay_stall_detected);
    }
    // Gameplay regression: at target 50, sequence 87 reported live/average
    // 50.01, p95 20.93 ms, lows 46.28/45.35, and zero stutters. Merely
    // keeping these modest percentile deviations for 3.5 s must not request
    // the effects 20 -> 10 change seen in sequence 88.
    for (int target = 30; target <= 240; ++target) {
        // 0: historical mild tail, 1: Issue #64's 48-52 FPS lows at 60,
        // 2: severe historical tail alone; 3-6: moderate tail corroborated
        // respectively by current p95, live FPS, average FPS, or a counted
        // stutter; 7: independently catastrophic short and long tails.
        for (const int scenario : {0, 1, 2, 3, 4, 5, 6, 7}) {
            const bool actionable_tail = scenario >= 3;
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
            if (scenario == 1) frame.frames.p95_ms = 18.4 * 60.0 / target;
            if (scenario == 3) frame.frames.p95_ms = 20.0 * 60.0 / target;
            const double warning_fps = 1000.0 /
                optimizer::adaptive_stability_bands(target).warning_frame_time_ms - 0.1;
            if (scenario == 4) {
                frame.frames.fps = warning_fps;
                frame.frames.frame_time_ms = 1000.0 / *frame.frames.fps;
            }
            if (scenario == 5) frame.frames.average_fps = warning_fps;
            if (scenario == 6) frame.frames.stutter_count = 1;
            const double low_factor = scenario == 7 ? 0.45
                : scenario == 2 ? 0.73
                : scenario != 0 ? 0.80 : 46.28 / 50.0;
            frame.frames.sustained_one_percent_low_fps =
                target * low_factor;
            frame.frames.one_percent_low_fps = target *
                (scenario == 0 ? 45.35 / 50.0 : low_factor);
            AdaptiveSampleContext context;
            context.current_quality = 20;
            context.current_map = "KF-Outpost";
            context.map_generation = 1;
            context.last_telemetry_sample = 1;
            frame.gameplay->telemetry_sample = 1;
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
                if (!actionable_tail) {
                    CHECK(!decision.current_frame_pressure);
                    CHECK(!decision.quality_recovery_eligible);
                }
                AdaptiveRuntimeControlInput input;
                input.state = decision.state;
                input.data_quality = decision.data.quality;
                // Tail corroboration and resource attribution are independent
                // gates. Keep this integration scenario focused on the former.
                input.primary_resource = optimizer::ResourceKind::gpu;
                input.primary_confidence = 0.80;
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
                if (!actionable_tail) CHECK(!next);
                requested = requested || next.has_value();
            }
            if (requested != actionable_tail) {
                std::cerr << "target=" << target << " scenario=" << scenario << '\n';
            }
            CHECK(requested == actionable_tail);
        }
    }
    return EXIT_SUCCESS;
}
