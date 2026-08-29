#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "kf2/game/game_log_locator.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

void write_log(const std::filesystem::path& path, std::uint64_t timestamp,
               std::string_view contents = "log") {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << contents;
    HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    FILETIME value{
        static_cast<DWORD>(timestamp & 0xFFFFFFFFULL),
        static_cast<DWORD>(timestamp >> 32U)};
    static_cast<void>(SetFileTime(file, &value, nullptr, &value));
    CloseHandle(file);
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root = KF2_TEST_ROOT;
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);

    constexpr std::uint64_t process_start = 1'000;
    write_log(root / L"Launch.log", 1'100);
    write_log(root / L"Launch_2.log", 1'200);
    write_log(root / L"Launch-backup-2026.08.26.log", 1'300);
    write_log(root / L"Launch_debug.log", 1'400);

    auto selected = kf2::game::find_active_game_log(root, process_start);
    CHECK(selected.has_value());
    CHECK(selected.value().has_value());
    CHECK(selected.value()->path.filename() == L"Launch_2.log");
    CHECK(selected.value()->last_write_filetime == 1'200);

    write_log(root / L"Launch.log", 1'500);
    selected = kf2::game::find_active_game_log(root, process_start);
    CHECK(selected.has_value());
    CHECK(selected.value().has_value());
    CHECK(selected.value()->path.filename() == L"Launch.log");

    HANDLE active_log = CreateFileW((root / L"Launch_2.log").c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    CHECK(active_log != INVALID_HANDLE_VALUE);
    selected = kf2::game::find_active_game_log(root, process_start);
    CHECK(selected.has_value());
    CHECK(selected.value().has_value());
    CHECK(selected.value()->path.filename() == L"Launch_2.log");
    CloseHandle(active_log);

    selected = kf2::game::find_active_game_log(root, 2'000);
    CHECK(selected.has_value());
    CHECK(!selected.value().has_value());

    write_log(root / L"Launch.log", 2'100,
              "[0002.55] Log: Adapter : AMD Radeon(TM) Graphics\r\n");
    std::string delayed_renderer(128U * 1024U, 'x');
    delayed_renderer +=
        "\r\n[0002.55] Log: Adapter : NVIDIA GeForce RTX 4090\r\n";
    write_log(root / L"Launch_2.log", 2'200, delayed_renderer);
    auto renderer = kf2::game::find_last_render_adapter_name(root);
    CHECK(renderer.has_value());
    CHECK(renderer.value() ==
          std::optional<std::wstring>{L"NVIDIA GeForce RTX 4090"});

    write_log(root / L"Launch_2.log", 2'300,
              "[0002.55] Log: Adapter :   \r\n");
    renderer = kf2::game::find_last_render_adapter_name(root);
    CHECK(renderer.has_value());
    CHECK(!renderer.value().has_value());

    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
