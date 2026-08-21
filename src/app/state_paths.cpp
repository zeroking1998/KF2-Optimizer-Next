#include "kf2/app/state_paths.hpp"

namespace kf2::app {

Result<StateLocation> choose_state_location(
    const std::filesystem::path& executable_directory,
    const std::filesystem::path& local_app_data,
    const WritableProbe& is_writable) {
    if (executable_directory.empty() || !is_writable) {
        return Result<StateLocation>::failure(
            {ErrorCode::invalid_argument, L"Executable directory or probe is invalid", 0});
    }

    const auto portable_root = executable_directory / L"Data";
    if (is_writable(portable_root)) {
        return Result<StateLocation>::success(
            {portable_root, StateLocationKind::portable, L""});
    }

    if (!local_app_data.empty()) {
        const auto fallback_root = local_app_data / L"KF2OptimizerNext" / L"Data";
        if (is_writable(fallback_root)) {
            return Result<StateLocation>::success(
                {fallback_root, StateLocationKind::per_user_fallback,
                 L"Portable Data directory is not writable"});
        }
    }

    return Result<StateLocation>::failure(
        {ErrorCode::access_denied,
         L"Neither the portable nor per-user state directory is writable", 0});
}

}  // namespace kf2::app
