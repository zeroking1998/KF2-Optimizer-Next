#pragma once

#include <memory>

#include "kf2/core/result.hpp"
#include "kf2/telemetry/present_source.hpp"

namespace kf2::platform::windows {

// Process-bound DirectX 11 application-present timing. The session consumes
// only the DXGI Present Start/Stop ETW events; it never injects into KF2.
class DxgiFrameTimingSession final {
public:
    DxgiFrameTimingSession(const DxgiFrameTimingSession&) = delete;
    DxgiFrameTimingSession& operator=(const DxgiFrameTimingSession&) = delete;
    ~DxgiFrameTimingSession();

    [[nodiscard]] static Result<std::unique_ptr<DxgiFrameTimingSession>> start(
        telemetry::SampleIdentity identity, telemetry::PresentSource& sink);
    [[nodiscard]] Result<bool> stop();

private:
    struct Impl;
    explicit DxgiFrameTimingSession(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
};

}  // namespace kf2::platform::windows
