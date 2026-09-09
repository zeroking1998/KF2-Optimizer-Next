#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace kf2::platform::windows {

// Serializes atomic file replacements on one normal-priority background
// thread. Pending writes to the same target are coalesced to their newest
// complete payload.
class AsyncFileWriter final {
public:
    AsyncFileWriter();
    ~AsyncFileWriter();

    AsyncFileWriter(const AsyncFileWriter&) = delete;
    AsyncFileWriter& operator=(const AsyncFileWriter&) = delete;
    AsyncFileWriter(AsyncFileWriter&&) = delete;
    AsyncFileWriter& operator=(AsyncFileWriter&&) = delete;

    [[nodiscard]] std::uint64_t submit(std::filesystem::path target,
                                       std::string bytes);
    [[nodiscard]] bool wait(std::uint64_t ticket,
                            std::chrono::milliseconds timeout);
    [[nodiscard]] bool wait_until_idle(std::chrono::milliseconds timeout);

private:
    class Impl;
    std::unique_ptr<Impl> implementation_;
};

}  // namespace kf2::platform::windows
