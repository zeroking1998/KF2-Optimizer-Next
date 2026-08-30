#include <WinSock2.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "kf2/game/adaptive_control_client.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

int main() {
    using namespace kf2::game;
    constexpr auto token = "0123456789abcdef0123456789abcdef";
    CHECK(valid_adaptive_control_token(token));
    CHECK(!valid_adaptive_control_token("0123"));
    CHECK(!valid_adaptive_control_token(
        "0123456789ABCDEF0123456789ABCDEF"));

    const auto generated = generate_adaptive_control_token();
    CHECK(generated.has_value());
    CHECK(valid_adaptive_control_token(generated.value()));

    const auto command = build_adaptive_control_command({
        .port = 17777,
        .token = token,
        .sequence = 42,
        .resource = AdaptiveResourceControl::vram,
        .quality = 50,
        .timeout_ms = 200});
    CHECK(command.has_value());
    CHECK(command.value() ==
          "KF2OPT 0123456789abcdef0123456789abcdef 42 vram 50\n");
    const auto overdraw_command = build_adaptive_control_command({
        .port = 17777,
        .token = token,
        .sequence = 43,
        .resource = AdaptiveResourceControl::overdraw,
        .quality = 70,
        .timeout_ms = 200});
    CHECK(overdraw_command.has_value());
    CHECK(overdraw_command.value() ==
          "KF2OPT 0123456789abcdef0123456789abcdef 43 overdraw 70\n");
    CHECK(!build_adaptive_control_command({
        .port = 0, .token = token, .sequence = 1}).has_value());
    CHECK(!build_adaptive_control_command({
        .port = 1, .token = token, .sequence = 1,
        .quality = 9}).has_value());

    const auto receipt = parse_adaptive_control_receipt(
        "KF2OPT_ACK 42 applied vram 50\r\n");
    CHECK(receipt.has_value());
    CHECK(receipt->sequence == 42);
    CHECK(receipt->resource == AdaptiveResourceControl::vram);
    CHECK(receipt->quality == 50);
    const auto overdraw_receipt = parse_adaptive_control_receipt(
        "KF2OPT_ACK 43 applied overdraw 70\r\n");
    CHECK(overdraw_receipt.has_value());
    CHECK(overdraw_receipt->resource == AdaptiveResourceControl::overdraw);
    CHECK(overdraw_receipt->quality == 70);
    CHECK(!parse_adaptive_control_receipt(
        "KF2OPT_ACK 43 applied target_fps 100\r\n").has_value());
    CHECK(!parse_adaptive_control_receipt(
        "KF2OPT_ACK 42 failed rejected").has_value());
    CHECK(!parse_adaptive_control_receipt(
        "KF2OPT_ACK 42 applied vram 50 trailing").has_value());
    CHECK(!parse_adaptive_control_receipt(
        "KF2OPT_ACK 42 applied flex 50").has_value());

    AdaptiveResourceQualityState quality{100};
    CHECK(quality.effective_quality() == 100);
    CHECK(quality.control_quality(AdaptiveResourceControl::gpu) == 100);
    quality.apply({1, AdaptiveResourceControl::gpu, 60});
    CHECK(quality.gpu == 60);
    CHECK(quality.cpu == 100);
    CHECK(quality.effective_quality() == 60);
    CHECK(quality.control_quality(AdaptiveResourceControl::cpu) == 100);
    quality.apply({2, AdaptiveResourceControl::overdraw, 70});
    CHECK(quality.overdraw == 70);
    CHECK(quality.effective_quality() == 60);
    quality.apply({2, AdaptiveResourceControl::cpu, 80});
    CHECK(quality.cpu == 80);
    CHECK(quality.gpu == 60);
    quality.apply({3, AdaptiveResourceControl::recover, 65});
    CHECK(quality.gpu == 65);
    CHECK(quality.cpu == 80);
    CHECK(quality.vram == 100);
    quality.apply({4, AdaptiveResourceControl::mixed, 50});
    CHECK(quality.cpu == 50 && quality.gpu == 50 &&
          quality.vram == 50 && quality.ram == 50 &&
          quality.overdraw == 50);
    quality.reset(90);
    CHECK(quality.effective_quality() == 90);

    WSADATA winsock{};
    CHECK(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
    const SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(listener != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    CHECK(bind(listener, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) == 0);
    int address_size = sizeof(address);
    CHECK(getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                      &address_size) == 0);
    CHECK(listen(listener, 1) == 0);
    std::string observed_command;
    std::thread server{[&] {
        const SOCKET connection = accept(listener, nullptr, nullptr);
        if (connection == INVALID_SOCKET) return;
        char buffer[128]{};
        while (observed_command.find('\n') == std::string::npos) {
            const int received = recv(connection, buffer, sizeof(buffer), 0);
            if (received <= 0) break;
            observed_command.append(
                buffer, static_cast<std::size_t>(received));
        }
        if (!observed_command.empty()) {
            constexpr std::string_view acknowledgement =
                "KF2OPT_ACK 77 applied cpu 75\r\n";
            send(connection, acknowledgement.data(),
                 static_cast<int>(acknowledgement.size()), 0);
        }
        closesocket(connection);
    }};
    const auto live_receipt = send_adaptive_control({
        .port = ntohs(address.sin_port),
        .token = token,
        .sequence = 77,
        .resource = AdaptiveResourceControl::cpu,
        .quality = 75,
        .timeout_ms = 500});
    server.join();
    closesocket(listener);
    CHECK(live_receipt.has_value());
    CHECK(live_receipt.value().sequence == 77);
    CHECK(live_receipt.value().resource == AdaptiveResourceControl::cpu);
    CHECK(live_receipt.value().quality == 75);
    CHECK(observed_command ==
          "KF2OPT 0123456789abcdef0123456789abcdef 77 cpu 75\n");

    const SOCKET delayed_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(delayed_listener != INVALID_SOCKET);
    address.sin_port = 0;
    CHECK(bind(delayed_listener, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) == 0);
    address_size = sizeof(address);
    CHECK(getsockname(delayed_listener,
                      reinterpret_cast<sockaddr*>(&address),
                      &address_size) == 0);
    CHECK(listen(delayed_listener, 1) == 0);
    std::thread delayed_server{[&] {
        const SOCKET connection = accept(delayed_listener, nullptr, nullptr);
        if (connection == INVALID_SOCKET) return;
        char buffer[128]{};
        std::string request;
        while (request.find('\n') == std::string::npos) {
            const int received = recv(connection, buffer, sizeof(buffer), 0);
            if (received <= 0) break;
            request.append(buffer, static_cast<std::size_t>(received));
        }
        Sleep(350);
        constexpr std::string_view acknowledgement =
            "KF2OPT_ACK 79 applied gpu 60\r\n";
        send(connection, acknowledgement.data(),
             static_cast<int>(acknowledgement.size()), 0);
        closesocket(connection);
    }};
    const auto delayed_receipt = send_adaptive_control({
        .port = ntohs(address.sin_port),
        .token = token,
        .sequence = 79,
        .resource = AdaptiveResourceControl::gpu,
        .quality = 60});
    delayed_server.join();
    closesocket(delayed_listener);
    CHECK(delayed_receipt.has_value());
    CHECK(delayed_receipt.value().sequence == 79);
    CHECK(delayed_receipt.value().resource == AdaptiveResourceControl::gpu);
    CHECK(delayed_receipt.value().quality == 60);

    const SOCKET async_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    CHECK(async_listener != INVALID_SOCKET);
    address.sin_port = 0;
    CHECK(bind(async_listener, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) == 0);
    address_size = sizeof(address);
    CHECK(getsockname(async_listener, reinterpret_cast<sockaddr*>(&address),
                      &address_size) == 0);
    CHECK(listen(async_listener, 1) == 0);
    const HANDLE release_ack = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    CHECK(release_ack != nullptr);
    std::atomic_bool acknowledgement_sent{false};
    std::thread async_server{[&] {
        const SOCKET connection = accept(async_listener, nullptr, nullptr);
        if (connection == INVALID_SOCKET) return;
        char buffer[128]{};
        std::string request;
        while (request.find('\n') == std::string::npos) {
            const int received = recv(connection, buffer, sizeof(buffer), 0);
            if (received <= 0) break;
            request.append(buffer, static_cast<std::size_t>(received));
        }
        if (WaitForSingleObject(release_ack, 2000) == WAIT_OBJECT_0) {
            constexpr std::string_view acknowledgement =
                "KF2OPT_ACK 78 applied gpu 50\r\n";
            send(connection, acknowledgement.data(),
                 static_cast<int>(acknowledgement.size()), 0);
            acknowledgement_sent = true;
        }
        closesocket(connection);
    }};
    AdaptiveControlDispatcher dispatcher;
    auto started = dispatcher.start({
        .port = ntohs(address.sin_port),
        .token = token,
        .sequence = 78,
        .resource = AdaptiveResourceControl::gpu,
        .quality = 50});
    CHECK(started.has_value());
    CHECK(started.value());
    CHECK(dispatcher.busy());
    CHECK(!acknowledgement_sent.load());
    Sleep(250);
    CHECK(!dispatcher.poll().has_value());
    CHECK(dispatcher.busy());
    const auto parallel = dispatcher.start({
        .port = ntohs(address.sin_port),
        .token = token,
        .sequence = 79,
        .resource = AdaptiveResourceControl::gpu,
        .quality = 50});
    CHECK(parallel.has_value());
    CHECK(!parallel.value());
    CHECK(SetEvent(release_ack));
    std::optional<kf2::Result<AdaptiveControlReceipt>> async_receipt;
    for (int attempt = 0; attempt < 200 && !async_receipt; ++attempt) {
        Sleep(5);
        async_receipt = dispatcher.poll();
    }
    async_server.join();
    CloseHandle(release_ack);
    closesocket(async_listener);
    WSACleanup();
    CHECK(async_receipt.has_value());
    CHECK(async_receipt->has_value());
    CHECK(async_receipt->value().sequence == 78);
    CHECK(async_receipt->value().resource == AdaptiveResourceControl::gpu);
    CHECK(async_receipt->value().quality == 50);
    CHECK(!dispatcher.busy());
    return EXIT_SUCCESS;
}
