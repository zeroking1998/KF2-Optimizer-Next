#include "kf2/game/adaptive_control_client.hpp"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

namespace kf2::game {
namespace {

class WinsockSession final {
public:
    WinsockSession() noexcept {
        WSADATA data{};
        active_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockSession() {
        if (active_) WSACleanup();
    }
    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    bool active_{false};
};

class SocketHandle final {
public:
    explicit SocketHandle(SOCKET socket) noexcept : socket_{socket} {}
    ~SocketHandle() {
        if (socket_ != INVALID_SOCKET) closesocket(socket_);
    }
    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;
    [[nodiscard]] SOCKET get() const noexcept { return socket_; }

private:
    SOCKET socket_{INVALID_SOCKET};
};

std::optional<AdaptiveResourceControl> parse_resource(
    std::string_view value) noexcept {
    if (value == "cpu") return AdaptiveResourceControl::cpu;
    if (value == "gpu") return AdaptiveResourceControl::gpu;
    if (value == "vram") return AdaptiveResourceControl::vram;
    if (value == "ram") return AdaptiveResourceControl::ram;
    if (value == "mixed") return AdaptiveResourceControl::mixed;
    if (value == "recover") return AdaptiveResourceControl::recover;
    return std::nullopt;
}

template <typename Integer>
bool parse_integer(std::string_view value, Integer& parsed) noexcept {
    if (value.empty()) return false;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), parsed);
    return error == std::errc{} && end == value.data() + value.size();
}

std::string_view take_token(std::string_view& line) noexcept {
    const auto separator = line.find(' ');
    if (separator == std::string_view::npos) {
        const auto token = line;
        line = {};
        return token;
    }
    const auto token = line.substr(0, separator);
    line.remove_prefix(separator + 1);
    return token;
}

bool connect_with_timeout(SOCKET socket, const sockaddr_in& address,
                          std::uint32_t timeout_ms,
                          int& socket_error) noexcept {
    u_long non_blocking = 1;
    if (ioctlsocket(socket, FIONBIO, &non_blocking) != 0) {
        socket_error = WSAGetLastError();
        return false;
    }
    const int connected = connect(
        socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (connected != 0) {
        const int error = WSAGetLastError();
        if (error != WSAEWOULDBLOCK && error != WSAEINPROGRESS &&
            error != WSAEINVAL) {
            socket_error = error;
            return false;
        }
        fd_set writable{};
        FD_ZERO(&writable);
        FD_SET(socket, &writable);
        timeval timeout{
            static_cast<long>(timeout_ms / 1000U),
            static_cast<long>((timeout_ms % 1000U) * 1000U)};
        const int selected = select(0, nullptr, &writable, nullptr, &timeout);
        if (selected <= 0) {
            socket_error = selected == 0 ? WSAETIMEDOUT : WSAGetLastError();
            return false;
        }
        int pending_error = 0;
        int pending_size = sizeof(pending_error);
        if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&pending_error),
                       &pending_size) != 0 || pending_error != 0) {
            socket_error = pending_error != 0
                ? pending_error : WSAGetLastError();
            return false;
        }
    }
    non_blocking = 0;
    if (ioctlsocket(socket, FIONBIO, &non_blocking) != 0) {
        socket_error = WSAGetLastError();
        return false;
    }
    socket_error = 0;
    return true;
}

}  // namespace

std::string_view adaptive_resource_control_name(
    AdaptiveResourceControl resource) noexcept {
    switch (resource) {
        case AdaptiveResourceControl::cpu: return "cpu";
        case AdaptiveResourceControl::gpu: return "gpu";
        case AdaptiveResourceControl::vram: return "vram";
        case AdaptiveResourceControl::ram: return "ram";
        case AdaptiveResourceControl::mixed: return "mixed";
        case AdaptiveResourceControl::recover: return "recover";
    }
    return "mixed";
}

bool valid_adaptive_control_token(std::string_view token) noexcept {
    if (token.size() != 32) return false;
    for (const unsigned char character : token) {
        if (!((character >= '0' && character <= '9') ||
              (character >= 'a' && character <= 'f'))) {
            return false;
        }
    }
    return true;
}

Result<std::string> generate_adaptive_control_token() {
    std::array<unsigned char, 16> bytes{};
    const auto status = BCryptGenRandom(
        nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status != 0) {
        return Result<std::string>::failure({
            ErrorCode::platform_failure,
            L"A secure Adaptive session token could not be generated",
            static_cast<std::uint32_t>(status)});
    }
    constexpr char digits[] = "0123456789abcdef";
    std::string token(bytes.size() * 2, '0');
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        token[index * 2] = digits[bytes[index] >> 4U];
        token[index * 2 + 1] = digits[bytes[index] & 0x0FU];
    }
    return Result<std::string>::success(std::move(token));
}

Result<std::string> build_adaptive_control_command(
    const AdaptiveControlRequest& request) {
    if (request.port == 0 || !valid_adaptive_control_token(request.token) ||
        request.sequence == 0 || request.quality < 10 ||
        request.quality > 100 || request.timeout_ms < 25 ||
        request.timeout_ms > 2000) {
        return Result<std::string>::failure({
            ErrorCode::invalid_argument,
            L"Adaptive runtime-control request is invalid", 0});
    }
    std::string command{"KF2OPT "};
    command += request.token;
    command += ' ';
    command += std::to_string(request.sequence);
    command += ' ';
    command += adaptive_resource_control_name(request.resource);
    command += ' ';
    command += std::to_string(request.quality);
    command += '\n';
    return Result<std::string>::success(std::move(command));
}

std::optional<AdaptiveControlReceipt> parse_adaptive_control_receipt(
    std::string_view response) noexcept {
    while (!response.empty() &&
           (response.back() == '\r' || response.back() == '\n')) {
        response.remove_suffix(1);
    }
    const auto prefix = take_token(response);
    const auto sequence_text = take_token(response);
    const auto status = take_token(response);
    const auto resource_text = take_token(response);
    const auto quality_text = take_token(response);
    if (prefix != "KF2OPT_ACK" || status != "applied" || !response.empty()) {
        return std::nullopt;
    }
    AdaptiveControlReceipt receipt;
    const auto resource = parse_resource(resource_text);
    if (!parse_integer(sequence_text, receipt.sequence) ||
        receipt.sequence == 0 || !resource ||
        !parse_integer(quality_text, receipt.quality) ||
        receipt.quality < 10 || receipt.quality > 100) {
        return std::nullopt;
    }
    receipt.resource = *resource;
    return receipt;
}

Result<AdaptiveControlReceipt> send_adaptive_control(
    const AdaptiveControlRequest& request) {
    const auto command = build_adaptive_control_command(request);
    if (!command.has_value()) {
        return Result<AdaptiveControlReceipt>::failure(command.error());
    }
    WinsockSession winsock;
    if (!winsock.active()) {
        return Result<AdaptiveControlReceipt>::failure({
            ErrorCode::platform_failure,
            L"Windows sockets could not initialize for Adaptive control",
            static_cast<std::uint32_t>(WSAGetLastError())});
    }
    SocketHandle socket{::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    if (socket.get() == INVALID_SOCKET) {
        return Result<AdaptiveControlReceipt>::failure({
            ErrorCode::platform_failure,
            L"Adaptive loopback socket could not be created",
            static_cast<std::uint32_t>(WSAGetLastError())});
    }
    const DWORD timeout = request.timeout_ms;
    if (setsockopt(socket.get(), SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0 ||
        setsockopt(socket.get(), SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout)) != 0) {
        return Result<AdaptiveControlReceipt>::failure({
            ErrorCode::platform_failure,
            L"Adaptive loopback timeout could not be configured",
            static_cast<std::uint32_t>(WSAGetLastError())});
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(request.port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int connect_error = 0;
    if (!connect_with_timeout(
            socket.get(), address, request.timeout_ms, connect_error)) {
        return Result<AdaptiveControlReceipt>::failure({
            ErrorCode::io_failure,
            L"KF2 Adaptive runtime bridge is not reachable",
            static_cast<std::uint32_t>(connect_error)});
    }
    std::size_t sent = 0;
    while (sent < command.value().size()) {
        const auto remaining = command.value().size() - sent;
        const int amount = send(
            socket.get(), command.value().data() + sent,
            static_cast<int>(std::min<std::size_t>(
                remaining, static_cast<std::size_t>(
                    std::numeric_limits<int>::max()))), 0);
        if (amount <= 0) {
            return Result<AdaptiveControlReceipt>::failure({
                ErrorCode::io_failure,
                L"Adaptive runtime command could not be sent",
                static_cast<std::uint32_t>(WSAGetLastError())});
        }
        sent += static_cast<std::size_t>(amount);
    }
    std::array<char, 257> buffer{};
    std::string response;
    for (;;) {
        const int amount = recv(socket.get(), buffer.data(), 256, 0);
        if (amount == 0) break;
        if (amount < 0) {
            return Result<AdaptiveControlReceipt>::failure({
                ErrorCode::io_failure,
                L"Adaptive runtime acknowledgement was not received",
                static_cast<std::uint32_t>(WSAGetLastError())});
        }
        response.append(buffer.data(), static_cast<std::size_t>(amount));
        if (response.size() > 256) {
            return Result<AdaptiveControlReceipt>::failure({
                ErrorCode::io_failure,
                L"Adaptive runtime acknowledgement exceeded its limit", 0});
        }
        if (response.find('\n') != std::string::npos) break;
    }
    const auto receipt = parse_adaptive_control_receipt(response);
    if (!receipt || receipt->sequence != request.sequence ||
        receipt->resource != request.resource ||
        receipt->quality != request.quality) {
        return Result<AdaptiveControlReceipt>::failure({
            ErrorCode::io_failure,
            L"Adaptive runtime acknowledgement did not match the request", 0});
    }
    return Result<AdaptiveControlReceipt>::success(*receipt);
}

struct AdaptiveControlDispatcher::State final {
    mutable std::mutex mutex;
    bool busy{false};
    std::optional<Result<AdaptiveControlReceipt>> outcome;
};

AdaptiveControlDispatcher::AdaptiveControlDispatcher()
    : state_{std::make_shared<State>()} {}

bool AdaptiveControlDispatcher::busy() const noexcept {
    std::scoped_lock lock{state_->mutex};
    return state_->busy;
}

Result<bool> AdaptiveControlDispatcher::start(AdaptiveControlRequest request) {
    const auto command = build_adaptive_control_command(request);
    if (!command.has_value()) {
        return Result<bool>::failure(command.error());
    }
    {
        std::scoped_lock lock{state_->mutex};
        if (state_->busy) return Result<bool>::success(false);
        state_->busy = true;
        state_->outcome.reset();
    }
    try {
        const auto state = state_;
        std::thread{[state, request = std::move(request)]() mutable {
            auto outcome = send_adaptive_control(request);
            std::scoped_lock lock{state->mutex};
            state->outcome.emplace(std::move(outcome));
        }}.detach();
    } catch (...) {
        std::scoped_lock lock{state_->mutex};
        state_->busy = false;
        return Result<bool>::failure({
            ErrorCode::platform_failure,
            L"Adaptive runtime-control worker could not start", 0});
    }
    return Result<bool>::success(true);
}

std::optional<Result<AdaptiveControlReceipt>>
AdaptiveControlDispatcher::poll() {
    std::scoped_lock lock{state_->mutex};
    if (!state_->outcome) return std::nullopt;
    auto outcome = std::move(*state_->outcome);
    state_->outcome.reset();
    state_->busy = false;
    return outcome;
}

}  // namespace kf2::game
