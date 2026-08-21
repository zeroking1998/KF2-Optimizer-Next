#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include "kf2/optimizer/adaptive_registry.hpp"

namespace kf2::optimizer {

enum class AdaptiveActionStatus {
    proposed,
    skipped_unavailable,
    shadow,
    pending,
    applied,
    failed,
    rolled_back,
    restart_required,
};

enum class AdaptiveControlId : std::uint8_t {
    corpse_runtime_limit,
    flex_solver_substeps,
    flex_particle_budget,
    flex_particle_spawn,
    flex_particle_lifetime,
    flex_fluid_particles,
    flex_nonfluid_particles,
    count,
};

struct AdaptiveGeneration final {
    std::uint64_t process_start_id{0};
    std::uint64_t session{0};
    std::uint64_t map{0};
    std::uint64_t settings{0};
    std::uint64_t capabilities{0};

    friend bool operator==(const AdaptiveGeneration&,
                           const AdaptiveGeneration&) = default;
};

struct AdaptiveActionRecord final {
    std::uint64_t action_id{0};
    AdaptiveControlId control{AdaptiveControlId::corpse_runtime_limit};
    AdaptiveActionStatus status{AdaptiveActionStatus::failed};
    double requested_value{0.0};
    std::optional<double> observed_value;
    std::optional<double> previous_owned_value;
    AdaptiveGeneration generation;
    std::uint64_t proposed_ns{0};
    std::uint64_t dispatched_ns{0};
    std::uint64_t completed_ns{0};
    std::uint32_t attempt{0};
    std::string_view provider;
    std::string_view failure_reason;
    bool owns_temporary_value{false};
};

struct AdaptiveActionReceipt final {
    std::uint64_t action_id{0};
    AdaptiveControlId control{AdaptiveControlId::corpse_runtime_limit};
    AdaptiveActionStatus status{AdaptiveActionStatus::failed};
    double requested_value{0.0};
    std::optional<double> observed_value;
    AdaptiveGeneration generation;
    std::uint64_t timestamp_ns{0};
    std::string_view provider;
    std::string_view failure_reason;
    bool owns_temporary_value{false};
    std::optional<double> previous_owned_value;
};

enum class AdaptiveReceiptResult {
    accepted,
    duplicate,
    stale_generation,
    unknown_action,
    invalid,
};

class AdaptiveActuationTracker final {
public:
    static constexpr std::uint64_t kAcknowledgementTimeoutNs =
        2'000'000'000ULL;
    static constexpr std::uint64_t kRetryBackoffNs = 500'000'000ULL;
    static constexpr std::uint32_t kMaximumAttempts = 3;
    static constexpr std::uint32_t kCircuitBreakerFailures = 3;

    explicit AdaptiveActuationTracker(AdaptiveGeneration generation = {})
        noexcept;

    [[nodiscard]] const AdaptiveActionRecord& propose(
        AdaptiveControlId control, double requested_value,
        std::optional<double> effective_value,
        AdaptiveCapabilityState capability, std::uint64_t now_ns,
        std::string_view provider = {}, bool startup_only = false) noexcept;

    [[nodiscard]] bool dispatch(AdaptiveControlId control,
                                std::uint64_t now_ns) noexcept;
    [[nodiscard]] AdaptiveReceiptResult receive(
        const AdaptiveActionReceipt& receipt) noexcept;
    void establish_effective(AdaptiveControlId control, double observed_value)
        noexcept;
    void poll(std::uint64_t now_ns) noexcept;
    void rebase(AdaptiveGeneration generation, std::uint64_t now_ns) noexcept;
    void disable(std::uint64_t now_ns) noexcept;

    [[nodiscard]] const AdaptiveActionRecord* current(
        AdaptiveControlId control) const noexcept;
    [[nodiscard]] std::optional<double> effective_value(
        AdaptiveControlId control) const noexcept;
    [[nodiscard]] bool circuit_open(AdaptiveControlId control) const noexcept;
    [[nodiscard]] AdaptiveGeneration generation() const noexcept;

private:
    struct Slot final {
        AdaptiveActionRecord record;
        std::optional<double> effective_value;
        std::uint32_t consecutive_failures{0};
        std::uint64_t retry_after_ns{0};
        bool occupied{false};
        bool circuit_open{false};
    };

    [[nodiscard]] static std::size_t index(AdaptiveControlId control) noexcept;
    void fail(Slot& slot, std::uint64_t now_ns,
              std::string_view reason) noexcept;

    std::array<Slot, static_cast<std::size_t>(AdaptiveControlId::count)> slots_{};
    AdaptiveGeneration generation_{};
    std::uint64_t next_action_id_{1};
};

[[nodiscard]] std::string_view adaptive_action_status_name(
    AdaptiveActionStatus status) noexcept;

}  // namespace kf2::optimizer
