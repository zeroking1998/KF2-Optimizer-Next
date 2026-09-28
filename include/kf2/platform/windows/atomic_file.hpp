#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"

namespace kf2::platform::windows {

[[nodiscard]] Result<std::string> read_bounded_verified_file(
    const std::filesystem::path& path,
    std::uintmax_t maximum_bytes);

#if defined(KF2_ATOMIC_FILE_TESTING)
using BoundedReadHook = void (*)(const std::filesystem::path&);
void set_bounded_read_hook_for_testing(BoundedReadHook hook) noexcept;

enum class AtomicFileMutationStage {
    atomic_after_validation,
    conditional_after_validation,
    quarantine_after_source_validation,
    quarantine_after_candidate_validation,
};
using AtomicFileMutationHook = void (*)(
    AtomicFileMutationStage, const std::filesystem::path&);
void set_atomic_file_mutation_hook_for_testing(
    AtomicFileMutationHook hook) noexcept;
#endif

[[nodiscard]] Result<bool> atomic_replace_utf8(
    const std::filesystem::path& target,
    std::string_view bytes);
[[nodiscard]] Result<bool> atomic_replace_utf8_if_unchanged(
    const std::filesystem::path& target,
    std::string_view expected_bytes,
    std::string_view replacement_bytes);
[[nodiscard]] Result<std::filesystem::path> quarantine_regular_file(
    const std::filesystem::path& source,
    std::wstring_view suffix = L".corrupt",
    std::size_t maximum_retained = 4);

}  // namespace kf2::platform::windows
