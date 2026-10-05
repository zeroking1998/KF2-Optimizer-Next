#include "kf2/game/frame_rate_cap.hpp"

#include <array>
#include <charconv>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include "kf2/config/ini_document.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"
#include "kf2/platform/windows/atomic_file.hpp"
#include "kf2/security/sha256.hpp"

namespace kf2::game {
namespace {

constexpr std::wstring_view kStartupSection = L"Startup";
constexpr std::wstring_view kConsoleCapKey = L"t.MaxFPS";
constexpr std::wstring_view kGameEngineSection = L"KFGame.KFGameEngine";
constexpr std::uintmax_t kMaximumFrameCapConfigBytes = 4U * 1024U * 1024U;

std::array<std::filesystem::path, 2> cap_paths(const GameInstallation& installation) {
    return {installation.install_root / L"Engine/Config/ConsoleVariables.ini",
            installation.config_root / L"KFGame.ini"};
}

std::filesystem::path recovery_path(const GameInstallation& installation,
                                   const std::filesystem::path& root) {
    return (root.empty() ? installation.config_root : root) /
           L"frame-rate-cap.recovery";
}

Error recovery_error(Error error) {
    error.code = ErrorCode::recovery_required;
    error.message = L"Native FPS cap recovery remains pending: " + error.message;
    return error;
}

Result<std::string> installation_binding(const GameInstallation& installation) {
    const auto install = installation.install_root.lexically_normal().generic_u8string();
    const auto config = installation.config_root.lexically_normal().generic_u8string();
    std::string material{reinterpret_cast<const char*>(install.data()), install.size()};
    material.push_back('\0');
    material.append(reinterpret_cast<const char*>(config.data()), config.size());
    return security::sha256_hex(material);
}

struct CapRecovery {
    std::array<std::string, 2> originals;
    std::array<std::string, 2> applied_hashes;
};

Result<std::string> recovery_record(const GameInstallation& installation,
    const std::array<std::string, 2>& originals,
    const std::array<std::string, 2>& proposed) {
    const auto binding = installation_binding(installation);
    if (!binding.has_value()) return binding;
    std::string header = "KF2FrameCapRecovery1\n" + binding.value() + "\n";
    for (const auto& bytes : originals) header += std::to_string(bytes.size()) + "\n";
    for (const auto& bytes : proposed) {
        const auto hash = security::sha256_hex(bytes);
        if (!hash.has_value()) return hash;
        header += hash.value() + "\n";
    }
    const auto body = originals[0] + originals[1];
    const auto checksum = security::sha256_hex(header + body);
    if (!checksum.has_value()) return checksum;
    return Result<std::string>::success(header + checksum.value() + "\n" + body);
}

Result<CapRecovery> parse_recovery_record(const GameInstallation& installation,
                                        std::string_view bytes) {
    const auto invalid = [] {
        return Result<CapRecovery>::failure({ErrorCode::recovery_required,
            L"Native FPS cap recovery record is invalid or belongs to another installation", 0});
    };
    std::array<std::string_view, 7> fields;
    std::size_t offset = 0;
    std::size_t checksum_offset = 0;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        const auto end = bytes.find('\n', offset);
        if (end == std::string_view::npos || end - offset > 64U) return invalid();
        if (index == 6) checksum_offset = offset;
        fields[index] = bytes.substr(offset, end - offset);
        offset = end + 1;
    }
    const auto binding = installation_binding(installation);
    if (!binding.has_value()) return Result<CapRecovery>::failure(binding.error());
    if (fields[0] != "KF2FrameCapRecovery1" || fields[1] != binding.value()) return invalid();
    std::array<std::size_t, 2> sizes{};
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        const auto field = fields[index + 2];
        const auto parsed = std::from_chars(field.data(), field.data() + field.size(), sizes[index]);
        if (field.empty() || parsed.ec != std::errc{} ||
            parsed.ptr != field.data() + field.size() || sizes[index] > kMaximumFrameCapConfigBytes) return invalid();
        const auto hash = fields[index + 4];
        if (hash.size() != 64U || hash.find_first_not_of("0123456789abcdef") != std::string_view::npos) return invalid();
    }
    const auto body = bytes.substr(offset);
    if (body.size() != sizes[0] + sizes[1]) return invalid();
    const auto checksum = security::sha256_hex(std::string{bytes.substr(0, checksum_offset)} + std::string{body});
    if (!checksum.has_value()) return Result<CapRecovery>::failure(checksum.error());
    if (fields[6] != checksum.value()) return invalid();
    return Result<CapRecovery>::success({
        {std::string{body.substr(0, sizes[0])}, std::string{body.substr(sizes[0])}},
        {std::string{fields[4]}, std::string{fields[5]}}});
}

Result<std::string> read_file(const std::filesystem::path& path) {
    return platform::windows::read_bounded_verified_file(
        path, kMaximumFrameCapConfigBytes);
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
                          int target_fps,
                          const std::array<std::string, 2>& expected) {
    const auto console_bytes = read_file(console_path);
    const auto game_bytes = read_file(game_path);
    if (!console_bytes.has_value()) {
        return Result<bool>::failure(console_bytes.error());
    }
    if (!game_bytes.has_value()) {
        return Result<bool>::failure(game_bytes.error());
    }
    if (console_bytes.value() != expected[0] || game_bytes.value() != expected[1]) {
        return Result<bool>::failure({ErrorCode::stale_data,
            L"KF2's native frame-cap files changed before readback", 0});
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

Result<bool> recover_frame_rate_cap(const GameInstallation& installation,
    const std::filesystem::path& recovery_root, bool game_running) {
    const auto journal = recovery_path(installation, recovery_root);
    const auto record = platform::windows::read_bounded_verified_file(
        journal, 2U * kMaximumFrameCapConfigBytes + 1024U);
    if (!record.has_value()) {
        if (record.error().code == ErrorCode::not_found) return Result<bool>::success(false);
        return Result<bool>::failure(recovery_error(record.error()));
    }
    if (record.value().empty()) return Result<bool>::success(false);
    if (game_running) return Result<bool>::failure({ErrorCode::recovery_required,
        L"Close KF2 before recovering its interrupted native FPS cap", 0});
    const auto snapshot = parse_recovery_record(installation, record.value());
    if (!snapshot.has_value()) return Result<bool>::failure(recovery_error(snapshot.error()));
    const auto paths = cap_paths(installation);
    std::optional<Error> failure;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto restored = [&]() -> Result<bool> {
            const auto current = read_file(paths[index]);
            if (!current.has_value()) return Result<bool>::failure(current.error());
            const auto& original = snapshot.value().originals[index];
            if (current.value() == original) return Result<bool>::success(true);
            const auto hash = security::sha256_hex(current.value());
            if (!hash.has_value()) return Result<bool>::failure(hash.error());
            if (hash.value() != snapshot.value().applied_hashes[index]) {
                return Result<bool>::failure({ErrorCode::stale_data,
                    L"Native FPS cap file has conflicting changes; they were preserved", 0});
            }
            const auto written = platform::windows::atomic_replace_utf8_if_unchanged(
                paths[index], current.value(), original);
            const auto readback = read_file(paths[index]);
            if (readback.has_value() && readback.value() == original) return Result<bool>::success(true);
            return Result<bool>::failure(!written.has_value() ? written.error() :
                !readback.has_value() ? readback.error() :
                Error{ErrorCode::stale_data, L"Native FPS cap rollback failed exact readback", 0});
        }();
        if (!restored.has_value() && !failure) failure = restored.error();
    }
    if (failure) return Result<bool>::failure(recovery_error(std::move(*failure)));
    const auto cleared = platform::windows::atomic_replace_utf8_if_unchanged(journal, record.value(), "");
    if (!cleared.has_value()) return Result<bool>::failure(recovery_error(cleared.error()));
    return Result<bool>::success(true);
}

Result<FrameRateCapResult> persist_frame_rate_cap(
    const GameInstallation& installation, int target_fps,
    const std::filesystem::path& recovery_root) {
    if (!optimizer::valid_target_fps(target_fps) ||
        installation.install_root.empty() ||
        installation.config_root.empty()) {
        return Result<FrameRateCapResult>::failure(
            {ErrorCode::invalid_argument,
             L"The requested KF2 frame cap is invalid", 0});
    }

    const auto recovered = recover_frame_rate_cap(installation, recovery_root);
    if (!recovered.has_value()) return Result<FrameRateCapResult>::failure(recovered.error());

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
    const std::array<std::string, 2> originals{original_console.value(), original_game.value()};
    const std::array<std::string, 2> proposed{console.value().serialize(), game.value().serialize()};
    const bool changed = write_console || write_game;
    std::string record;
    const auto journal = recovery_path(installation, recovery_root);
    if (changed) {
        for (const auto& bytes : proposed) {
            if (bytes.size() > kMaximumFrameCapConfigBytes) return Result<FrameRateCapResult>::failure({
                ErrorCode::access_denied, L"Native FPS cap output exceeds its size limit", 0});
        }
        const auto prepared = recovery_record(installation, originals, proposed);
        if (!prepared.has_value()) return Result<FrameRateCapResult>::failure(prepared.error());
        record = prepared.value();
        const auto armed = platform::windows::atomic_replace_utf8(journal, record);
        if (!armed.has_value()) return Result<FrameRateCapResult>::failure(armed.error());
        const auto verified_record = platform::windows::read_bounded_verified_file(
            journal, 2U * kMaximumFrameCapConfigBytes + 1024U);
        if (!verified_record.has_value() || verified_record.value() != record) {
            return Result<FrameRateCapResult>::failure(recovery_error(
                !verified_record.has_value() ? verified_record.error() :
                Error{ErrorCode::stale_data, L"Native FPS cap recovery record changed before commit", 0}));
        }
    }
    const auto rollback = [&](Error error) {
        const auto restored = recover_frame_rate_cap(installation, recovery_root);
        if (!restored.has_value()) {
            error = {ErrorCode::recovery_required,
                error.message + L"; " + restored.error().message, restored.error().native_code};
        }
        return Result<FrameRateCapResult>::failure(std::move(error));
    };
    if (write_console) {
        const auto written = platform::windows::atomic_replace_utf8_if_unchanged(
            console_path, originals[0], proposed[0]);
        if (!written.has_value()) {
            return rollback(written.error());
        }
    }
    if (write_game) {
        const auto written = platform::windows::atomic_replace_utf8_if_unchanged(
            game_path, originals[1], proposed[1]);
        if (!written.has_value()) {
            return rollback(written.error());
        }
    }

    const auto verified = verify_exact(console_path, game_path, target_fps, proposed);
    if (!verified.has_value()) {
        return changed ? rollback(verified.error()) : Result<FrameRateCapResult>::failure(verified.error());
    }
    if (changed) {
        const auto completed = platform::windows::atomic_replace_utf8_if_unchanged(journal, record, "");
        if (!completed.has_value()) return Result<FrameRateCapResult>::failure(recovery_error(completed.error()));
    }
    return Result<FrameRateCapResult>::success(
        {target_fps, changed});
}

}  // namespace kf2::game
