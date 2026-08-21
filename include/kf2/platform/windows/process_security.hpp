#pragma once

#include "kf2/core/result.hpp"

namespace kf2::platform::windows {

// Removes the current working directory from implicit DLL resolution and
// limits default dependency lookup to Windows plus explicitly registered
// directories. The portable product has no implicit sibling-DLL contract.
[[nodiscard]] Result<bool> harden_process_dll_search() noexcept;

}  // namespace kf2::platform::windows
