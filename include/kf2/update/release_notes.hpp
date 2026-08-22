#pragma once

#include <string>
#include <string_view>

namespace kf2::update {

// Keeps only the short, user-facing sections used by the in-app updater and
// GitHub Releases: What's new, Bug fixes, and optional Important notes.
[[nodiscard]] std::string concise_release_notes(std::string_view markdown);

}  // namespace kf2::update
