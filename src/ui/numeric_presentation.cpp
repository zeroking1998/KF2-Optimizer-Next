#include "kf2/ui/numeric_presentation.hpp"

#include <algorithm>
#include <cmath>

namespace kf2::ui {
namespace {

bool advance_integer(std::optional<int>& presented, int target,
                     bool animate) noexcept {
    if (!presented) {
        presented = target;
        return false;
    }
    const int current = *presented;
    if (current == target) return false;
    if (!animate) {
        presented = target;
        return true;
    }
    const int distance = std::abs(target - current);
    const int step = std::max(1, (distance + 7) / 8);
    presented = current < target
        ? std::min(target, current + step)
        : std::max(target, current - step);
    return true;
}

bool advance_optional_integer(std::optional<int>& presented,
                              const std::optional<int>& target,
                              bool animate) noexcept {
    if (!target) {
        const bool changed = presented.has_value();
        presented.reset();
        return changed;
    }
    return advance_integer(presented, *target, animate);
}

bool advance_optional_real(std::optional<double>& presented,
                           const std::optional<double>& target,
                           bool animate) noexcept {
    if (!target) {
        const bool changed = presented.has_value();
        presented.reset();
        return changed;
    }
    if (!presented) {
        presented = *target;
        return false;
    }
    const double distance = *target - *presented;
    if (distance == 0.0) return false;
    if (!animate || std::abs(distance) <= 0.05) {
        presented = *target;
        return true;
    }
    *presented += distance / 8.0;
    return true;
}

template <typename Value>
std::optional<Value> optional_value(
    const std::optional<Value>& presented,
    const std::optional<Value>& target) noexcept {
    if (!target) return std::nullopt;
    return presented.value_or(*target);
}

}  // namespace

int NumericPresentation::target_fps(int target) const noexcept {
    return previewed_target_fps_.value_or(
        presented_target_fps_.value_or(target));
}

int NumericPresentation::corpse_limit(int target) const noexcept {
    return previewed_corpse_limit_.value_or(
        presented_corpse_limit_.value_or(target));
}

std::optional<double> NumericPresentation::live_fps(
    const std::optional<double>& target) const noexcept {
    return optional_value(presented_live_fps_, target);
}

std::optional<double> NumericPresentation::live_frame_time_ms(
    const std::optional<double>& target) const noexcept {
    return optional_value(presented_live_frame_time_ms_, target);
}

std::optional<double> NumericPresentation::live_cpu_percent(
    const std::optional<double>& target) const noexcept {
    return optional_value(presented_live_cpu_percent_, target);
}

std::optional<double> NumericPresentation::live_gpu_percent(
    const std::optional<double>& target) const noexcept {
    return optional_value(presented_live_gpu_percent_, target);
}

std::optional<int> NumericPresentation::live_active_corpses(
    const std::optional<int>& target) const noexcept {
    return optional_value(presented_live_active_corpses_, target);
}

std::optional<int> NumericPresentation::live_sleeping_corpses(
    const std::optional<int>& target) const noexcept {
    return optional_value(presented_live_sleeping_corpses_, target);
}

void NumericPresentation::preview_target_fps(int value) noexcept {
    previewed_target_fps_ = value;
}

void NumericPresentation::preview_corpse_limit(int value) noexcept {
    previewed_corpse_limit_ = value;
}

void NumericPresentation::commit_target_fps(int value) noexcept {
    previewed_target_fps_.reset();
    presented_target_fps_ = value;
}

void NumericPresentation::commit_corpse_limit(int value) noexcept {
    previewed_corpse_limit_.reset();
    presented_corpse_limit_ = value;
}

bool NumericPresentation::advance(const NumericPresentationTargets& targets,
                                  bool animate) noexcept {
    bool changed = !previewed_target_fps_ &&
        advance_integer(presented_target_fps_, targets.target_fps, animate);
    changed = (!previewed_corpse_limit_ &&
               advance_integer(presented_corpse_limit_, targets.corpse_limit,
                               animate)) || changed;
    changed = advance_optional_real(presented_live_fps_, targets.live_fps,
                                    animate) || changed;
    changed = advance_optional_real(presented_live_frame_time_ms_,
                                    targets.live_frame_time_ms, animate) ||
              changed;
    changed = advance_optional_real(presented_live_cpu_percent_,
                                    targets.live_cpu_percent, animate) ||
              changed;
    changed = advance_optional_real(presented_live_gpu_percent_,
                                    targets.live_gpu_percent, animate) ||
              changed;
    changed = advance_optional_integer(presented_live_active_corpses_,
                                       targets.live_active_corpses, animate) ||
              changed;
    changed = advance_optional_integer(presented_live_sleeping_corpses_,
                                       targets.live_sleeping_corpses, animate) ||
              changed;
    return changed;
}

}  // namespace kf2::ui
