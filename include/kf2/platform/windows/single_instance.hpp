#pragma once

#include <string_view>

#include "kf2/core/result.hpp"

namespace kf2::platform::windows {

class SingleInstance final {
public:
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    SingleInstance(SingleInstance&& other) noexcept;
    SingleInstance& operator=(SingleInstance&& other) noexcept;
    ~SingleInstance();

    [[nodiscard]] static Result<SingleInstance> acquire(std::wstring_view name);

private:
    explicit SingleInstance(void* mutex) noexcept;
    void reset() noexcept;

    void* mutex_{nullptr};
};

}  // namespace kf2::platform::windows
