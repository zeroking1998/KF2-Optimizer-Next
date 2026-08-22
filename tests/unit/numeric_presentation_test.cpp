#include <cstdlib>
#include <iostream>

#include "kf2/ui/numeric_presentation.hpp"

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

    NumericPresentation presentation;
    NumericPresentationTargets targets{
        .target_fps = 60,
        .corpse_limit = 20,
        .live_fps = 60.0,
        .live_frame_time_ms = 16.7,
        .live_cpu_percent = 30.0,
        .live_gpu_percent = 40.0,
        .live_active_corpses = 10,
        .live_sleeping_corpses = 20,
    };
    CHECK(!presentation.advance(targets, true));
    CHECK(presentation.target_fps(targets.target_fps) == 60);
    CHECK(presentation.corpse_limit(targets.corpse_limit) == 20);
    CHECK(presentation.live_fps(targets.live_fps) == 60.0);

    targets.target_fps = 120;
    targets.corpse_limit = 1000;
    targets.live_fps = 120.0;
    targets.live_frame_time_ms = 8.3;
    targets.live_cpu_percent = 70.0;
    targets.live_gpu_percent = 80.0;
    targets.live_active_corpses = 50;
    targets.live_sleeping_corpses = 100;
    CHECK(presentation.advance(targets, true));
    CHECK(presentation.target_fps(targets.target_fps) > 60);
    CHECK(presentation.target_fps(targets.target_fps) < 120);
    CHECK(*presentation.live_fps(targets.live_fps) > 60.0);
    CHECK(*presentation.live_fps(targets.live_fps) < 120.0);

    presentation.preview_target_fps(144);
    presentation.preview_corpse_limit(1500);
    CHECK(presentation.target_fps(targets.target_fps) == 144);
    CHECK(presentation.corpse_limit(targets.corpse_limit) == 1500);
    CHECK(presentation.advance(targets, true));
    CHECK(presentation.target_fps(targets.target_fps) == 144);
    CHECK(presentation.corpse_limit(targets.corpse_limit) == 1500);
    presentation.commit_target_fps(144);
    presentation.commit_corpse_limit(1500);
    CHECK(presentation.target_fps(144) == 144);
    CHECK(presentation.corpse_limit(1500) == 1500);

    targets.target_fps = 90;
    targets.corpse_limit = 40;
    targets.live_fps = 90.0;
    targets.live_frame_time_ms = 11.1;
    targets.live_cpu_percent = 35.0;
    targets.live_gpu_percent = 45.0;
    targets.live_active_corpses = 5;
    targets.live_sleeping_corpses = 15;
    CHECK(presentation.advance(targets, false));
    CHECK(presentation.target_fps(targets.target_fps) == 90);
    CHECK(presentation.corpse_limit(targets.corpse_limit) == 40);
    CHECK(presentation.live_fps(targets.live_fps) == 90.0);
    CHECK(presentation.live_active_corpses(targets.live_active_corpses) == 5);

    targets.live_fps.reset();
    targets.live_active_corpses.reset();
    CHECK(presentation.advance(targets, true));
    CHECK(!presentation.live_fps(targets.live_fps));
    CHECK(!presentation.live_active_corpses(targets.live_active_corpses));
    return EXIT_SUCCESS;
}
