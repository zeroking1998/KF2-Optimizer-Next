#pragma once

#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::game {

struct GameProcessIdentity;

enum class AnchorRuntimeLoadState { loaded, already_loaded };

// Loads the package-owned anchor runtime into the exact bound KF2 process.
// The module remains process-owned and disappears automatically when KF2 exits.
[[nodiscard]] Result<AnchorRuntimeLoadState> load_anchor_runtime(
    const GameProcessIdentity& process,
    const std::filesystem::path& runtime_dll);

}  // namespace kf2::game
