#pragma once

#include <memory>

#include "kf2/core/result.hpp"
#include "kf2/telemetry/present_source.hpp"

namespace kf2::platform::windows {

class PresentMonSession final {
public:
    PresentMonSession(const PresentMonSession&) = delete;
    PresentMonSession& operator=(const PresentMonSession&) = delete;
    ~PresentMonSession();

    [[nodiscard]] static Result<std::unique_ptr<PresentMonSession>> start(
        telemetry::SampleIdentity identity, telemetry::PresentSource& sink);
    [[nodiscard]] Result<bool> stop();

private:
    struct Impl;
    explicit PresentMonSession(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> implementation_;
};

}  // namespace kf2::platform::windows
