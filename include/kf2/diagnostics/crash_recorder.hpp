#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

#include "kf2/core/result.hpp"

namespace kf2::diagnostics {

class CrashRecorder final {
public:
    CrashRecorder(const CrashRecorder&) = delete;
    CrashRecorder& operator=(const CrashRecorder&) = delete;
    CrashRecorder(CrashRecorder&& other) noexcept;
    CrashRecorder& operator=(CrashRecorder&& other) noexcept;
    ~CrashRecorder();

    [[nodiscard]] static Result<CrashRecorder> arm(
        const std::filesystem::path& directory,
        std::string_view build_identity) noexcept;
    [[nodiscard]] Result<bool> write_for_testing(
        std::uint32_t exception_code, std::uintptr_t address) noexcept;
    [[nodiscard]] const std::filesystem::path& pending_path() const noexcept;

private:
    explicit CrashRecorder(std::filesystem::path pending_path) noexcept;
    void disarm() noexcept;

    std::filesystem::path pending_path_;
    bool active_{false};
};

[[nodiscard]] std::size_t retained_crash_record_count(
    const std::filesystem::path& directory) noexcept;

}  // namespace kf2::diagnostics
