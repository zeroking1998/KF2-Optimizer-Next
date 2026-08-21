#pragma once

#include <type_traits>

namespace pmon::util {

template <typename T>
inline constexpr bool DependentFalse = false;

}  // namespace pmon::util
