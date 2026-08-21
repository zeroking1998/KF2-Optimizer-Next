#include "kf2/platform/windows/single_instance.hpp"

#include <Windows.h>

#include <cstdint>
#include <string>
#include <utility>

namespace kf2::platform::windows {

SingleInstance::SingleInstance(void* mutex) noexcept : mutex_{mutex} {}

SingleInstance::SingleInstance(SingleInstance&& other) noexcept
    : mutex_{std::exchange(other.mutex_, nullptr)} {}

SingleInstance& SingleInstance::operator=(SingleInstance&& other) noexcept {
    if (this != &other) {
        reset();
        mutex_ = std::exchange(other.mutex_, nullptr);
    }
    return *this;
}

SingleInstance::~SingleInstance() {
    reset();
}

Result<SingleInstance> SingleInstance::acquire(std::wstring_view name) {
    if (name.empty()) {
        return Result<SingleInstance>::failure(
            {ErrorCode::invalid_argument, L"Single-instance name is empty", 0});
    }

    const std::wstring terminated{name};
    HANDLE mutex = CreateMutexW(nullptr, FALSE, terminated.c_str());
    if (mutex == nullptr) {
        return Result<SingleInstance>::failure(
            {ErrorCode::platform_failure, L"Single-instance mutex creation failed",
             static_cast<std::uint32_t>(GetLastError())});
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        return Result<SingleInstance>::failure(
            {ErrorCode::already_running, L"KF2 Optimizer Next is already running",
             ERROR_ALREADY_EXISTS});
    }
    return Result<SingleInstance>::success(SingleInstance{mutex});
}

void SingleInstance::reset() noexcept {
    if (mutex_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(mutex_));
        mutex_ = nullptr;
    }
}

}  // namespace kf2::platform::windows
