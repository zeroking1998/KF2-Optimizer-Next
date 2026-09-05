#include <cstdlib>
#include <iostream>
#include "kf2/optimizer/quality_response.hpp"
#define CHECK(x) do { if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; return EXIT_FAILURE; } } while(false)

int main() {
    using kf2::optimizer::QualityResponse;
    using kf2::telemetry::PresentSource;
    const kf2::telemetry::SampleIdentity id{42, 123};
    constexpr std::uint64_t second = 1'000'000'000ULL;
    const QualityResponse::Context context{id, "KF-Test", 5, 60, 20, 40, true};
    // Real ETW delivery can lag the event clock by 250 ms. Preserve exact
    // event-time bounds and let the missing tail arrive before judging it.
    for (const auto lag : {second / 4, second / 2, second * 3 / 4}) {
        PresentSource delayed_source{id, 4096};
        CHECK(delayed_source.start().has_value());
        QualityResponse delayed;
        for (auto at = 4 * second; at <= 10 * second; at += second / 4)
            CHECK(!delayed.observe(context, at));
        for (auto at = 5 * second; at <= 10 * second - lag; at += 25'000'000ULL)
            CHECK(delayed_source.ingest({id, at, 1, true, 0, 7}));
        auto before = delayed_source.measure_window(5 * second, 10 * second);
        CHECK(!before.complete);
        CHECK(before.span_ns == 5 * second - lag);
        delayed.begin(20, "mixed", 100, 90, 10 * second, context, before);
        delayed.confirm(20, 10 * second);
        for (auto now = 10 * second + second / 4; now <= 16 * second + lag + second / 4; now += second / 4) {
            for (auto at = now - lag - second / 4 + 25'000'000ULL; at <= now - lag; at += 25'000'000ULL)
                CHECK(delayed_source.ingest({id, at, 1, true, 0, 7}));
            // Runtime integration refreshes only the fixed baseline interval.
            if (now <= 11 * second)
                delayed.refresh_baseline(now, delayed_source.measure_window(5 * second, 10 * second));
            auto after = delayed_source.measure_window(11 * second, 16 * second);
            const auto result = delayed.observe(context, now, after);
            if (now == 16 * second + lag) {
                CHECK(result && result->result == "no_clear_change");
                CHECK(result->before.span_ns == 5 * second);
                CHECK(result->after.span_ns == 5 * second);
            } else CHECK(!result);
        }
    }
    for (const int scenario : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}) {
        PresentSource source{id, 4096};
        CHECK(source.start().has_value());
        for (auto at = 5 * second; at <= 10 * second; at += 25'000'000ULL)
            CHECK(source.ingest({id, at, 1, true, 0, 7}));
        const auto before = source.measure_window(5 * second, 10 * second);
        CHECK(before.complete);
        CHECK(before.count == 201);
        CHECK(std::abs(*before.metrics.average_fps - 40.0) < 0.001);
        CHECK(!source.measure_window(1 * second, 10 * second).complete);
        QualityResponse tracker;
        for (auto at = 4 * second; at <= 10 * second; at += second / 4)
            CHECK(!tracker.observe(context, at));
        auto baseline = before;
        if (scenario == 8) baseline.complete = false;
        tracker.begin(12, "mixed", 100, 90, 10 * second, context, baseline);
        tracker.confirm(11, 10 * second);
        CHECK(tracker.end_ns() == 0); // Wrong receipt cannot start an evaluation.
        if (scenario != 3) tracker.confirm(12, 10 * second);
        if (scenario == 8) tracker.refresh_baseline(11 * second + second / 4, before);
        if (scenario == 13) source.reset_statistics();
        if (scenario == 14) {
            CHECK(source.ingest({id, 10 * second + second / 4, 1, true, 0, 8}));
            CHECK(source.ingest({id, 10 * second + second / 2, 1, true, 0, 7}));
        }
        const auto interval = scenario == 0 ? 20'000'000ULL :
            scenario == 2 ? 40'000'000ULL : 25'000'000ULL;
        for (auto at = 11 * second; at <= 16 * second; at += interval)
            CHECK(source.ingest({id, at, 1, true, 0, scenario == 6 ? 8ULL : 7ULL}));
        auto after = source.measure_window(11 * second, 16 * second);
        CHECK(after.complete || scenario == 6);
        if (scenario == 9) after.complete = false;
        auto current = context;
        std::optional<QualityResponse::Report> result;
        for (auto at = 10 * second + second / 4; at <= 17 * second; at += second / 4) {
            if (scenario == 10 && at > 12 * second && at < 13 * second) continue;
            if (scenario == 4 && at >= 12 * second) current.map = "KF-Other";
            if (scenario == 5 && at >= 12 * second) current.living = 100;
            if (scenario == 7 && at >= 12 * second) current.ready = false;
            if (scenario == 11 && at >= 12 * second) current.target = 120;
            if (scenario == 12 && at >= 12 * second) current.adapter = 9;
            if (auto report = tracker.observe(current, at, after)) result = report;
        }
        if (scenario == 3) CHECK(!result);
        else {
            CHECK(result);
            CHECK(result->sequence == 12);
            const std::string expected = scenario == 0 ? "improved" :
                scenario == 1 ? "no_clear_change" : scenario == 2 ? "worsened" :
                scenario == 5 ? "inconclusive:scene_changed_or_unknown" :
                scenario == 6 ? "inconclusive:incomplete_post_window" :
                scenario >= 13 ? "inconclusive:present_source_interrupted" :
                scenario == 8 ? "inconclusive:incomplete_baseline" :
                scenario == 9 ? "inconclusive:incomplete_post_window" :
                scenario == 10 ? "inconclusive:observation_gap" :
                "inconclusive:context_changed";
            CHECK(result->result == expected);
        }
        CHECK(!tracker.cancel("end"));
        CHECK(source.ingest({id, 17 * second, 1, true, 1, 7}));
        CHECK(!source.measure_window(11 * second, 16 * second).complete);
    }
    QualityResponse superseded;
    for (auto at = second; at <= 10 * second; at += second / 4)
        CHECK(!superseded.observe(context, at));
    superseded.begin(1, "mixed", 100, 90, 10 * second, context, {});
    CHECK(!superseded.cancel("failed_request"));
    superseded.begin(2, "mixed", 100, 90, 10 * second, context, {});
    superseded.confirm(2, 10 * second);
    const auto canceled = superseded.cancel("superseded_by_next_action");
    CHECK(canceled && canceled->result == "inconclusive:superseded_by_next_action");
    CHECK(!superseded.cancel("duplicate"));
    QualityResponse baseline_scene;
    auto changing = context;
    for (auto at = 4 * second; at <= 10 * second; at += second / 4) {
        changing.living = at == 4 * second ? 20 : at < 10 * second ? 17 : 24;
        CHECK(!baseline_scene.observe(changing, at));
    }
    PresentSource::Window complete;
    complete.complete = true;
    baseline_scene.begin(3, "mixed", 100, 90, 10 * second, changing, complete);
    baseline_scene.confirm(3, 10 * second);
    std::optional<QualityResponse::Report> scene_report;
    for (auto at = 10 * second + second / 4; at <= 16 * second; at += second / 4)
        if (auto r = baseline_scene.observe(changing, at, complete)) scene_report = r;
    CHECK(scene_report && scene_report->result == "inconclusive:unstable_baseline_scene");
    for (const bool mixed : {false, true}) {
        QualityResponse delayed;
        for (auto at = second; at <= 10 * second; at += second / 4)
            CHECK(!delayed.observe(context, at));
        auto baseline = complete;
        baseline.metrics.average_fps = 40;
        baseline.metrics.p95_ms = 30;
        baseline.metrics.one_percent_low_fps = 25;
        delayed.begin(4, "mixed", 100, 90, 10 * second, context, baseline);
        delayed.confirm(4, 12 * second);
        delayed.confirm(4, 13 * second); // Duplicate must not move the deadline.
        CHECK(delayed.end_ns() == 18 * second);
        auto post = baseline;
        post.metrics.average_fps = 50;
        post.metrics.p95_ms = mixed ? 40 : 25;
        post.metrics.one_percent_low_fps = 30;
        for (auto at = 10 * second + second / 4; at < 18 * second; at += second / 4)
            CHECK(!delayed.observe(context, at, post));
        auto result = delayed.observe(context, 18 * second, post);
        CHECK(result && result->result == (mixed ? "mixed" : "improved"));
        CHECK(!delayed.observe(context, 18 * second + second / 4, post));
    }
    return EXIT_SUCCESS;
}
