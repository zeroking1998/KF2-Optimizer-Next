#include <cstdlib>
#include <iostream>

#include "features/telemetry/telemetry_collection_stage.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using namespace kf2::telemetry_pipeline;
    kf2::telemetry::FrameMetrics frames;
    frames.fps = 60.0;
    auto ready = PresentDrainResult::with_frames(std::move(frames));
    CHECK(ready.disposition() == PresentDrainDisposition::frames_ready);
    CHECK(ready.frames().has_value());
    CHECK(ready.frames()->fps == 60.0);
    CHECK(!ready.error().has_value());

    auto reconnecting = PresentDrainResult::reconnecting();
    CHECK(reconnecting.disposition() ==
          PresentDrainDisposition::reconnecting);
    CHECK(!reconnecting.frames().has_value());
    CHECK(!reconnecting.error().has_value());

    auto invalid = PresentDrainResult::invalid(
        {kf2::ErrorCode::stale_data, L"identity mismatch", 0});
    CHECK(invalid.disposition() == PresentDrainDisposition::source_invalid);
    CHECK(!invalid.frames().has_value());
    CHECK(invalid.error().has_value());
    CHECK(invalid.error()->code == kf2::ErrorCode::stale_data);
    return EXIT_SUCCESS;
}
