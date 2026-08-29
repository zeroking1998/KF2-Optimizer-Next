#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "kf2/core/result.hpp"

namespace kf2::game {

inline constexpr std::uint32_t kAdaptiveControlReadbackTimeoutMs = 2'000;

enum class AdaptiveResourceControl : std::uint8_t {
    cpu,
    gpu,
    vram,
    ram,
    mixed,
    recover,
};

struct AdaptiveControlRequest final {
    std::uint16_t port{0};
    std::string token;
    std::uint64_t sequence{0};
    AdaptiveResourceControl resource{AdaptiveResourceControl::mixed};
    int quality{100};
    std::uint32_t timeout_ms{kAdaptiveControlReadbackTimeoutMs};
};

struct AdaptiveControlReceipt final {
    std::uint64_t sequence{0};
    AdaptiveResourceControl resource{AdaptiveResourceControl::mixed};
    int quality{100};
};

struct AdaptiveResourceQualityState final {
    explicit AdaptiveResourceQualityState(int initial_quality = 100) noexcept;

    int cpu{100};
    int gpu{100};
    int vram{100};
    int ram{100};

    [[nodiscard]] int effective_quality() const noexcept;
    [[nodiscard]] int control_quality(
        AdaptiveResourceControl resource) const noexcept;
    void apply(const AdaptiveControlReceipt& receipt) noexcept;
    void reset(int quality) noexcept;
};

[[nodiscard]] std::string_view adaptive_resource_control_name(
    AdaptiveResourceControl resource) noexcept;
[[nodiscard]] bool valid_adaptive_control_token(
    std::string_view token) noexcept;
[[nodiscard]] Result<std::string> generate_adaptive_control_token();
[[nodiscard]] Result<std::string> build_adaptive_control_command(
    const AdaptiveControlRequest& request);
[[nodiscard]] std::optional<AdaptiveControlReceipt>
parse_adaptive_control_receipt(std::string_view response) noexcept;
[[nodiscard]] Result<AdaptiveControlReceipt> send_adaptive_control(
    const AdaptiveControlRequest& request);

// Owns no UI objects. The worker keeps only the immutable request and a
// shared result slot, so a slow KF2 acknowledgement cannot block rendering.
class AdaptiveControlDispatcher final {
public:
    AdaptiveControlDispatcher();

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] Result<bool> start(AdaptiveControlRequest request);
    [[nodiscard]] std::optional<Result<AdaptiveControlReceipt>> poll();

private:
    struct State;
    std::shared_ptr<State> state_;
};

}  // namespace kf2::game
