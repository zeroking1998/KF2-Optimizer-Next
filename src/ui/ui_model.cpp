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

}  // namespace

std::wstring_view destination_label(Destination destination) {
    static constexpr std::array<std::wstring_view, 5> labels{
        L"Home", L"Game graphics", L"Overlay", L"Advanced settings",
        L"Help & Repair"};
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
void UiModel::set_notice(Notice notice) { notice_ = std::move(notice); }
void UiModel::clear_notice() noexcept { notice_.reset(); }
const std::wstring& UiModel::state_path() const noexcept { return state_path_; }
const std::wstring& UiModel::build_identity() const noexcept {
    return build_identity_;
}
bool UiModel::recovery_required() const noexcept { return recovery_required_; }
const UiStatus& UiModel::status() const noexcept { return status_; }
int UiModel::presented_target_fps() const noexcept {
    return numeric_presentation_.target_fps(status_.target_fps);
}
int UiModel::presented_corpse_limit() const noexcept {
    return numeric_presentation_.corpse_limit(status_.corpse_limit);
}
void UiModel::preview_target_fps(int value) noexcept {
    numeric_presentation_.preview_target_fps(value);
}
void UiModel::preview_corpse_limit(int value) noexcept {
    numeric_presentation_.preview_corpse_limit(value);
}
void UiModel::commit_target_fps_presentation(int value) noexcept {
    numeric_presentation_.commit_target_fps(value);
}
void UiModel::commit_corpse_limit_presentation(int value) noexcept {
    numeric_presentation_.commit_corpse_limit(value);
}
std::optional<double> UiModel::presented_live_fps() const noexcept {
    return numeric_presentation_.live_fps(status_.live_fps);
}
std::optional<double> UiModel::presented_live_frame_time_ms() const noexcept {
    return numeric_presentation_.live_frame_time_ms(
        status_.live_frame_time_ms);
}
std::optional<double> UiModel::presented_live_cpu_percent() const noexcept {
    return numeric_presentation_.live_cpu_percent(status_.live_cpu_percent);
}
std::optional<double> UiModel::presented_live_gpu_percent() const noexcept {
    return numeric_presentation_.live_gpu_percent(status_.live_gpu_percent);
}
std::optional<int> UiModel::presented_live_active_corpses() const noexcept {
    return numeric_presentation_.live_active_corpses(
        status_.live_active_corpses);
}
std::optional<int> UiModel::presented_live_sleeping_corpses() const noexcept {
    return numeric_presentation_.live_sleeping_corpses(
        status_.live_sleeping_corpses);
}
bool UiModel::advance_numeric_presentation(bool animate) noexcept {
    return numeric_presentation_.advance(
        {.target_fps = status_.target_fps,
         .corpse_limit = status_.corpse_limit,
         .live_fps = status_.live_fps,
         .live_frame_time_ms = status_.live_frame_time_ms,
         .live_cpu_percent = status_.live_cpu_percent,
         .live_gpu_percent = status_.live_gpu_percent,
         .live_active_corpses = status_.live_active_corpses,
         .live_sleeping_corpses = status_.live_sleeping_corpses},
        animate);
}
const std::optional<Notice>& UiModel::notice() const noexcept { return notice_; }

std::wstring UiModel::page_heading() const {
    return std::wstring{destination_label(selected_)};
}

std::wstring UiModel::page_body() const {
    if (selected_ == Destination::dashboard) {
        return L"";
    }
    if (selected_ == Destination::overlay) {
        return L"";
    }
    if (selected_ == Destination::graphics) {
        return status_.graphics_available
            ? L""
            : L"Select a valid Killing Floor 2 installation to edit video settings.";
    }
    if (selected_ == Destination::advanced) {
        return status_.advanced_available
            ? L""
            : L"Select a valid Killing Floor 2 installation to edit advanced INI settings.";
    }
    if (selected_ == Destination::diagnostics) {
        return L"";
    }
    return L"Feature unavailable";
}

}  // namespace kf2::ui
