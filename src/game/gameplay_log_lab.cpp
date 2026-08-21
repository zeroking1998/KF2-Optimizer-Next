#include "kf2/game/gameplay_log_lab.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <string>

#include "kf2/config/ini_document.hpp"
#include "kf2/optimizer/adaptive_stability.hpp"
#include "kf2/platform/windows/atomic_file.hpp"

namespace kf2::game {
namespace {

constexpr std::uintmax_t kMaximumGameIniBytes = 16U * 1024U * 1024U;
constexpr std::wstring_view kCountSection =
    L"KFGameContent.KFGameInfo_Survival";
constexpr std::wstring_view kCountKey = L"bLogAICount";
constexpr std::array<std::wstring_view, 3> kWaveSections{
    L"KFGame.KFAISpawnManager_Short",
    L"KFGame.KFAISpawnManager_Normal",
    L"KFGame.KFAISpawnManager_Long"};
constexpr std::wstring_view kWaveKey = L"bLogWaveSpawnTiming";
constexpr std::wstring_view kEngineSection = L"Engine.Engine";
constexpr std::wstring_view kViewportClientKey = L"GameViewportClientClassName";
constexpr std::wstring_view kTelemetryViewportClient =
    L"KF2OptimizerTelemetry.KF2OptimizerTelemetryViewport";
constexpr std::wstring_view kTelemetrySection =
    L"KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe";
constexpr std::wstring_view kAdaptiveCorpseStaggerKey =
    L"bAdaptiveCorpseStagger";
constexpr std::wstring_view kAdaptiveCorpseDebugMarkersKey =
    L"bAdaptiveCorpseDebugMarkers";
constexpr std::wstring_view kAdaptiveCorpseMaximumKey =
    L"AdaptiveCorpseMaximum";
constexpr std::wstring_view kAdaptiveTargetFpsKey = L"AdaptiveTargetFPS";
constexpr std::wstring_view kAdaptiveQualityChangeBudgetKey =
    L"AdaptiveQualityChangeBudget";

Result<std::string> read_verified_ini(const std::filesystem::path& path) {
    HANDLE file = CreateFileW(
        path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Result<std::string>::failure(
            {ErrorCode::not_found, L"Required KF2 INI was not found", GetLastError()});
    }
    BY_HANDLE_FILE_INFORMATION information{};
    LARGE_INTEGER size{};
    if (!GetFileInformationByHandle(file, &information) ||
        !GetFileSizeEx(file, &size) || size.QuadPart < 0 ||
        (information.dwFileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0 ||
        information.nNumberOfLinks != 1 ||
        static_cast<std::uintmax_t>(size.QuadPart) > kMaximumGameIniBytes) {
        const DWORD native = GetLastError();
        CloseHandle(file);
        return Result<std::string>::failure(
            {ErrorCode::access_denied,
             L"Required KF2 INI identity or size is unsafe", native});
    }
    std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD requested = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - offset, std::numeric_limits<DWORD>::max()));
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + offset, requested, &read, nullptr) ||
            read == 0) {
            const DWORD native = GetLastError();
            CloseHandle(file);
            return Result<std::string>::failure(
                {ErrorCode::io_failure, L"Required KF2 INI cannot be read", native});
        }
        offset += read;
    }
    CloseHandle(file);
    return Result<std::string>::success(std::move(bytes));
}

std::wstring normalized_boolean(std::wstring value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(),
        [](wchar_t character) { return !std::iswspace(character); }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
        [](wchar_t character) { return !std::iswspace(character); }).base(),
        value.end());
    std::transform(value.begin(), value.end(), value.begin(),
        [](wchar_t character) { return std::towlower(character); });
    return value;
}

Result<config::IniDocument> parse_verified(
    const std::filesystem::path& path) {
    auto bytes = read_verified_ini(path);
    if (!bytes.has_value()) {
        return Result<config::IniDocument>::failure(bytes.error());
    }
    auto document = config::IniDocument::parse(bytes.value());
    if (!document.has_value()) {
        return Result<config::IniDocument>::failure(document.error());
    }
    return document;
}

}  // namespace

Result<bool> enable_offline_gameplay_logging(
    const std::filesystem::path& config_root,
    bool adaptive_corpse_stagger,
    int adaptive_corpse_maximum,
    int adaptive_target_fps,
    bool adaptive_corpse_debug_markers,
    int adaptive_quality_change_budget) {
    if (config_root.empty() || !config_root.is_absolute()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Offline gameplay log configuration root is invalid", 0});
    }
    if ((adaptive_corpse_stagger &&
         (adaptive_corpse_maximum < 4 || adaptive_corpse_maximum > 2000 ||
           !optimizer::valid_target_fps(adaptive_target_fps) ||
           adaptive_quality_change_budget < 1 ||
           adaptive_quality_change_budget > 5)) ||
        (!adaptive_corpse_stagger &&
         (adaptive_corpse_maximum != 0 || adaptive_target_fps != 0 ||
          adaptive_corpse_debug_markers))) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Adaptive corpse maximum must be 4..2000 and target FPS 30..240 when Adaptive is active",
             0});
    }
    const auto game_ini = config_root / L"KFGame.ini";
    const auto engine_ini = config_root / L"KFEngine.ini";
    auto parsed = parse_verified(game_ini);
    if (!parsed.has_value()) return Result<bool>::failure(parsed.error());
    auto engine = parse_verified(engine_ini);
    if (!engine.has_value()) return Result<bool>::failure(engine.error());

    const auto current = parsed.value().find(kCountSection, kCountKey);
    if (!current) {
        return Result<bool>::failure(
            {ErrorCode::not_found,
             L"Verified KF2 AI-count logging setting was not found", 0});
    }
    const auto boolean = normalized_boolean(*current);
    if (boolean != L"true" && boolean != L"false") {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 AI-count logging setting is malformed", 0});
    }

    const auto replaced = parsed.value().replace(
        kCountSection, kCountKey, L"True");
    if (replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 AI-count logging setting is ambiguous", 0});
    }
    bool wave_changed = false;
    for (const auto wave_section : kWaveSections) {
        if (const auto current_wave =
                parsed.value().find(wave_section, kWaveKey);
            current_wave) {
            const auto wave_boolean = normalized_boolean(*current_wave);
            if (wave_boolean != L"true" && wave_boolean != L"false") {
                return Result<bool>::failure(
                    {ErrorCode::invalid_argument,
                     L"KF2 wave-timing logging setting is malformed", 0});
            }
        }
        const auto wave_replaced = parsed.value().upsert(
            wave_section, kWaveKey, L"True");
        if (wave_replaced.shadowed_occurrences != 0) {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"KF2 wave-timing logging setting is ambiguous", 0});
        }
        wave_changed = wave_changed || wave_replaced.changed;
    }
    const auto viewport_replaced = engine.value().replace(
        kEngineSection, kViewportClientKey, kTelemetryViewportClient);
    if (viewport_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 viewport-client setting is ambiguous", 0});
    }
    if (!engine.value().find(kEngineSection, kViewportClientKey)) {
        return Result<bool>::failure(
            {ErrorCode::not_found,
             L"Verified KF2 viewport-client setting was not found", 0});
    }
    if (const auto current_stagger = engine.value().find(
            kTelemetrySection, kAdaptiveCorpseStaggerKey);
        current_stagger) {
        const auto stagger_boolean = normalized_boolean(*current_stagger);
        if (stagger_boolean != L"true" && stagger_boolean != L"false") {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"Adaptive corpse-stagger setting is malformed", 0});
        }
    }
    const auto stagger_replaced = engine.value().upsert(
        kTelemetrySection, kAdaptiveCorpseStaggerKey,
        adaptive_corpse_stagger ? L"True" : L"False");
    if (stagger_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Adaptive corpse-stagger setting is ambiguous", 0});
    }
    const auto maximum_replaced = engine.value().upsert(
        kTelemetrySection, kAdaptiveCorpseMaximumKey,
        std::to_wstring(adaptive_corpse_maximum));
    if (maximum_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Adaptive corpse-maximum setting is ambiguous", 0});
    }
    if (const auto current_markers = engine.value().find(
            kTelemetrySection, kAdaptiveCorpseDebugMarkersKey);
        current_markers) {
        const auto marker_boolean = normalized_boolean(*current_markers);
        if (marker_boolean != L"true" && marker_boolean != L"false") {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"Adaptive corpse debug-marker setting is malformed", 0});
        }
    }
    const auto markers_replaced = engine.value().upsert(
        kTelemetrySection, kAdaptiveCorpseDebugMarkersKey,
        adaptive_corpse_debug_markers ? L"True" : L"False");
    if (markers_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Adaptive corpse debug-marker setting is ambiguous", 0});
    }
    const auto target_replaced = engine.value().upsert(
        kTelemetrySection, kAdaptiveTargetFpsKey,
        std::to_wstring(adaptive_target_fps));
    if (target_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
              L"Adaptive target-FPS setting is ambiguous", 0});
    }
    const auto quality_budget_replaced = engine.value().upsert(
        kTelemetrySection, kAdaptiveQualityChangeBudgetKey,
        std::to_wstring(adaptive_quality_change_budget));
    if (quality_budget_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Adaptive quality-change budget setting is ambiguous", 0});
    }
    if (!replaced.changed && !wave_changed && !viewport_replaced.changed &&
        !stagger_replaced.changed && !markers_replaced.changed &&
        !maximum_replaced.changed &&
        !target_replaced.changed && !quality_budget_replaced.changed) {
        return Result<bool>::success(false);
    }

    if (replaced.changed || wave_changed) {
        const auto written = platform::windows::atomic_replace_utf8(
            game_ini, parsed.value().serialize());
        if (!written.has_value()) return Result<bool>::failure(written.error());
    }
    if (viewport_replaced.changed || stagger_replaced.changed ||
        markers_replaced.changed || maximum_replaced.changed ||
        target_replaced.changed || quality_budget_replaced.changed) {
        const auto written = platform::windows::atomic_replace_utf8(
            engine_ini, engine.value().serialize());
        if (!written.has_value()) return Result<bool>::failure(written.error());
    }

    auto verified = parse_verified(game_ini);
    if (!verified.has_value()) return Result<bool>::failure(verified.error());
    const auto active = verified.value().find(kCountSection, kCountKey);
    bool all_waves_active = true;
    for (const auto wave_section : kWaveSections) {
        const auto active_wave = verified.value().find(wave_section, kWaveKey);
        all_waves_active = all_waves_active && active_wave &&
            normalized_boolean(*active_wave) == L"true";
    }
    if (!active || normalized_boolean(*active) != L"true" ||
        !all_waves_active) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"KF2 gameplay logging could not be verified after writing", 0});
    }
    auto verified_engine = parse_verified(engine_ini);
    const auto verified_viewport = verified_engine.has_value()
        ? verified_engine.value().find(kEngineSection, kViewportClientKey)
        : std::optional<std::wstring>{};
    const auto verified_stagger = verified_engine.has_value()
        ? verified_engine.value().find(
              kTelemetrySection, kAdaptiveCorpseStaggerKey)
        : std::optional<std::wstring>{};
    const auto verified_maximum = verified_engine.has_value()
        ? verified_engine.value().find(
              kTelemetrySection, kAdaptiveCorpseMaximumKey)
        : std::optional<std::wstring>{};
    const auto verified_markers = verified_engine.has_value()
        ? verified_engine.value().find(
              kTelemetrySection, kAdaptiveCorpseDebugMarkersKey)
        : std::optional<std::wstring>{};
    const auto verified_target = verified_engine.has_value()
        ? verified_engine.value().find(kTelemetrySection, kAdaptiveTargetFpsKey)
        : std::optional<std::wstring>{};
    const auto verified_quality_budget = verified_engine.has_value()
        ? verified_engine.value().find(
              kTelemetrySection, kAdaptiveQualityChangeBudgetKey)
        : std::optional<std::wstring>{};
    if (!verified_engine.has_value() || !verified_viewport ||
        *verified_viewport != kTelemetryViewportClient || !verified_stagger ||
        normalized_boolean(*verified_stagger) !=
            (adaptive_corpse_stagger ? L"true" : L"false") ||
        !verified_markers ||
        normalized_boolean(*verified_markers) !=
            (adaptive_corpse_debug_markers ? L"true" : L"false") ||
        !verified_maximum ||
        *verified_maximum != std::to_wstring(adaptive_corpse_maximum) ||
        !verified_target ||
        *verified_target != std::to_wstring(adaptive_target_fps) ||
        !verified_quality_budget ||
        *verified_quality_budget !=
            std::to_wstring(adaptive_quality_change_budget)) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"KF2 offline telemetry viewport policy could not be verified after writing",
             0});
    }
    return Result<bool>::success(true);
}

}  // namespace kf2::game
