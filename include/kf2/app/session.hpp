#pragma once

#include <cstdint>
#include <filesystem>
#include <system_error>

#include "kf2/core/result.hpp"

namespace kf2::app {

#if defined(KF2_APP_SESSION_TESTING)
using SessionStatusHook = bool (*)(const std::filesystem::path&,
                                   std::error_code&);
void set_session_status_hook_for_testing(SessionStatusHook hook) noexcept;
#endif

struct SessionIdentity {
    std::uint32_t pid{0};
    std::uint64_t process_start_id{0};
};

class SessionGuard final {
public:
    SessionGuard(const SessionGuard&) = delete;
    SessionGuard& operator=(const SessionGuard&) = delete;
    SessionGuard(SessionGuard&&) noexcept = default;
    SessionGuard& operator=(SessionGuard&&) noexcept = default;

    [[nodiscard]] static Result<SessionGuard> start(
        const std::filesystem::path& marker_path,
        SessionIdentity identity);
    [[nodiscard]] bool previous_session_unclean() const noexcept;
    [[nodiscard]] Result<bool> mark_clean();

private:
    SessionGuard(std::filesystem::path marker_path, SessionIdentity identity,
                 bool previous_unclean);

    std::filesystem::path marker_path_;
    SessionIdentity identity_;
    bool previous_unclean_{false};
};

}  // namespace kf2::app
