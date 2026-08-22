#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "kf2/core/result.hpp"

namespace kf2::update {

inline constexpr std::int64_t kAutomaticCheckIntervalSeconds = 24 * 60 * 60;

enum class CheckTrigger { automatic, manual };
enum class CheckStart { started, automatic_disabled, throttled, busy };
enum class UpdatePhase { idle, checking, available, current, error, installing };

struct ReleaseAsset {
    std::string file_name;
    std::string download_url;
    std::uint64_t size_bytes{};
    std::string sha256;
};

struct ReleaseInfo {
    std::string repository;
    std::string tag;
    std::string version;
    std::string published_at;
    std::string changelog;
    std::optional<ReleaseAsset> asset;
    std::wstring install_block_reason;
};

struct UpdateSnapshot {
    std::string installed_version;
    bool automatic_checks_enabled{true};
    std::int64_t last_check_unix_seconds{};
    UpdatePhase phase{UpdatePhase::idle};
    std::optional<ReleaseInfo> available_release;
    bool cached_check_completed{false};
    std::optional<std::string> cached_available_version;
    std::string ignored_version;
    std::wstring status{L"Not checked yet"};
    bool dismissed{false};
};

class UpdateController final {
public:
    explicit UpdateController(std::string installed_version);
    void restore_preferences(bool automatic_checks_enabled,
                             std::int64_t last_check_unix_seconds,
                             bool cached_check_completed = false,
                             std::string cached_available_version = {},
                             std::string ignored_version = {}) noexcept;
    [[nodiscard]] CheckStart begin_check(CheckTrigger trigger,
                                         std::int64_t now_unix_seconds) noexcept;
    void complete_check(Result<std::optional<ReleaseInfo>> result);
    void set_automatic_checks_enabled(bool enabled) noexcept;
    [[nodiscard]] bool begin_install_with_user_consent() noexcept;
    void complete_install_failure(std::wstring message);
    void dismiss() noexcept;
    void ignore_available_version() noexcept;
    [[nodiscard]] const UpdateSnapshot& snapshot() const noexcept;

private:
    UpdateSnapshot snapshot_;
};

}  // namespace kf2::update
