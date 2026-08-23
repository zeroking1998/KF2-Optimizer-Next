#include "kf2/config/session_guard.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <Windows.h>

#define CHECK(x) do { if (!(x)) { std::cerr << "failed line " << __LINE__ << '\n'; return __LINE__; } } while (0)

static void write(const std::filesystem::path& p, const char* text) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary); f << text;
}
static std::string read(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>{f}, {}};
}

int main() {
    namespace fs = std::filesystem;
    const fs::path root{KF2_TEST_ROOT};
    std::error_code ec; fs::remove_all(root, ec);
    const auto config = root / L"Config";
    write(config / L"KFEngine.ini", "engine-original");
    write(config / L"Nested/KFGame.ini", "game-original");
    write(config / L"KFSystemSettings.ini",
          "[SystemSettings]\r\nbAllowTemporalAA=True\r\n");
    write(config / L"ignored.txt", "not-protected");
    auto snapshot = kf2::config::capture_session_config(config, root / L"State");
    if (!snapshot.has_value()) std::wcerr << snapshot.error().message << L" native=" << snapshot.error().native_code << L'\n';
    CHECK(snapshot.has_value()); CHECK(snapshot.value().file_count == 3);
    write(config / L"KFEngine.ini", "changed");
    write(config / L"Nested/KFGame.ini", "changed");
    write(config / L"KFSystemSettings.ini",
          "[SystemSettings]\r\nbAllowTemporalAA=False\r\n");
    write(config / L"New.ini", "created-by-game");
    write(config / L"ignored.txt", "changed-and-kept");
    auto restored = kf2::config::restore_session_config(snapshot.value());
    CHECK(restored.has_value() && restored.value() == 3);
    CHECK(read(config / L"KFEngine.ini") == "engine-original");
    CHECK(read(config / L"Nested/KFGame.ini") == "game-original");
    CHECK(read(config / L"KFSystemSettings.ini") ==
          "[SystemSettings]\r\nbAllowTemporalAA=False\r\n");
    CHECK(read(config / L"New.ini") == "created-by-game");
    CHECK(read(config / L"ignored.txt") == "changed-and-kept");
    auto second = kf2::config::capture_session_config(config, root / L"State");
    CHECK(second.has_value());
    write(config / L"KFEngine.ini", "changed-again");
    auto resumed = kf2::config::resume_session_config(config, root / L"State");
    CHECK(resumed.has_value() && resumed.value().has_value());
    CHECK(resumed.value()->file_count == 4);
    CHECK(read(config / L"KFEngine.ini") == "changed-again");
    const auto foreign = root / L"ForeignConfig";
    fs::create_directories(foreign);
    CHECK(!kf2::config::resume_session_config(
        foreign, root / L"State").has_value());
    CHECK(!kf2::config::recover_session_config(config, root / L"State", true).has_value());
    auto recovered = kf2::config::recover_session_config(config, root / L"State", false);
    CHECK(recovered.has_value() && recovered.value() == 4);
    CHECK(read(config / L"KFEngine.ini") == "engine-original");
    auto absent = kf2::config::resume_session_config(config, root / L"State");
    CHECK(absent.has_value() && !absent.value().has_value());

    auto linked = kf2::config::capture_session_config(config, root / L"State");
    CHECK(linked.has_value());
    const auto snapshot_file = linked.value().snapshot_root /
        L"files/KFEngine.ini";
    const auto snapshot_alias = linked.value().snapshot_root /
        L"files/KFEngine-alias.ini";
    CHECK(CreateHardLinkW(snapshot_alias.c_str(), snapshot_file.c_str(), nullptr) != FALSE);
    CHECK(!kf2::config::resume_session_config(
        config, root / L"State").has_value());
    fs::remove(snapshot_alias);
    CHECK(kf2::config::recover_session_config(
        config, root / L"State", false).has_value());
    return 0;
}
