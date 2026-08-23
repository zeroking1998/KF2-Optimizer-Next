#include "kf2/game/frame_rate_cap.hpp"

#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <string>

#include "kf2/config/ini_document.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"
#include "kf2/platform/windows/atomic_file.hpp"

namespace kf2::game {
namespace {

constexpr std::wstring_view kStartupSection = L"Startup";
constexpr std::wstring_view kConsoleCapKey = L"t.MaxFPS";
constexpr std::wstring_view kGameEngineSection = L"KFGame.KFGameEngine";

Result<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Result<std::string>::failure(
            {ErrorCode::not_found,
             L"Required KF2 frame-cap configuration file is missing", 0});
    }
    return Result<std::string>::success(
        {std::istreambuf_iterator<char>{input},
         std::istreambuf_iterator<char>{}});
}

std::wstring fixed_fps(int value) {
    std::wostringstream text;
    text << std::fixed << std::setprecision(6)
         << static_cast<double>(value);
    return text.str();
}

Result<bool> replace_unique(config::IniDocument& document,
                            std::wstring_view section,
                            std::wstring_view key,
                            std::wstring_view value,
                            bool must_exist) {
    if (must_exist && !document.find(section, key)) {
        return Result<bool>::failure(
            {ErrorCode::not_found,
             L"KF2's native frame-cap setting is missing", 0});
    }
    const auto replaced = document.replace(section, key, value);
    if (replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Duplicate KF2 frame-cap settings were rejected", 0});
    }
    if (!replaced.changed && !document.find(section, key)) {
        return Result<bool>::failure(
            {ErrorCode::not_found,
             L"KF2's native frame-cap section is missing", 0});
    }
    return Result<bool>::success(replaced.changed);
}

Result<bool> verify_exact(const std::filesystem::path& console_path,
                          const std::filesystem::path& game_path,
                          int target_fps) {
    const auto console_bytes = read_file(console_path);
    const auto game_bytes = read_file(game_path);
    if (!console_bytes.has_value()) {
        return Result<bool>::failure(console_bytes.error());
    }
    if (!game_bytes.has_value()) {
        return Result<bool>::failure(game_bytes.error());
    }
    const auto console = config::IniDocument::parse(console_bytes.value());
    const auto game = config::IniDocument::parse(game_bytes.value());
    if (!console.has_value()) return Result<bool>::failure(console.error());
    if (!game.has_value()) return Result<bool>::failure(game.error());
    if (console.value().find(kStartupSection, kConsoleCapKey) !=
            std::optional<std::wstring>{std::to_wstring(target_fps)} ||
        game.value().find(kGameEngineSection, L"bSmoothFrameRate") !=
            std::optional<std::wstring>{L"True"} ||
        game.value().find(kGameEngineSection, L"MinSmoothedFrameRate") !=
            std::optional<std::wstring>{L"22.000000"} ||
        game.value().find(kGameEngineSection, L"MaxSmoothedFrameRate") !=
            std::optional<std::wstring>{fixed_fps(target_fps)}) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"KF2's native frame cap did not pass exact readback", 0});
    }
    return Result<bool>::success(true);
}

}  // namespace

Result<FrameRateCapResult> persist_frame_rate_cap(
    const GameInstallation& installation, int target_fps) {
    if (!optimizer::valid_target_fps(target_fps) ||
        installation.install_root.empty() ||
        installation.config_root.empty()) {
        return Result<FrameRateCapResult>::failure(
            {ErrorCode::invalid_argument,
             L"The requested KF2 frame cap is invalid", 0});
    }

    const auto console_path = installation.install_root /
        L"Engine/Config/ConsoleVariables.ini";
    const auto game_path = installation.config_root / L"KFGame.ini";
    const auto original_console = read_file(console_path);
    const auto original_game = read_file(game_path);
    if (!original_console.has_value()) {
        return Result<FrameRateCapResult>::failure(original_console.error());
    }
    if (!original_game.has_value()) {
        return Result<FrameRateCapResult>::failure(original_game.error());
    }

    auto console = config::IniDocument::parse(original_console.value());
    auto game = config::IniDocument::parse(original_game.value());
    if (!console.has_value()) {
        return Result<FrameRateCapResult>::failure(console.error());
    }
    if (!game.has_value()) {
        return Result<FrameRateCapResult>::failure(game.error());
    }

    const auto console_changed = replace_unique(
        console.value(), kStartupSection, kConsoleCapKey,
        std::to_wstring(target_fps), false);
    const auto smooth_changed = replace_unique(
        game.value(), kGameEngineSection, L"bSmoothFrameRate", L"True", true);
    const auto minimum_changed = replace_unique(
        game.value(), kGameEngineSection, L"MinSmoothedFrameRate",
        L"22.000000", true);
    const auto maximum_changed = replace_unique(
        game.value(), kGameEngineSection, L"MaxSmoothedFrameRate",
        fixed_fps(target_fps), true);
    for (const auto* result : {&console_changed, &smooth_changed,
                               &minimum_changed, &maximum_changed}) {
        if (!result->has_value()) {
            return Result<FrameRateCapResult>::failure(result->error());
        }
    }

    const bool write_console = console_changed.value();
    const bool write_game = smooth_changed.value() ||
        minimum_changed.value() || maximum_changed.value();
    if (write_console) {
        const auto written = platform::windows::atomic_replace_utf8(
            console_path, console.value().serialize());
        if (!written.has_value()) {
            return Result<FrameRateCapResult>::failure(written.error());
        }
    }
    if (write_game) {
        const auto written = platform::windows::atomic_replace_utf8(
            game_path, game.value().serialize());
        if (!written.has_value()) {
            if (write_console) {
                static_cast<void>(platform::windows::atomic_replace_utf8(
                    console_path, original_console.value()));
            }
            return Result<FrameRateCapResult>::failure(written.error());
        }
    }

    const auto verified = verify_exact(console_path, game_path, target_fps);
    if (!verified.has_value()) {
        if (write_game) {
            static_cast<void>(platform::windows::atomic_replace_utf8(
                game_path, original_game.value()));
        }
        if (write_console) {
            static_cast<void>(platform::windows::atomic_replace_utf8(
                console_path, original_console.value()));
        }
        return Result<FrameRateCapResult>::failure(verified.error());
    }
    return Result<FrameRateCapResult>::success(
        {target_fps, write_console || write_game});
}

}  // namespace kf2::game
