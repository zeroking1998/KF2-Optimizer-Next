#include "kf2/ui/ui_model.hpp"

#include <algorithm>
#include <utility>

namespace kf2::ui {
namespace {

std::size_t enum_index(Destination destination) {
    return static_cast<std::size_t>(destination);
}

std::size_t navigation_index(Destination destination) {
    const auto found = std::find(kDestinations.begin(), kDestinations.end(),
                                 destination);
    return found == kDestinations.end()
        ? std::size_t{0}
        : static_cast<std::size_t>(found - kDestinations.begin());
}

std::wstring_view friendly_mode(std::wstring_view mode) {
    if (mode == L"Adaptive / Automatic") return L"Adaptive";
    if (mode == L"Read-only") return L"Read-only";
    if (mode == L"Safe mode") return L"Safe mode";
    return mode;
}

std::wstring_view friendly_profile(std::wstring_view profile) {
    if (profile == L"high_performance") return L"Maximum performance";
    if (profile == L"balanced") return L"Balanced";
    if (profile == L"stability") return L"Stability";
    if (profile == L"custom") return L"Custom";
    if (profile == L"waiting") return L"Waiting for measurements";
    return profile;
}

}  // namespace

std::wstring_view destination_label(Destination destination) {
    static constexpr std::array<std::wstring_view, 6> labels{
        L"Home", L"Game", L"Fine-tuning", L"Overlay",
        L"Diagnostics & Backup", L"Optimization"};
    return labels.at(enum_index(destination));
}

Destination UiModel::selected() const noexcept { return selected_; }
Destination UiModel::focused_destination() const noexcept { return focused_; }
const std::optional<std::string>& UiModel::focused_action() const noexcept {
    return focused_action_;
}

UiAction UiModel::navigate(NavigationCommand command) noexcept {
    const std::size_t old_index = navigation_index(focused_);
    const bool action_was_focused = focused_action_.has_value();
    std::size_t new_index = old_index;
    switch (command) {
        case NavigationCommand::next:
            new_index = (old_index + 1) % kDestinations.size();
            break;
        case NavigationCommand::previous:
            new_index = (old_index + kDestinations.size() - 1) % kDestinations.size();
            break;
        case NavigationCommand::home:
            new_index = 0;
            break;
        case NavigationCommand::end:
            new_index = kDestinations.size() - 1;
            break;
    }
    focused_ = kDestinations[new_index];
    focused_action_.reset();
    const bool changed = new_index != old_index || action_was_focused;
    return {changed};
}

UiAction UiModel::activate_focused() noexcept {
    const bool changed = selected_ != focused_;
    selected_ = focused_;
    scroll_offset_ = 0.0F;
    return {changed};
}

UiAction UiModel::focus_destination(Destination destination) noexcept {
    const bool changed = focused_ != destination || focused_action_.has_value();
    focused_ = destination;
    focused_action_.reset();
    return {changed};
}

UiAction UiModel::focus_action(std::optional<std::string> action) noexcept {
    const bool changed = focused_action_ != action;
    focused_action_ = std::move(action);
    return {changed};
}

void UiModel::set_scroll_extent(float extent) noexcept {
    scroll_extent_ = std::max(0.0F, extent);
    scroll_offset_ = std::clamp(scroll_offset_, 0.0F, scroll_extent_);
}

UiAction UiModel::set_scroll(float offset) noexcept {
    const float bounded = std::clamp(offset, 0.0F, scroll_extent_);
    const bool changed = bounded != scroll_offset_;
    scroll_offset_ = bounded;
    return {changed};
}

float UiModel::scroll_offset() const noexcept { return scroll_offset_; }
float UiModel::scroll_extent() const noexcept { return scroll_extent_; }
void UiModel::set_state_path(std::wstring path) { state_path_ = std::move(path); }
void UiModel::set_build_identity(std::wstring identity) {
    build_identity_ = std::move(identity);
}
void UiModel::set_recovery_required(bool required) noexcept {
    recovery_required_ = required;
}
void UiModel::set_status(UiStatus status) { status_ = std::move(status); }
void UiModel::set_preview_summary(std::wstring summary) {
    preview_summary_ = std::move(summary);
}
void UiModel::clear_preview_summary() noexcept { preview_summary_.clear(); }
void UiModel::set_notice(Notice notice) { notice_ = std::move(notice); }
void UiModel::clear_notice() noexcept { notice_.reset(); }
const std::wstring& UiModel::state_path() const noexcept { return state_path_; }
const std::wstring& UiModel::build_identity() const noexcept {
    return build_identity_;
}
bool UiModel::recovery_required() const noexcept { return recovery_required_; }
const UiStatus& UiModel::status() const noexcept { return status_; }
const std::optional<Notice>& UiModel::notice() const noexcept { return notice_; }

std::wstring UiModel::page_heading() const {
    return std::wstring{destination_label(selected_)};
}

std::wstring UiModel::page_body() const {
    if (selected_ == Destination::dashboard) {
        if (!status_.game_detected) {
            return L"KF2 is not ready yet. Select the game folder on the Game "
                   L"page or refresh discovery.\n"
                   L"All settings remain portable and local.";
        }
        return L"Ready to play. Mode: " +
               std::wstring{friendly_mode(status_.mode)} + L" | Target: " +
               std::to_wstring(status_.target_fps) + L" FPS | Profile: " +
               std::wstring{friendly_profile(status_.profile)} + L".\n" +
               status_.game + L" | " + status_.telemetry;
    }
    if (selected_ == Destination::game) {
        const auto session = status_.game_session.empty()
            ? status_.game
            : status_.game + L". " + status_.game_session;
        const std::wstring flex_requested =
            status_.adaptive_flex_requested_substeps
                ? std::to_wstring(*status_.adaptive_flex_requested_substeps)
                : L"unavailable";
        const std::wstring flex_effective =
            status_.adaptive_flex_effective_substeps
                ? std::to_wstring(*status_.adaptive_flex_effective_substeps)
                : L"not confirmed";
        return session + L". " + status_.flex_telemetry + L".\n" +
               L"FleX capability: " + status_.adaptive_flex_capability +
               L" | Requested: " + flex_requested +
               L" | Effective: " + flex_effective +
               L" | Status: " + status_.adaptive_flex_action_status + L".\n" +
               L"Protected corpse provider: automatic on Adaptive launch" +
               L" | Game configuration protection: " +
               std::wstring{status_.restore_config_after_game ? L"restore"
                                                               : L"keep"};
    }
    if (selected_ == Destination::overlay) {
        return L"Press F10 to toggle. Status: " +
               std::wstring{status_.overlay_enabled ? L"ON" : L"OFF"} +
               L" | Position: " + status_.overlay_position + L" | Scale: " +
               std::to_wstring(status_.overlay_scale_percent) + L" %.\n" +
               L"The overlay remains bound to the verified KF2 process.";
    }
    if (selected_ == Destination::optimizer) {
        std::wstring workflow;
        switch (status_.config) {
            case ConfigWorkflowState::unavailable: workflow = L"Game not detected"; break;
            case ConfigWorkflowState::detected:
                workflow = L"Game detected; the verified change catalog is available";
                break;
            case ConfigWorkflowState::preview_ready:
                workflow = preview_summary_.empty() ? L"Preview ready" : preview_summary_;
                break;
            case ConfigWorkflowState::apply_blocked: workflow = L"Apply blocked"; break;
            case ConfigWorkflowState::applied: workflow = L"Configuration applied"; break;
            case ConfigWorkflowState::restore_available: workflow = L"Restore available"; break;
            case ConfigWorkflowState::recovery_required: workflow = L"Recovery required"; break;
        }
        return L"Verified change plan: " + workflow + L".\n" +
               L"Recommended profile: " +
               std::wstring{friendly_profile(status_.recommended_profile)} +
               L". Every change remains visible, backed up, and reversible.";
    }
    if (selected_ == Destination::settings) {
        const std::wstring policy_effect =
            L"Adaptive controls the verified profile and restores the original INIs after the game";
        const std::wstring mode_values =
            L" | Next profile: " + status_.recommended_profile;
        const std::wstring activation =
            L"Launching through this app prepares the safe plan automatically.";
        const std::wstring runtime_corpses =
            status_.adaptive_runtime_corpse_limit
                ? std::to_wstring(*status_.adaptive_runtime_corpse_limit)
                : L"not observed yet";
        return L"Mode: " + std::wstring{friendly_mode(status_.mode)} +
               L" | Target: " +
               std::to_wstring(status_.target_fps) + L" FPS" + mode_values +
               L" | Corpse ceiling: " +
               std::to_wstring(status_.corpse_limit) +
               L" | Adaptive runtime limit: " + runtime_corpses + L".\n" +
               L"Corpse capability: " + status_.adaptive_corpse_capability +
               L" | Action status: " + status_.adaptive_corpse_action_status +
               L" | Particle actor: " + status_.adaptive_particle_capability +
               L".\n" +
               policy_effect + L". " + activation;
    }
    if (selected_ == Destination::diagnostics) {
        return L"Check, back up, and restore entirely locally. No data is "
               L"uploaded. The most important actions are grouped by task.\n" +
               status_.flex_telemetry + L" | " + status_.hardware_summary +
               L".\nLicense: GPL-3.0-only, without warranty. Full terms: "
               L"Data\\Documentation\\LICENSE";
    }
    return L"Feature unavailable";
}

}  // namespace kf2::ui
