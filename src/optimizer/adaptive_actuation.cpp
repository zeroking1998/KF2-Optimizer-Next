#include "kf2/optimizer/adaptive_actuation.hpp"

#include <cmath>

namespace kf2::optimizer {
namespace {

bool is_terminal(AdaptiveActionStatus status) noexcept {
    return status == AdaptiveActionStatus::applied ||
           status == AdaptiveActionStatus::failed ||
           status == AdaptiveActionStatus::rolled_back ||
           status == AdaptiveActionStatus::restart_required ||
           status == AdaptiveActionStatus::skipped_unavailable ||
           status == AdaptiveActionStatus::shadow;
}

bool valid_terminal_receipt(AdaptiveActionStatus status) noexcept {
    return status == AdaptiveActionStatus::applied ||
           status == AdaptiveActionStatus::failed ||
           status == AdaptiveActionStatus::rolled_back;
}

}  // namespace

AdaptiveActuationTracker::AdaptiveActuationTracker(
    AdaptiveGeneration generation) noexcept : generation_(generation) {}

const AdaptiveActionRecord& AdaptiveActuationTracker::propose(
    AdaptiveControlId control, double requested_value,
    std::optional<double> effective_value, AdaptiveCapabilityState capability,
    std::uint64_t now_ns, std::string_view provider,
    bool startup_only) noexcept {
    Slot& slot = slots_[index(control)];
    if (slot.occupied && slot.record.generation == generation_ &&
        slot.record.requested_value == requested_value &&
        (slot.record.status == AdaptiveActionStatus::proposed ||
         slot.record.status == AdaptiveActionStatus::pending ||
         (slot.record.status == AdaptiveActionStatus::applied &&
          slot.effective_value == effective_value))) {
        return slot.record;
    }
    if (slot.occupied && slot.record.status == AdaptiveActionStatus::pending) {
        return slot.record;
    }

    slot.occupied = true;
    slot.effective_value = effective_value;
    slot.record = {};
    slot.record.action_id = next_action_id_++;
    slot.record.control = control;
    slot.record.requested_value = requested_value;
    slot.record.observed_value = effective_value;
    slot.record.generation = generation_;
    slot.record.proposed_ns = now_ns;
    slot.record.provider = provider;

    if (!std::isfinite(requested_value)) {
        slot.record.status = AdaptiveActionStatus::failed;
        slot.record.failure_reason = "invalid_requested_value";
    } else if (startup_only || capability == AdaptiveCapabilityState::restart_required) {
        slot.record.status = AdaptiveActionStatus::restart_required;
        slot.record.failure_reason = "startup_only";
    } else if (capability == AdaptiveCapabilityState::unavailable ||
               slot.circuit_open) {
        slot.record.status = AdaptiveActionStatus::skipped_unavailable;
        slot.record.failure_reason = slot.circuit_open
            ? "session_circuit_breaker_open" : "actuator_unavailable";
    } else if (capability == AdaptiveCapabilityState::shadow) {
        slot.record.status = AdaptiveActionStatus::shadow;
        slot.record.failure_reason = "shadow_capability";
    } else {
        slot.record.status = AdaptiveActionStatus::proposed;
    }
    return slot.record;
}

bool AdaptiveActuationTracker::dispatch(AdaptiveControlId control,
                                         std::uint64_t now_ns) noexcept {
    Slot& slot = slots_[index(control)];
    if (!slot.occupied || slot.circuit_open ||
        slot.record.status != AdaptiveActionStatus::proposed ||
        now_ns < slot.retry_after_ns ||
        slot.record.attempt >= kMaximumAttempts) {
        return false;
    }
    slot.record.status = AdaptiveActionStatus::pending;
    slot.record.dispatched_ns = now_ns;
    ++slot.record.attempt;
    return true;
}

AdaptiveReceiptResult AdaptiveActuationTracker::receive(
    const AdaptiveActionReceipt& receipt) noexcept {
    if (receipt.control == AdaptiveControlId::count ||
        !valid_terminal_receipt(receipt.status) ||
        !std::isfinite(receipt.requested_value) || receipt.timestamp_ns == 0 ||
        (receipt.status == AdaptiveActionStatus::applied &&
         (!receipt.observed_value || !std::isfinite(*receipt.observed_value)))) {
        return AdaptiveReceiptResult::invalid;
    }
    if (!(receipt.generation == generation_)) {
        return AdaptiveReceiptResult::stale_generation;
    }
    Slot& slot = slots_[index(receipt.control)];
    if (!slot.occupied || slot.record.action_id != receipt.action_id) {
        return AdaptiveReceiptResult::unknown_action;
    }
    if (is_terminal(slot.record.status)) {
        return AdaptiveReceiptResult::duplicate;
    }
    if (slot.record.status != AdaptiveActionStatus::pending ||
        receipt.requested_value != slot.record.requested_value) {
        return AdaptiveReceiptResult::invalid;
    }

    slot.record.completed_ns = receipt.timestamp_ns;
    slot.record.provider = receipt.provider;
    slot.record.failure_reason = receipt.failure_reason;
    slot.record.owns_temporary_value = receipt.owns_temporary_value;
    slot.record.previous_owned_value = receipt.previous_owned_value;
    if (receipt.status == AdaptiveActionStatus::applied) {
        slot.record.status = AdaptiveActionStatus::applied;
        slot.record.observed_value = receipt.observed_value;
        slot.effective_value = receipt.observed_value;
        slot.consecutive_failures = 0;
        slot.retry_after_ns = 0;
    } else if (receipt.status == AdaptiveActionStatus::rolled_back) {
        slot.record.status = AdaptiveActionStatus::rolled_back;
        slot.record.observed_value = receipt.observed_value;
        slot.effective_value = receipt.observed_value;
        slot.consecutive_failures = 0;
        slot.retry_after_ns = 0;
        slot.record.owns_temporary_value = false;
    } else {
        fail(slot, receipt.timestamp_ns,
             receipt.failure_reason.empty() ? "actuator_failed"
                                            : receipt.failure_reason);
    }
    return AdaptiveReceiptResult::accepted;
}

void AdaptiveActuationTracker::establish_effective(
    AdaptiveControlId control, double observed_value) noexcept {
    if (control == AdaptiveControlId::count ||
        !std::isfinite(observed_value)) {
        return;
    }
    Slot& slot = slots_[index(control)];
    if (!slot.occupied) slot.effective_value = observed_value;
}

void AdaptiveActuationTracker::poll(std::uint64_t now_ns) noexcept {
    for (auto& slot : slots_) {
        if (!slot.occupied || slot.record.status != AdaptiveActionStatus::pending ||
            now_ns < slot.record.dispatched_ns) {
            continue;
        }
        if (now_ns - slot.record.dispatched_ns >= kAcknowledgementTimeoutNs) {
            fail(slot, now_ns, "acknowledgement_timeout");
        }
    }
}

void AdaptiveActuationTracker::rebase(AdaptiveGeneration generation,
                                      std::uint64_t now_ns) noexcept {
    const bool same_session = generation_.process_start_id != 0 &&
        generation.process_start_id == generation_.process_start_id &&
        generation.session == generation_.session;
    for (auto& slot : slots_) {
        if (slot.occupied && (slot.record.status == AdaptiveActionStatus::pending ||
                              slot.record.status == AdaptiveActionStatus::proposed)) {
            slot.record.status = AdaptiveActionStatus::failed;
            slot.record.failure_reason = "generation_rebased";
            slot.record.completed_ns = now_ns;
        }
        const auto consecutive_failures = slot.consecutive_failures;
        const auto retry_after_ns = slot.retry_after_ns;
        const bool circuit_open = slot.circuit_open;
        slot = {};
        if (same_session) {
            slot.consecutive_failures = consecutive_failures;
            slot.retry_after_ns = retry_after_ns;
            slot.circuit_open = circuit_open;
        }
    }
    generation_ = generation;
}

void AdaptiveActuationTracker::disable(std::uint64_t now_ns) noexcept {
    for (auto& slot : slots_) {
        if (!slot.occupied) continue;
        if (slot.record.status == AdaptiveActionStatus::pending ||
            slot.record.status == AdaptiveActionStatus::proposed) {
            slot.record.status = AdaptiveActionStatus::failed;
            slot.record.failure_reason = "adaptive_disabled";
            slot.record.completed_ns = now_ns;
        }
    }
}

const AdaptiveActionRecord* AdaptiveActuationTracker::current(
    AdaptiveControlId control) const noexcept {
    if (control == AdaptiveControlId::count) return nullptr;
    const Slot& slot = slots_[index(control)];
    return slot.occupied ? &slot.record : nullptr;
}

std::optional<double> AdaptiveActuationTracker::effective_value(
    AdaptiveControlId control) const noexcept {
    if (control == AdaptiveControlId::count) return std::nullopt;
    return slots_[index(control)].effective_value;
}

bool AdaptiveActuationTracker::circuit_open(
    AdaptiveControlId control) const noexcept {
    if (control == AdaptiveControlId::count) return true;
    return slots_[index(control)].circuit_open;
}

AdaptiveGeneration AdaptiveActuationTracker::generation() const noexcept {
    return generation_;
}

std::size_t AdaptiveActuationTracker::index(AdaptiveControlId control) noexcept {
    const auto value = static_cast<std::size_t>(control);
    return value < static_cast<std::size_t>(AdaptiveControlId::count) ? value : 0;
}

void AdaptiveActuationTracker::fail(Slot& slot, std::uint64_t now_ns,
                                    std::string_view reason) noexcept {
    slot.record.status = AdaptiveActionStatus::failed;
    slot.record.completed_ns = now_ns;
    slot.record.failure_reason = reason;
    ++slot.consecutive_failures;
    slot.retry_after_ns = now_ns + kRetryBackoffNs;
    if (slot.consecutive_failures >= kCircuitBreakerFailures) {
        slot.circuit_open = true;
    }
}

std::string_view adaptive_action_status_name(AdaptiveActionStatus status) noexcept {
    switch (status) {
        case AdaptiveActionStatus::proposed: return "PROPOSED";
        case AdaptiveActionStatus::skipped_unavailable:
            return "SKIPPED_UNAVAILABLE";
        case AdaptiveActionStatus::shadow: return "SHADOW";
        case AdaptiveActionStatus::pending: return "PENDING";
        case AdaptiveActionStatus::applied: return "APPLIED";
        case AdaptiveActionStatus::failed: return "FAILED";
        case AdaptiveActionStatus::rolled_back: return "ROLLED_BACK";
        case AdaptiveActionStatus::restart_required: return "RESTART_REQUIRED";
    }
    return "FAILED";
}

}  // namespace kf2::optimizer
