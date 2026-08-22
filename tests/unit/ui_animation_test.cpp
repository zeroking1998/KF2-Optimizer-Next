#include <cmath>
#include <cstdlib>
#include <iostream>

#include "kf2/ui/ui_animation.hpp"

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

    float opacity = 0.0F;
    opacity = advance_tooltip_opacity(opacity, true, true);
    CHECK(opacity > 0.0F && opacity < 1.0F);
    for (int frame = 0; frame < 43; ++frame) {
        opacity = advance_tooltip_opacity(opacity, true, true);
    }
    CHECK(opacity < 1.0F);
    opacity = advance_tooltip_opacity(opacity, true, true);
    CHECK(opacity == 1.0F);
    for (int frame = 0; frame < 44; ++frame) {
        opacity = advance_tooltip_opacity(opacity, false, true);
    }
    CHECK(opacity > 0.0F);
    opacity = advance_tooltip_opacity(opacity, false, true);
    opacity = advance_tooltip_opacity(opacity, false, true);
    CHECK(opacity == 0.0F);
    CHECK(advance_tooltip_opacity(0.4F, true, false) == 1.0F);
    CHECK(advance_tooltip_opacity(0.4F, false, false) == 0.0F);

    CHECK(advance_interaction_strength(0.4F, true, true) == 1.0F);
    CHECK(advance_interaction_strength(1.0F, false, true) < 1.0F);
    float interaction = 1.0F;
    for (int frame = 0; frame < 29; ++frame) {
        interaction = advance_interaction_strength(interaction, false, true);
    }
    CHECK(interaction > 0.0F);
    interaction = advance_interaction_strength(interaction, false, true);
    interaction = advance_interaction_strength(interaction, false, true);
    CHECK(interaction == 0.0F);
    CHECK(advance_interaction_strength(1.0F, false, false) == 0.0F);
    CHECK(control_press_scale(1.0F) < 1.0F);
    CHECK(control_press_scale(1.0F) > 0.99F);
    CHECK(std::abs(control_press_scale(0.0F) - 1.0F) < 0.0001F);

    float hover = 0.0F;
    for (int frame = 0; frame < 34; ++frame) {
        hover = advance_hover_strength(hover, true, true);
    }
    CHECK(hover < 1.0F);
    hover = advance_hover_strength(hover, true, true);
    hover = advance_hover_strength(hover, true, true);
    CHECK(hover == 1.0F);
    CHECK(advance_hover_strength(0.4F, true, false) == 1.0F);

    float startup = 0.0F;
    for (int frame = 0; frame < 79; ++frame) {
        startup = advance_startup_progress(startup, true);
    }
    CHECK(startup < 1.0F);
    startup = advance_startup_progress(startup, true);
    startup = advance_startup_progress(startup, true);
    CHECK(startup == 1.0F);
    CHECK(startup_logo_scale(0.0F) == 0.82F);
    CHECK(startup_logo_scale(1.0F) == 1.0F);
    CHECK(startup_title_offset_x(0.0F) == -18.0F);
    CHECK(startup_title_offset_x(1.0F) == 0.0F);
    CHECK(advance_startup_progress(0.0F, false) == 1.0F);

    float page = 0.0F;
    for (int frame = 0; frame < 52; ++frame) {
        page = advance_page_progress(page, true);
    }
    CHECK(page < 1.0F);
    page = advance_page_progress(page, true);
    page = advance_page_progress(page, true);
    CHECK(page == 1.0F);
    CHECK(page_motion_opacity(0.0F) == 0.0F);
    CHECK(page_motion_opacity(1.0F) == 1.0F);
    CHECK(page_motion_offset_x(0.0F) == 14.0F);
    CHECK(page_motion_offset_x(1.0F) == 0.0F);

    float navigation = 0.0F;
    for (int frame = 0; frame < 57; ++frame) {
        navigation = advance_navigation_progress(navigation, true);
    }
    CHECK(navigation < 1.0F);
    navigation = advance_navigation_progress(navigation, true);
    navigation = advance_navigation_progress(navigation, true);
    CHECK(navigation == 1.0F);

    CHECK(tooltip_motion_offset_y(0.0F) == 4.0F);
    CHECK(tooltip_motion_offset_y(1.0F) == 0.0F);

    float exit = 0.0F;
    for (int index = 0; index < 44; ++index) {
        exit = advance_exit_progress(exit, true);
    }
    CHECK(exit < 1.0F);
    exit = advance_exit_progress(exit, true);
    exit = advance_exit_progress(exit, true);
    CHECK(exit == 1.0F);
    CHECK(advance_exit_progress(0.0F, false) == 1.0F);

    float glow = 0.0F;
    for (int frame = 0; frame < 199; ++frame) {
        glow = advance_update_glow_progress(glow, true);
    }
    CHECK(glow < 1.0F);
    glow = advance_update_glow_progress(glow, true);
    glow = advance_update_glow_progress(glow, true);
    CHECK(glow == 1.0F);
    CHECK(update_glow_opacity(0.0F) == 0.0F);
    CHECK(update_glow_opacity(0.5F) > 0.99F);
    CHECK(std::abs(update_glow_opacity(1.0F)) < 0.0001F);
    CHECK(interpolate_motion(10.0F, 20.0F, 0.5F) == 15.0F);
    return EXIT_SUCCESS;
}
