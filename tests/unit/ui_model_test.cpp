#include <cstdlib>
#include <iostream>
#include <limits>
#include <type_traits>
#include <utility>

#include "kf2/ui/ui_model.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using namespace kf2::ui;
    static_assert(std::is_nothrow_move_assignable_v<AdaptiveUiStatus>);
    {
        UiModel adaptive_model;
        UiStatus unrelated;
        unrelated.graphics_values.fill(L"Saved graphics values remain unchanged");
        unrelated.advanced_values.fill(L"Saved advanced values remain unchanged");
        unrelated.flex_telemetry = L"Current FleX telemetry remains unchanged";
        unrelated.telemetry = L"Current frame telemetry remains unchanged";
        unrelated.prewarm_map = L"Current map prewarm remains unchanged";
        unrelated.target_fps = 119;
        unrelated.active_target_fps = 60;
        unrelated.corpse_limit = 1242;
        unrelated.active_corpse_limit = 2000;
        adaptive_model.set_status(unrelated);
        adaptive_model.preview_target_fps(125);
        adaptive_model.set_notice({NoticeSeverity::warning, L"UNCHANGED",
            L"Current safety notice", L""});
        static_cast<void>(adaptive_model.focus_destination(Destination::debug));
        static_cast<void>(adaptive_model.activate_focused());
        auto prepared = adaptive_model.adaptive_status();
        constexpr std::array text_fields{
            &AdaptiveUiStatus::recommended_profile,
            &AdaptiveUiStatus::recommendation_reason,
            &AdaptiveUiStatus::adaptive_corpse_capability,
            &AdaptiveUiStatus::adaptive_corpse_action_status,
            &AdaptiveUiStatus::adaptive_particle_capability,
            &AdaptiveUiStatus::adaptive_state,
            &AdaptiveUiStatus::adaptive_bottleneck,
            &AdaptiveUiStatus::adaptive_cpu_parallelism,
            &AdaptiveUiStatus::adaptive_action,
            &AdaptiveUiStatus::adaptive_reason,
            &AdaptiveUiStatus::adaptive_data_quality,
            &AdaptiveUiStatus::adaptive_prediction,
            &AdaptiveUiStatus::adaptive_session,
            &AdaptiveUiStatus::adaptive_source,
            &AdaptiveUiStatus::adaptive_safety,
            &AdaptiveUiStatus::adaptive_evidence};
        int label_index = 0;
        for (const auto field : text_fields) {
            prepared.*field = L"Current Adaptive label " +
                std::to_wstring(++label_index);
        }
        prepared.adaptive_optimization_enabled = false;
        prepared.adaptive_shadow_mode = true;
        prepared.adaptive_runtime_corpse_limit = 1500;
        prepared.adaptive_confidence_percent = 91;
        prepared.adaptive_drop_risk_percent = 32;
        prepared.adaptive_quality_score = 75;
        prepared.adaptive_headroom_available_percent = 12;
        prepared.adaptive_restore_generation =
            std::numeric_limits<std::uint64_t>::max();

        // An unrelated publication made after staging must survive the commit.
        auto newer_status = adaptive_model.status();
        newer_status.update_status = L"Newer update status must not roll back";
        adaptive_model.set_status(std::move(newer_status));
        const auto* graphics_text = adaptive_model.status().graphics_values[0].data();
        const auto* update_text = adaptive_model.status().update_status.data();
        const auto matches = [&](const AdaptiveUiStatus& expected) {
            const auto& current = adaptive_model.adaptive_status();
            for (const auto field : text_fields) {
                if (current.*field != expected.*field) return false;
            }
            return current.adaptive_optimization_enabled ==
                    expected.adaptive_optimization_enabled &&
                current.adaptive_shadow_mode == expected.adaptive_shadow_mode &&
                current.adaptive_runtime_corpse_limit ==
                    expected.adaptive_runtime_corpse_limit &&
                current.adaptive_confidence_percent ==
                    expected.adaptive_confidence_percent &&
                current.adaptive_drop_risk_percent ==
                    expected.adaptive_drop_risk_percent &&
                current.adaptive_quality_score == expected.adaptive_quality_score &&
                current.adaptive_headroom_available_percent ==
                    expected.adaptive_headroom_available_percent &&
                current.adaptive_restore_generation ==
                    expected.adaptive_restore_generation;
        };
        adaptive_model.set_adaptive_status(prepared);
        CHECK(matches(prepared));
        adaptive_model.set_adaptive_status(adaptive_model.adaptive_status());
        CHECK(matches(prepared));
        for (const auto field : text_fields) prepared.*field = L"";
        prepared.adaptive_runtime_corpse_limit.reset();
        adaptive_model.set_adaptive_status(prepared);
        CHECK(matches(prepared));
        adaptive_model.set_adaptive_status(AdaptiveUiStatus{});
        CHECK(matches(AdaptiveUiStatus{}));
        CHECK(adaptive_model.status().graphics_values == unrelated.graphics_values);
        CHECK(adaptive_model.status().advanced_values == unrelated.advanced_values);
        CHECK(adaptive_model.status().flex_telemetry == unrelated.flex_telemetry);
        CHECK(adaptive_model.status().telemetry == unrelated.telemetry);
        CHECK(adaptive_model.status().prewarm_map == unrelated.prewarm_map);
        CHECK(adaptive_model.status().target_fps == 119);
        CHECK(adaptive_model.status().active_target_fps == 60);
        CHECK(adaptive_model.status().corpse_limit == 1242);
        CHECK(adaptive_model.status().active_corpse_limit == 2000);
        CHECK(adaptive_model.status().graphics_values[0].data() == graphics_text);
        CHECK(adaptive_model.status().update_status.data() == update_text);
        CHECK(adaptive_model.status().update_status ==
              L"Newer update status must not roll back");
        CHECK(adaptive_model.presented_target_fps() == 125);
        CHECK(adaptive_model.selected() == Destination::debug);
        CHECK(adaptive_model.notice() && adaptive_model.notice()->code == L"UNCHANGED");
    }
    UiModel model;
    CHECK(model.selected() == Destination::dashboard);
    CHECK((kDestinations == std::array{
        Destination::dashboard, Destination::graphics, Destination::overlay,
        Destination::advanced, Destination::debug,
        Destination::diagnostics}));
    CHECK(destination_label(Destination::dashboard) == L"Home");
    CHECK(destination_label(Destination::diagnostics) == L"Help & Repair");
    CHECK(destination_label(Destination::graphics) == L"Game graphics");
    CHECK(destination_label(Destination::advanced) == L"Advanced settings");
    CHECK(destination_label(Destination::debug) == L"Debug");

    UiModel navigation_model;
    CHECK(navigation_model.navigate(NavigationCommand::home).changed == false);
    for (std::size_t index = 0; index < kDestinations.size(); ++index) {
        CHECK(navigation_model.focused_destination() == kDestinations[index]);
        CHECK(navigation_model.activate_focused().changed ==
              (index != 0));
        CHECK(navigation_model.selected() == kDestinations[index]);
        CHECK(navigation_model.navigate(NavigationCommand::next).changed);
    }
    CHECK(navigation_model.focused_destination() == Destination::dashboard);
    for (std::size_t index = kDestinations.size(); index-- > 0;) {
        CHECK(navigation_model.navigate(NavigationCommand::previous).changed);
        CHECK(navigation_model.focused_destination() == kDestinations[index]);
    }
    CHECK(navigation_model.focused_destination() == Destination::dashboard);

    CHECK(model.navigate(NavigationCommand::end).changed);
    CHECK(model.focused_destination() == Destination::diagnostics);
    CHECK(model.activate_focused().changed);
    CHECK(model.page_heading() == L"Help & Repair");

    CHECK(model.navigate(NavigationCommand::next).changed);
    CHECK(model.focused_destination() == Destination::dashboard);
    CHECK(model.activate_focused().changed);
    model.set_scroll_extent(300.0F);
    CHECK(model.set_scroll(500.0F).changed);
    CHECK(model.scroll_offset() == 300.0F);
    CHECK(model.set_scroll(-1.0F).changed);
    CHECK(model.scroll_offset() == 0.0F);

    model.set_state_path(L"C:\\Portable\\Data");
    model.set_build_identity(L"0.0.2-alpha+test");
    model.set_recovery_required(true);
    CHECK(model.page_heading() == L"Home");

    UiStatus status;
    status.mode = L"Adaptive / Automatic";
    status.target_fps = 144;
    status.profile = L"high_performance";
    status.game = L"Game detected";
    status.game_detected = true;
    status.telemetry = L"143.8 FPS, 7.0 ms";
    model.set_status(status);
    CHECK(model.page_body().empty());
    CHECK(!model.numeric_presentation_pending());
    static_cast<void>(model.advance_numeric_presentation(true));
    status.target_fps = 60;
    model.set_status(status);
    CHECK(model.numeric_presentation_pending());
    for (int frame = 0; frame < 64 && model.numeric_presentation_pending();
         ++frame) {
        static_cast<void>(model.advance_numeric_presentation(true));
    }
    CHECK(!model.numeric_presentation_pending());

    static_cast<void>(model.focus_destination(Destination::overlay));
    static_cast<void>(model.activate_focused());
    status.overlay_enabled = true;
    status.overlay_scale_percent = 125;
    status.overlay_position = L"top right";
    model.set_status(status);
    CHECK(model.page_body().empty());

    static_cast<void>(model.focus_destination(Destination::diagnostics));
    static_cast<void>(model.activate_focused());
    status.hardware_summary = L"CPU 16 Threads | GPU RTX";
    model.set_status(status);
    CHECK(model.page_body().empty());

    status.corpse_limit = 2000;
    model.set_status(status);
    CHECK(model.page_body().empty());

    static_cast<void>(model.focus_destination(Destination::debug));
    static_cast<void>(model.activate_focused());
    CHECK(model.page_heading() == L"Debug");
    CHECK(model.page_body().find(L"next protected KF2 start") !=
          std::wstring::npos);

    static_cast<void>(model.focus_destination(Destination::advanced));
    static_cast<void>(model.activate_focused());
    status.advanced_available = true;
    model.set_status(status);
    CHECK(model.page_body().empty());

    model.set_notice({NoticeSeverity::warning, L"TEST", L"Warning", L""});
    CHECK(model.notice().has_value());
    model.clear_notice();
    CHECK(!model.notice().has_value());

    status.graphics_values[3] = L"Medium";
    status.advanced_values[4] = L"Observed advanced setting";
    status.update_status = L"Update check completed";
    status.adaptive_state = L"evaluating";
    model.set_status(status);
    model.preview_target_fps(119);
    model.set_notice({NoticeSeverity::warning, L"UNCHANGED", L"Keep this notice", L""});
    const auto before_flex = model.status();
    model.set_flex_observation_status(L"Current FleX observation", 3, 1,
        L"APPLIED", L"Current substep counters", L"Current readback counters");
    CHECK(model.status().flex_telemetry == L"Current FleX observation");
    CHECK(model.status().flex_requested_substeps == 3);
    CHECK(model.status().flex_effective_substeps == 1);
    CHECK(model.status().flex_action_status == L"APPLIED");
    CHECK(model.status().flex_substep_diagnostics == L"Current substep counters");
    CHECK(model.status().flex_readback_diagnostics == L"Current readback counters");
    CHECK(model.status().mode == before_flex.mode);
    CHECK(model.status().telemetry == before_flex.telemetry);
    CHECK(model.status().target_fps == before_flex.target_fps);
    CHECK(model.status().corpse_limit == before_flex.corpse_limit);
    CHECK(model.status().graphics_values == before_flex.graphics_values);
    CHECK(model.status().advanced_values == before_flex.advanced_values);
    CHECK(model.status().update_status == before_flex.update_status);
    CHECK(model.status().adaptive_state == before_flex.adaptive_state);
    CHECK(model.presented_target_fps() == 119);
    CHECK(model.selected() == Destination::advanced);
    CHECK(model.notice() && model.notice()->code == L"UNCHANGED");
    const auto& observed = model.status();
    model.set_flex_observation_status(observed.flex_telemetry,
        observed.flex_requested_substeps, observed.flex_effective_substeps,
        observed.flex_action_status, observed.flex_substep_diagnostics,
        observed.flex_readback_diagnostics);
    CHECK(model.status().flex_telemetry == L"Current FleX observation");
    CHECK(model.status().flex_requested_substeps == 3);
    CHECK(model.status().flex_effective_substeps == 1);
    CHECK(model.status().flex_action_status == L"APPLIED");
    CHECK(model.status().flex_substep_diagnostics == L"Current substep counters");
    CHECK(model.status().flex_readback_diagnostics == L"Current readback counters");
    model.set_flex_observation_status(L"", std::nullopt, std::nullopt, L"", L"", L"");
    CHECK(model.status().flex_telemetry.empty());
    CHECK(!model.status().flex_requested_substeps);
    CHECK(!model.status().flex_effective_substeps);
    CHECK(model.status().flex_action_status.empty());
    CHECK(model.status().flex_substep_diagnostics.empty());
    CHECK(model.status().flex_readback_diagnostics.empty());
    CHECK(model.status().graphics_values == before_flex.graphics_values);
    CHECK(model.status().advanced_values == before_flex.advanced_values);
    CHECK(model.status().update_status == before_flex.update_status);
    CHECK(model.presented_target_fps() == 119);
    const auto* graphics_text = model.status().graphics_values[3].data();
    model.set_flex_capability(L"AVAILABLE");
    CHECK(model.status().flex_capability == L"AVAILABLE");
    model.set_flex_capability(model.status().flex_capability);
    CHECK(model.status().flex_capability == L"AVAILABLE");
    model.set_flex_capability(L"UNAVAILABLE");
    CHECK(model.status().flex_capability == L"UNAVAILABLE");
    model.set_flex_capability(L"");
    CHECK(model.status().flex_capability.empty());
    CHECK(model.status().graphics_values[3].data() == graphics_text);
    CHECK(model.status().graphics_values == before_flex.graphics_values);
    CHECK(model.status().advanced_values == before_flex.advanced_values);
    CHECK(model.status().update_status == before_flex.update_status);
    CHECK(model.status().adaptive_state == before_flex.adaptive_state);
    CHECK(model.presented_target_fps() == 119);
    CHECK(model.notice() && model.notice()->code == L"UNCHANGED");
    model.set_telemetry_status(L"Confirmed telemetry", L"Confirmed analysis",
        119.5, 8.25, 22.0, 44.0, 3, 9);
    CHECK(model.status().telemetry == L"Confirmed telemetry");
    CHECK(model.status().performance_analysis == L"Confirmed analysis");
    CHECK(model.status().live_fps == 119.5);
    CHECK(model.status().live_frame_time_ms == 8.25);
    CHECK(model.status().live_cpu_percent == 22.0);
    CHECK(model.status().live_gpu_percent == 44.0);
    CHECK(model.status().live_active_corpses == 3);
    CHECK(model.status().live_sleeping_corpses == 9);
    const auto& live = model.status();
    model.set_telemetry_status(live.telemetry, live.performance_analysis,
        live.live_fps, live.live_frame_time_ms, live.live_cpu_percent,
        live.live_gpu_percent, live.live_active_corpses, live.live_sleeping_corpses);
    CHECK(model.status().telemetry == L"Confirmed telemetry");
    CHECK(model.status().performance_analysis == L"Confirmed analysis");
    CHECK(model.status().live_fps == 119.5);
    model.set_telemetry_status(L"", L"", std::nullopt, std::nullopt,
        std::nullopt, std::nullopt, std::nullopt, std::nullopt);
    CHECK(model.status().telemetry.empty());
    CHECK(model.status().performance_analysis.empty());
    CHECK(!model.status().live_fps);
    CHECK(!model.status().live_frame_time_ms);
    CHECK(!model.status().live_cpu_percent);
    CHECK(!model.status().live_gpu_percent);
    CHECK(!model.status().live_active_corpses);
    CHECK(!model.status().live_sleeping_corpses);
    CHECK(model.status().graphics_values[3].data() == graphics_text);
    CHECK(model.status().graphics_values == before_flex.graphics_values);
    CHECK(model.status().advanced_values == before_flex.advanced_values);
    CHECK(model.status().adaptive_state == before_flex.adaptive_state);
    CHECK(model.status().update_status == before_flex.update_status);
    CHECK(model.presented_target_fps() == 119);
    return EXIT_SUCCESS;
}
