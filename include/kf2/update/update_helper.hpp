#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "kf2/core/result.hpp"
#include "kf2/update/update_package.hpp"

namespace kf2::update {

struct UpdateReadyArguments {
    std::filesystem::path receipt_path;
    std::uint32_t helper_process_id{};
    std::filesystem::path work_root;
    std::string token;
};

[[nodiscard]] Result<bool> launch_update_helper(
    const PreparedUpdatePackage& package,
    const std::filesystem::path& target_root,
    std::string_view expected_version,
    std::uint32_t parent_process_id);

[[nodiscard]] int run_update_helper(
    const std::filesystem::path& request_path) noexcept;

[[nodiscard]] Result<bool> signal_update_ready_and_schedule_cleanup(
    const UpdateReadyArguments& arguments);

[[nodiscard]] Result<bool> schedule_update_cleanup(
    std::uint32_t helper_process_id,
    const std::filesystem::path& work_root,
    std::string_view token);

}  // namespace kf2::update
