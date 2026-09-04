#pragma once

#include <cmath>
#include <deque>
#include <optional>
#include <string>
#include "kf2/telemetry/present_source.hpp"

namespace kf2::optimizer {

// Observational diagnostics only: no decisions, quality changes or causal claim.
class QualityResponse final {
public:
    static constexpr std::uint64_t window_ns = 5'000'000'000ULL;
    static constexpr std::uint64_t settle_ns = 1'000'000'000ULL;
    static constexpr std::uint64_t delivery_grace_ns = 1'000'000'000ULL;
    struct Context {
        telemetry::SampleIdentity identity;
        std::string map;
        std::optional<std::uint64_t> adapter;
        int target{0};
        std::optional<int> living;
        std::optional<int> corpses;
        bool ready{false};
    };
    struct Report {
        std::uint64_t sequence{0};
        std::string resource;
        int from{0}, to{0};
        std::string result;
        telemetry::PresentSource::Window before, after;
    };

    void begin(std::uint64_t sequence, std::string resource, int from, int to,
               std::uint64_t requested, Context context,
               telemetry::PresentSource::Window before) {
        pending_ = Pending{{sequence, std::move(resource), from, to, {}, before, {}},
                           std::move(context), requested, 0, {}};
        if (!pending_->context.ready || !before.complete)
            pending_->invalid = "incomplete_baseline";
        if (!context_since_ || requested < context_since_ ||
            requested - context_since_ < window_ns)
            pending_->invalid = "unstable_baseline_context";
        for (const auto& entry : scene_history_) {
            if (requested >= window_ns && entry.first < requested - window_ns) continue;
            const auto similar = [](auto a, auto b) {
                return a && b && std::abs(static_cast<double>(*a) - *b) <=
                    std::max(2.0, static_cast<double>(*a) * 0.20);
            };
            if (!similar(pending_->context.living, entry.second.living) ||
                !similar(pending_->context.corpses, entry.second.corpses))
                pending_->invalid = "unstable_baseline_scene";
        }
    }
    void confirm(std::uint64_t sequence, std::uint64_t at) {
        if (pending_ && pending_->report.sequence == sequence &&
            at >= pending_->requested && !pending_->applied) pending_->applied = at;
    }
    [[nodiscard]] std::uint64_t baseline_end_ns() const {
        return pending_ && pending_->invalid == "incomplete_baseline"
            ? pending_->requested : 0;
    }
    void refresh_baseline(std::uint64_t now, const telemetry::PresentSource::Window& before) {
        if (!pending_ || pending_->invalid != "incomplete_baseline" ||
            now < pending_->requested || now - pending_->requested > delivery_grace_ns ||
            !before.complete || before.generation != pending_->report.before.generation ||
            before.stream_id != pending_->report.before.stream_id) return;
        pending_->report.before = before;
        pending_->invalid.clear();
    }
    [[nodiscard]] std::optional<Report> cancel(std::string reason) {
        if (!pending_) return {};
        auto saved = std::move(*pending_);
        pending_.reset();
        if (!saved.applied) return {}; // Never report an unconfirmed action.
        saved.report.result = "inconclusive:" + reason;
        return saved.report;
    }
    [[nodiscard]] std::uint64_t end_ns() const {
        return pending_ && pending_->applied
            ? pending_->applied + settle_ns + window_ns : 0;
    }
    [[nodiscard]] std::optional<Report> observe(
        const Context& current, std::uint64_t now,
        const telemetry::PresentSource::Window& after = {}) {
        const auto similar = [](auto a, auto b) {
            return a && b && *a >= 0 && *b >= 0 &&
                std::abs(static_cast<double>(*a) - *b) <=
                    std::max(2.0, static_cast<double>(*a) * 0.20);
        };
        if (!current.ready || !current.living || !current.corpses) {
            context_since_ = 0;
        } else if (!context_since_ || now < last_observed_ ||
                   now - last_observed_ > 500'000'000ULL ||
                   current.identity != baseline_context_.identity ||
                   current.map != baseline_context_.map ||
                   current.adapter != baseline_context_.adapter ||
                   current.target != baseline_context_.target ||
                   !similar(current.living, baseline_context_.living) ||
                   !similar(current.corpses, baseline_context_.corpses)) {
            context_since_ = now;
            baseline_context_ = current;
        }
        const bool sampling_gap = last_observed_ &&
            (now < last_observed_ || now - last_observed_ > 500'000'000ULL);
        last_observed_ = now;
        scene_history_.emplace_back(now, current);
        while (scene_history_.size() > 1 && now >= window_ns &&
               scene_history_[1].first < now - window_ns) scene_history_.pop_front();
        if (scene_history_.size() > 256) {
            scene_history_.pop_front();
            context_since_ = now; // Do not accept a truncated baseline.
        }
        if (!pending_) return {};
        if (sampling_gap) pending_->invalid = "observation_gap";
        const auto& initial = pending_->context;
        if (!current.ready || current.identity != initial.identity ||
            current.map != initial.map || current.adapter != initial.adapter ||
            current.target != initial.target || now < pending_->requested) {
            if (!pending_->applied) {
                pending_->invalid = "context_changed";
                return {};
            }
            return cancel("context_changed");
        }
        if (!similar(initial.living, current.living) ||
            !similar(initial.corpses, current.corpses))
            pending_->invalid = "scene_changed_or_unknown";
        if (!pending_->applied || now < end_ns()) return {};
        if (!pending_->invalid.empty()) return cancel(pending_->invalid);
        // ETW batches can arrive after the fixed event-time window ends.
        // Wait boundedly, without shortening or moving either measurement.
        if (!after.complete && now - end_ns() < delivery_grace_ns) return {};
        if (now - end_ns() > delivery_grace_ns || !after.complete)
            return cancel("incomplete_post_window");
        if (pending_->report.before.stream_id != after.stream_id)
            return cancel("present_stream_changed");
        if (pending_->report.before.generation != after.generation)
            return cancel("present_source_interrupted");
        auto report = pending_->report;
        pending_.reset();
        report.after = after;
        const auto& a = report.before.metrics;
        const auto& b = after.metrics;
        if (!a.p95_ms || !b.p95_ms || !a.one_percent_low_fps ||
            !b.one_percent_low_fps || !a.average_fps || !b.average_fps) {
            report.result = "inconclusive:missing_metrics";
        } else {
            const double p95_gain = (*a.p95_ms - *b.p95_ms) / *a.p95_ms;
            const double low_gain = (*b.one_percent_low_fps - *a.one_percent_low_fps) /
                *a.one_percent_low_fps;
            const double avg_gain = (*b.average_fps - *a.average_fps) / *a.average_fps;
            const bool better = p95_gain >= 0.05 || low_gain >= 0.05 || avg_gain >= 0.05;
            const bool worse = p95_gain <= -0.05 || low_gain <= -0.05 || avg_gain <= -0.05;
            report.result = better && worse ? "mixed" : better ? "improved" :
                worse ? "worsened" : "no_clear_change";
        }
        return report;
    }
private:
    struct Pending {
        Report report;
        Context context;
        std::uint64_t requested, applied;
        std::string invalid;
    };
    std::optional<Pending> pending_;
    Context baseline_context_;
    std::deque<std::pair<std::uint64_t, Context>> scene_history_;
    std::uint64_t context_since_{0}, last_observed_{0};
};
} // namespace kf2::optimizer
