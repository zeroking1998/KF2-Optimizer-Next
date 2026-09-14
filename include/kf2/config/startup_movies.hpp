#pragma once

#include <cstddef>

#include "kf2/config/config_preview.hpp"
#include "kf2/core/result.hpp"

namespace kf2::config {

struct StartupLogoSkipResult {
    std::size_t removed_logos{0};
    bool file_staged{false};
};

// Adds the removal of KF2's four vendor/logo startup movies to an existing
// protected configuration preview. MainMenu and every map-loading movie remain
// untouched. The preview's normal backup, atomic apply and session restore
// provide the write and rollback boundary.
[[nodiscard]] Result<StartupLogoSkipResult> stage_startup_logo_skip(
    ConfigPreview& preview);

}  // namespace kf2::config
