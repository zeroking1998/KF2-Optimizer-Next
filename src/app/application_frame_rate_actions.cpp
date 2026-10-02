#include "application_runtime.hpp"

namespace kf2::app {
namespace {
telemetry::SampleIdentity identity_of(const game::GameProcessIdentity& process) {
    return {process.pid, process.process_start_id};
}
}

bool UiRuntime::live_frame_rate_unsettled() const noexcept {
    return live_frame_rate && game_process &&
        live_frame_rate->identity == identity_of(*game_process) &&
        (live_frame_rate->queued || frame_rate_pending || live_frame_rate->uncertain);
}

void UiRuntime::present_live_frame_rate(ui::UiStatus& status) const {
    if (!live_frame_rate || !game_process ||
        live_frame_rate->identity != identity_of(*game_process)) return;
    status.target_fps_pending = live_frame_rate->queued.has_value() ||
        (frame_rate_pending && frame_rate_pending->identity == live_frame_rate->identity);
    status.target_fps_unknown = live_frame_rate->uncertain;
    if (live_frame_rate->uncertain) status.active_target_fps.reset();
    else if (live_frame_rate->confirmed) status.active_target_fps = live_frame_rate->confirmed;
}

void UiRuntime::observe_live_frame_rate(const game::GameMenuGraphicsReadback& readback) {
    if (!live_frame_rate || !game_process || !live_frame_rate->confirmed ||
        live_frame_rate->uncertain || live_frame_rate->confirmed_sequence == 0 ||
        live_frame_rate->identity != identity_of(*game_process) ||
        !readback.frame_rate_sequence || !readback.frame_rate_limit ||
        static_cast<std::uint64_t>(*readback.frame_rate_sequence) !=
            live_frame_rate->confirmed_sequence ||
        *readback.frame_rate_limit == *live_frame_rate->confirmed) return;
    live_frame_rate->uncertain = true;
    auto status = model.status();
    present_live_frame_rate(status);
    model.set_status(std::move(status));
    model.set_notice({ui::NoticeSeverity::warning, L"TARGET_FPS_NATIVE_CHANGED",
        L"KF2 changed its native FPS limit. Adaptive is held; select Target FPS again to confirm the saved limit.", L""});
    events->append({0, diagnostics::Severity::warning, "TARGET_FPS_NATIVE_CHANGED",
        L"Current native menu readback no longer confirms the owned FPS limit", L"game"});
    invalidate();
}

void UiRuntime::queue_live_frame_rate(int target_fps) {
    if (target_fps < 30 || target_fps > 240 || !game_process ||
        !game::valid_adaptive_control_token(adaptive_control_token) ||
        !game::is_game_process_current(*game_process)) return;
    const auto identity = identity_of(*game_process);
    if (!live_frame_rate || live_frame_rate->identity != identity) {
        live_frame_rate = LiveFrameRateState{identity, model.status().active_target_fps};
    }
    live_frame_rate->queued = target_fps; // Bound rapid changes to the latest intent.
    live_frame_rate->queue_deadline_ns = monotonic_ns() + 10'000'000'000ULL;
    auto status = model.status();
    present_live_frame_rate(status);
    model.set_status(std::move(status));
    model.set_notice({ui::NoticeSeverity::info, L"TARGET_FPS_PENDING",
        L"Target FPS saved; waiting for KF2 to confirm the live limit.",
        L"The current target is not changed until its matching receipt arrives."});
    invalidate();
    poll_live_frame_rate();
}

void UiRuntime::poll_live_frame_rate() {
    if (!live_frame_rate && !frame_rate_pending) return;
    const auto publish = [this](ui::NoticeSeverity severity, std::string code,
                                std::wstring message) {
        auto status = model.status();
        present_live_frame_rate(status);
        model.set_status(std::move(status));
        events->append({0, severity == ui::NoticeSeverity::error
            ? diagnostics::Severity::error : diagnostics::Severity::info,
            code, message, L"game"});
        model.set_notice({severity, std::wstring{code.begin(), code.end()},
            std::move(message), L""});
        invalidate();
    };
    if (frame_rate_pending) {
        if (auto result = frame_rate_dispatcher.poll()) {
            const auto pending = *frame_rate_pending;
            frame_rate_pending.reset();
            if (!live_frame_rate ||
                (game_process && pending.identity != identity_of(*game_process)) ||
                live_frame_rate->identity != pending.identity ||
                !game::is_game_process_current({pending.identity.pid,
                    pending.identity.process_start_id, {}})) return;
            const bool matching = result->has_value() &&
                result->value().sequence == pending.sequence &&
                result->value().resource == game::AdaptiveResourceControl::frame_rate &&
                result->value().quality == pending.target_fps;
            if (matching && result->value().status ==
                    game::AdaptiveControlReceiptStatus::applied) {
                live_frame_rate->confirmed = pending.target_fps;
                live_frame_rate->confirmed_sequence = pending.sequence;
                live_frame_rate->uncertain = false;
                const auto now = monotonic_ns();
                auto generation = adaptive_actuation.generation();
                generation.settings = ++adaptive_settings_generation;
                adaptive_actuation.rebase(generation, now);
                adaptive_governor.reset();
                log_adaptive_quality_response(quality_response.cancel("target_fps_changed"));
                adaptive_frame_not_before_ns = now;
                adaptive_overhead_breaches = 0;
                adaptive_overhead_frozen = false;
                auto status = model.status();
                present_live_frame_rate(status);
                model.set_status(std::move(status));
                if (!live_frame_rate->queued) {
                    publish(ui::NoticeSeverity::info, "TARGET_FPS_APPLIED",
                        L"KF2 confirmed the live FPS limit: " +
                        std::to_wstring(pending.target_fps) + L" FPS.");
                }
            } else {
                // A lost reply may follow a successful mutation. Never claim
                // that the old limit survived an unacknowledged command.
                const bool unchanged = matching &&
                    (result->value().status == game::AdaptiveControlReceiptStatus::unsupported ||
                     result->value().status == game::AdaptiveControlReceiptStatus::restored);
                if (matching && result->value().status ==
                        game::AdaptiveControlReceiptStatus::restored &&
                    live_frame_rate->confirmed) {
                    // Verified rollback consumed the viewport sequence too.
                    // Later native menu changes still need an orderable readback.
                    live_frame_rate->confirmed_sequence = pending.sequence;
                }
                if (!unchanged) live_frame_rate->uncertain = true;
                auto status = model.status();
                present_live_frame_rate(status);
                model.set_status(std::move(status));
                if (!live_frame_rate->queued) {
                    publish(ui::NoticeSeverity::warning, "TARGET_FPS_UNCONFIRMED",
                        unchanged
                            ? L"KF2 rejected the live change; the saved FPS target will apply at the next start."
                            : L"The live FPS limit is unconfirmed. Adaptive is held; the saved target remains available for the next start.");
                }
            }
        }
    }
    if (!live_frame_rate || !game_process) return;
    if (live_frame_rate->identity != identity_of(*game_process)) {
        live_frame_rate.reset();
        return;
    }
    if (!live_frame_rate->queued || frame_rate_pending) return;
    const auto now = monotonic_ns();
    if (now >= live_frame_rate->queue_deadline_ns) {
        live_frame_rate->queued.reset();
        publish(ui::NoticeSeverity::warning, "TARGET_FPS_BRIDGE_UNAVAILABLE",
            L"KF2's live FPS bridge is unavailable; the saved target will apply at the next start.");
        return;
    }
    // Complete earlier commands while FPS confirmation holds Adaptive or
    // timing is temporarily detached; otherwise busy could block the queue.
    if (adaptive_control_dispatcher.busy()) poll_adaptive_quality_dispatcher();
    if (adaptive_mode_dispatcher.busy()) poll_adaptive_runtime_mode();
    if (adaptive_control_dispatcher.busy() || adaptive_mode_dispatcher.busy() ||
        frame_rate_dispatcher.busy()) return;
    const auto port = game_log_session ? game_log_session->telemetry_control_port : std::nullopt;
    if (!port || !game::is_game_process_current(*game_process)) return;
    const auto sequence = game::next_adaptive_control_sequence(adaptive_control_sequence);
    // UnrealScript's canonical integer sequence must remain representable.
    if (!sequence || *sequence > 2'147'483'647ULL) {
        live_frame_rate->queued.reset();
        publish(ui::NoticeSeverity::warning, "TARGET_FPS_BRIDGE_UNAVAILABLE",
            L"The runtime-control sequence is exhausted; restart KF2 to use the saved target.");
        return;
    }
    const auto target = *live_frame_rate->queued;
    const auto started = frame_rate_dispatcher.start({*port, adaptive_control_token,
        *sequence, game::AdaptiveResourceControl::frame_rate, target});
    if (!started.has_value() || !started.value()) return;
    adaptive_control_sequence = *sequence;
    frame_rate_pending = LiveFrameRatePending{live_frame_rate->identity, *sequence, target};
    live_frame_rate->queued.reset();
}
}  // namespace kf2::app
