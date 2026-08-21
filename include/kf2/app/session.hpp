#pragma once

#include <cstdint>
#include <filesystem>

#include "kf2/core/result.hpp"

namespace kf2::app {

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
