#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

#include "features/telemetry/telemetry_adaptive_stage.hpp"
#include "kf2/telemetry/present_source.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << __LINE__ << ": " #condition << '\n'; return EXIT_FAILURE; \
} } while (false)

namespace {

template <typename Configure>
void replace_gameplay(
    kf2::telemetry_pipeline::TelemetryFrame& frame,
    Configure&& configure) {
    auto session = frame.gameplay
        ? *frame.gameplay : kf2::game::GameLogSession{};
    configure(session);
    frame.gameplay = kf2::game::make_game_log_session_snapshot(
        std::move(session));
}

}  // namespace

int test_present_loss_recovery() {
    using namespace kf2;
    using namespace kf2::telemetry_pipeline;
    constexpr std::uint64_t second = 1'000'000'000ULL;
    constexpr std::uint64_t step = 25'000'000ULL;
    const telemetry::SampleIdentity identity{42, 9001};
    for (const bool completed_loss : {true, false}) {
        telemetry::PresentSource source{identity, 4096};
        CHECK(source.start().has_value());
        const auto append = [&](std::uint64_t begin, std::uint64_t end) {
            for (auto at = begin; at <= end; at += step)
                if (!source.ingest({identity, at, 1, true, 0, 7})) return false;
            return true;
        };
        CHECK(append(10 * second, 12 * second));
        const auto before = source.measure_window(10 * second, 12 * second);
        CHECK(before.complete);
        constexpr auto loss_ns = 12 * second + step;
        source.request_drain(12 * second, second / 2);
        CHECK(source.wait_for_drain(std::chrono::seconds{2}));
        CHECK(source.latest_drain()->quality == telemetry::SampleQuality::good);
        CHECK(source.ingest({identity, loss_ns, 1, completed_loss, 4, 7}) ==
            completed_loss);
        const auto lossy = source.drain(loss_ns, second / 2);
        CHECK(lossy.quality == telemetry::SampleQuality::degraded);
        CHECK(lossy.loss_count >= 4);
        CHECK(!source.measure_window(10 * second, loss_ns).complete);

        TelemetryFrame frame;
        frame.identity = identity;
        frame.adapter_luid = 77;
        frame.active_gameplay = true;
        frame.offline_gameplay = true;
        AdaptiveSampleContext context;
        context.current_map = "KF-Test";
        context.last_telemetry_sample = 1;
        const auto sample = [&](const telemetry::FrameMetrics& metrics,
                                std::uint64_t now) {
            frame.observed_at_ns = now;
            frame.frames = metrics;
            replace_gameplay(frame, [&](auto& gameplay) {
                gameplay.map = "KF-Test";
                gameplay.net_mode = "NM_Standalone";
                gameplay.telemetry_sample = 1;
                gameplay.telemetry_observed_ns = now;
            });
            return build_adaptive_sample(frame, context).sample;
        };
        optimizer::AdaptivePolicy policy;
        const auto loss_sample = sample(lossy, loss_ns);
        CHECK(loss_sample.sample_loss);
        CHECK(optimizer::validate_adaptive_sample(policy, loss_sample, loss_ns).
            quality == optimizer::AdaptiveDataQuality::degraded);
        optimizer::AdaptiveGovernor governor;
        const auto loss_decision = governor.evaluate(policy, loss_sample, loss_ns);
        CHECK(loss_decision.data.quality == optimizer::AdaptiveDataQuality::degraded);
        CHECK(!loss_decision.proposed_value);

        constexpr auto fresh_begin = loss_ns + step;
        constexpr auto fresh_end = fresh_begin + second;
        CHECK(append(fresh_begin, fresh_end));
        CHECK(source.drain(fresh_end, second / 2).loss_count >= 4);
        CHECK(source.drain(fresh_end, second / 2, loss_ns).loss_count >= 4);
        const auto fresh = source.drain(fresh_end, second / 2, fresh_begin);
        CHECK(fresh.quality == telemetry::SampleQuality::good);
        CHECK(fresh.loss_count == 0);
        // Loss revokes old cached publications and fixed-window comparisons.
        CHECK(source.wait_for_drain(std::chrono::seconds{2}));
        CHECK(!source.latest_drain());
        const auto fresh_window = source.measure_window(fresh_begin, fresh_end);
        CHECK(fresh_window.complete);
        CHECK(fresh_window.generation != before.generation);
        CHECK(!source.measure_window(loss_ns, fresh_end).complete);
        const auto fresh_sample = sample(fresh, fresh_end);
        CHECK(!fresh_sample.sample_loss);
        CHECK(optimizer::validate_adaptive_sample(policy, fresh_sample, fresh_end).
            quality == optimizer::AdaptiveDataQuality::valid);
        CHECK(governor.evaluate(policy, fresh_sample, fresh_end).data.quality ==
            optimizer::AdaptiveDataQuality::valid);
        source.request_drain(fresh_end, second / 2, fresh_begin);
        CHECK(source.wait_for_drain(std::chrono::seconds{2}));
        CHECK(source.latest_drain(fresh_begin)->loss_count == 0);

        // No reset: the longest UI window recovers once it is wholly fresh.
        constexpr auto recovered_end = loss_ns +
            telemetry::PresentSource::longest_window_ns + 2 * step;
        CHECK(append(fresh_end + step, recovered_end));
        const auto recovered = source.drain(recovered_end, second / 2);
        CHECK(recovered.quality == telemetry::SampleQuality::good);
        CHECK(recovered.loss_count == 0);
        CHECK(recovered.one_percent_low_fps == fresh.one_percent_low_fps);
        CHECK(source.drain(recovered_end + second, second / 2).reason ==
            telemetry::UnavailableReason::stale);
        const auto generation = source.measure_window(
            recovered_end - second, recovered_end).generation;
        CHECK(!source.ingest({{99, 88}, recovered_end + step, 1, true, 5, 7}));
        CHECK(source.measure_window(recovered_end - second, recovered_end).
            generation == generation);
        CHECK(source.drain(recovered_end, second / 2).loss_count == 0);

        // Duplicate timestamps cannot hide loss; counters must not wrap to
        // zero and certify affected data when the reported count saturates.
        constexpr auto maximum_loss = std::numeric_limits<std::uint64_t>::max();
        CHECK(!source.ingest({identity, recovered_end, 1, true, maximum_loss, 7}));
        CHECK(!source.ingest({identity, recovered_end, 1, true, 1, 7}));
        CHECK(source.drain(recovered_end, second / 2).loss_count == maximum_loss);

        // Late/missing timestamps cannot move the loss fence behind already
        // admitted samples or let another retained swapchain bypass it.
        CHECK(source.ingest({identity, recovered_end + step, 1, true, 0, 8}));
        for (const auto late_ns : {loss_ns, std::uint64_t{0}}) {
            CHECK(!source.ingest({identity, late_ns, 1, false, 0, 7}));
            CHECK(source.drain(recovered_end, second / 2).quality ==
                telemetry::SampleQuality::degraded);
            CHECK(!source.measure_window(recovered_end - second,
                recovered_end).complete);
        }
        CHECK(append(recovered_end + step, recovered_end + second));
        CHECK(source.drain(recovered_end + second, second / 2,
            recovered_end).loss_count > 0);
        CHECK(source.drain(recovered_end + second, second / 2,
            recovered_end + step).loss_count > 0);
        CHECK(source.drain(recovered_end + second, second / 2,
            recovered_end + 2 * step).loss_count == 0);

        // Existing reset/bind/start boundaries clear both count and fence.
        source.reset_statistics();
        CHECK(append(10 * second, 11 * second));
        CHECK(source.drain(11 * second, second / 2).quality ==
            telemetry::SampleQuality::good);
        CHECK(source.ingest({identity, 11 * second + step, 1, true, 1, 7}));
        source.bind(identity);
        CHECK(append(10 * second, 11 * second));
        CHECK(source.measure_window(10 * second, 11 * second).complete);
        CHECK(source.ingest({identity, 11 * second + step, 1, true, 1, 7}));
        CHECK(source.stop().has_value());
        CHECK(source.start().has_value());
        CHECK(append(10 * second, 11 * second));
        CHECK(source.drain(11 * second, second / 2).loss_count == 0);
    }
    return EXIT_SUCCESS;
}

int test_duplicate_present_observations() {
    using namespace kf2;
    using namespace kf2::telemetry_pipeline;
    constexpr std::uint64_t second = 1'000'000'000ULL;
    const telemetry::SampleIdentity identity{42, 9001};
    telemetry::PresentSource source{identity, 128};
    CHECK(source.start().has_value());
    for (auto at = 10 * second; at <= 11 * second; at += 20'000'000ULL)
        CHECK(source.ingest({identity, at, 1, true, 0, 7}));
    source.request_drain(11 * second, 2 * second);
    CHECK(source.wait_for_drain(std::chrono::seconds{2}));
    TelemetryFrame frame;
    frame.identity = identity;
    frame.adapter_luid = 77;
    frame.active_gameplay = true;
    frame.offline_gameplay = true;
    frame.observed_at_ns = 11 * second;
    auto published = source.latest_drain();
    CHECK(published.has_value());
    frame.frames = *published;
    AdaptiveSampleContext context;
    optimizer::AdaptivePolicy policy;
    optimizer::AdaptiveGovernor governor;
    auto built = build_adaptive_sample(frame, context);
    static_cast<void>(governor.evaluate(policy, built.sample, frame.observed_at_ns));
    // Cached drain age remains zero. Reading it later must not invent a new
    // Present timestamp, and an unchanging frame cannot trigger a reduction.
    frame.observed_at_ns += second / 2;
    frame.evidence.cpu_percent = 99.0;
    built = build_adaptive_sample(frame, context);
    CHECK(built.sample.timestamp_ns == 11 * second);
    const auto repeated = governor.evaluate(policy, built.sample, frame.observed_at_ns);
    CHECK(repeated.reason == "duplicate_frame_observation_hold");
    CHECK(repeated.disposition == optimizer::AdaptiveDisposition::hold);
    frame.observed_at_ns = 11 * second + policy.freshness_limit_ns + 1;
    built = build_adaptive_sample(frame, context);
    CHECK(governor.evaluate(policy, built.sample, frame.observed_at_ns).
          reason == "stale_telemetry");
    // A newly published drain with no new Present has the same identity too.
    source.request_drain(11 * second + second / 2, 2 * second);
    CHECK(source.wait_for_drain(std::chrono::seconds{2}));
    published = source.latest_drain();
    CHECK(published.has_value());
    frame.frames = *published;
    frame.observed_at_ns += 1;
    CHECK(build_adaptive_sample(frame, context).sample.timestamp_ns == 11 * second);
    const auto resumed_ns = frame.observed_at_ns;
    for (int index = 0; index <= 50; ++index)
        CHECK(source.ingest({identity,
            resumed_ns + index * 20'000'000ULL, 1, true, 0, 7}));
    frame.observed_at_ns = resumed_ns + second;
    source.request_drain(frame.observed_at_ns, 2 * second);
    CHECK(source.wait_for_drain(std::chrono::seconds{2}));
    published = source.latest_drain();
    CHECK(published.has_value());
    frame.frames = *published;
    built = build_adaptive_sample(frame, context);
    CHECK(built.sample.timestamp_ns == frame.observed_at_ns);
    CHECK(governor.evaluate(policy, built.sample, frame.observed_at_ns).
          reason != "duplicate_frame_observation_hold");
    return EXIT_SUCCESS;
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view{argv[1]} == "--present-loss-recovery")
        return test_present_loss_recovery();
    CHECK(test_duplicate_present_observations() == EXIT_SUCCESS);
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
        replace_gameplay(frame, [](auto& gameplay) {
            gameplay.map = "KF-CastleVolter";
        });
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
                replace_gameplay(frame, [&](auto& gameplay) {
                    gameplay.net_mode = "NM_Standalone";
                    gameplay.telemetry_sample = 1;
                    gameplay.telemetry_observed_ns = now;
                });
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
            replace_gameplay(frame, [](auto& gameplay) {
                gameplay.map = "KF-Outpost";
                gameplay.net_mode = "NM_Standalone";
                gameplay.phase = game::GameLogPhase::map_loaded;
            });
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
                replace_gameplay(frame, [&](auto& gameplay) {
                    gameplay.telemetry_observed_ns = frame.observed_at_ns;
                    gameplay.telemetry_sample = 1;
                });
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
                replace_gameplay(frame, [&](auto& gameplay) {
                    gameplay.telemetry_observed_ns = now;
                });
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
        replace_gameplay(frame, [&](auto& gameplay) {
            gameplay.map = "KF-Outpost";
            gameplay.net_mode = "NM_Standalone";
            gameplay.phase = game::GameLogPhase::map_loaded;
            gameplay.telemetry_sample = 10;
            gameplay.telemetry_observed_ns = now;
        });
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
            replace_gameplay(frame, [&](auto& gameplay) {
                gameplay.telemetry_observed_ns = at;
            });
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
            replace_gameplay(frame, [](auto& gameplay) {
                gameplay.map = "KF-Outpost";
                gameplay.net_mode = "NM_Standalone";
                gameplay.phase = game::GameLogPhase::map_loaded;
            });
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
            replace_gameplay(frame, [](auto& gameplay) {
                gameplay.telemetry_sample = 1;
            });
            bool requested = false;
            for (std::uint64_t elapsed = 200'000'000ULL;
                 elapsed <= 8'000'000'000ULL; elapsed += 200'000'000ULL) {
                const auto now = receipt_ns + elapsed;
                frame.observed_at_ns = now;
                frame.frames.newest_present_ns = now;
                replace_gameplay(frame, [&](auto& gameplay) {
                    gameplay.telemetry_observed_ns = now;
                });
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
