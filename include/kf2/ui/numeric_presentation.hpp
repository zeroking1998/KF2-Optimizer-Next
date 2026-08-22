#pragma once

#include <optional>

namespace kf2::ui {

struct NumericPresentationTargets {
    int target_fps{60};
    int corpse_limit{20};
    std::optional<double> live_fps;
    std::optional<double> live_frame_time_ms;
    std::optional<double> live_cpu_percent;
    std::optional<double> live_gpu_percent;
    std::optional<int> live_active_corpses;
    std::optional<int> live_sleeping_corpses;
};

class NumericPresentation final {
public:
    [[nodiscard]] int target_fps(int target) const noexcept;
    [[nodiscard]] int corpse_limit(int target) const noexcept;
    [[nodiscard]] std::optional<double> live_fps(
        const std::optional<double>& target) const noexcept;
    [[nodiscard]] std::optional<double> live_frame_time_ms(
        const std::optional<double>& target) const noexcept;
    [[nodiscard]] std::optional<double> live_cpu_percent(
        const std::optional<double>& target) const noexcept;
    [[nodiscard]] std::optional<double> live_gpu_percent(
        const std::optional<double>& target) const noexcept;
    [[nodiscard]] std::optional<int> live_active_corpses(
        const std::optional<int>& target) const noexcept;
    [[nodiscard]] std::optional<int> live_sleeping_corpses(
        const std::optional<int>& target) const noexcept;

    void preview_target_fps(int value) noexcept;
    void preview_corpse_limit(int value) noexcept;
    void commit_target_fps(int value) noexcept;
    void commit_corpse_limit(int value) noexcept;
    [[nodiscard]] bool advance(const NumericPresentationTargets& targets,
                               bool animate) noexcept;

private:
    std::optional<int> presented_target_fps_;
    std::optional<int> presented_corpse_limit_;
    std::optional<int> previewed_target_fps_;
    std::optional<int> previewed_corpse_limit_;
    std::optional<double> presented_live_fps_;
    std::optional<double> presented_live_frame_time_ms_;
    std::optional<double> presented_live_cpu_percent_;
    std::optional<double> presented_live_gpu_percent_;
    std::optional<int> presented_live_active_corpses_;
    std::optional<int> presented_live_sleeping_corpses_;
};

}  // namespace kf2::ui
