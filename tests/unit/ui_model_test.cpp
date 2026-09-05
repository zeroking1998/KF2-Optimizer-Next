#include <cstdlib>
#include <iostream>

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
    return EXIT_SUCCESS;
}
