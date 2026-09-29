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

#if defined(KF2_UPDATE_HELPER_TESTING)
enum class UpdateHelperStopFault {
    none,
    termination_failure,
    wait_failure,
    wait_timeout,
};

void set_update_helper_stop_fault_for_testing(
    UpdateHelperStopFault fault) noexcept;

[[nodiscard]] bool stop_update_child_for_testing(
    void* process_handle) noexcept;

[[nodiscard]] Result<std::string> read_update_control_file_for_testing(
    const std::filesystem::path& path);
#endif

[[nodiscard]] Result<bool> launch_update_helper(
    const PreparedUpdatePackage& package,
    const std::filesystem::path& target_root,
    std::string_view expected_version,
    std::uint32_t parent_process_id);

[[nodiscard]] int run_update_helper(
    const std::filesystem::path& request_path) noexcept;

// Returns true when an update owner is still active or a recovered package
// was restarted, so the caller must not continue normal startup.
[[nodiscard]] Result<bool> recover_interrupted_updates_on_startup(
    const std::filesystem::path& target_root);

[[nodiscard]] Result<bool> signal_update_ready_and_schedule_cleanup(
    const UpdateReadyArguments& arguments);

[[nodiscard]] Result<bool> schedule_update_cleanup(
    std::uint32_t helper_process_id,
    const std::filesystem::path& work_root,
    std::string_view token);

}  // namespace kf2::update
