#include "kf2/game/frame_rate_cap.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

#define CHECK(condition) do { if (!(condition)) return EXIT_FAILURE; } while (false)

namespace {

std::string read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

void write_bytes(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main() {
    const fs::path root{KF2_TEST_ROOT};
    std::error_code ignored;
    fs::remove_all(root, ignored);

    const fs::path install_root = root / L"game";
    const fs::path config_root = root / L"user-config";
    const fs::path console_variables =
        install_root / L"Engine/Config/ConsoleVariables.ini";
    const fs::path game_ini = config_root / L"KFGame.ini";
    write_bytes(console_variables,
        "; native startup variables\r\n[Startup]\r\n\r\n");
    write_bytes(game_ini,
        "[KFGame.KFGameEngine]\r\n"
        "bSmoothFrameRate=False\r\n"
        "MinSmoothedFrameRate=5.000000\r\n"
        "MaxSmoothedFrameRate=122.000000\r\n");

    kf2::game::GameInstallation installation;
    installation.install_root = install_root;
    installation.config_root = config_root;

    const auto applied = kf2::game::persist_frame_rate_cap(
        installation, 120);
    CHECK(applied.has_value());
    CHECK(applied.value().changed);
    CHECK(applied.value().target_fps == 120);
    const auto console_after = read_bytes(console_variables);
    CHECK(console_after.find("[Startup]\r\n") != std::string::npos);
    CHECK(console_after.find("t.MaxFPS=120\r\n") != std::string::npos);
    const auto game_after = read_bytes(game_ini);
    CHECK(game_after.find("bSmoothFrameRate=True\r\n") != std::string::npos);
    CHECK(game_after.find("MinSmoothedFrameRate=22.000000\r\n") !=
          std::string::npos);
    CHECK(game_after.find("MaxSmoothedFrameRate=120.000000\r\n") !=
          std::string::npos);

    const auto unchanged = kf2::game::persist_frame_rate_cap(
        installation, 120);
    CHECK(unchanged.has_value());
    CHECK(!unchanged.value().changed);
    CHECK(read_bytes(console_variables) == console_after);
    CHECK(read_bytes(game_ini) == game_after);

    const auto invalid = kf2::game::persist_frame_rate_cap(
        installation, 241);
    CHECK(!invalid.has_value());
    CHECK(read_bytes(console_variables) == console_after);
    CHECK(read_bytes(game_ini) == game_after);

    write_bytes(console_variables,
        "[Startup]\r\nt.MaxFPS=120\r\nt.MaxFPS=144\r\n");
    const auto ambiguous = kf2::game::persist_frame_rate_cap(
        installation, 90);
    CHECK(!ambiguous.has_value());
    CHECK(read_bytes(console_variables).find("t.MaxFPS=90") ==
          std::string::npos);

    fs::remove_all(root, ignored);
    return EXIT_SUCCESS;
}
