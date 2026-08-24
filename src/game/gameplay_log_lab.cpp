#include "kf2/game/gameplay_log_lab.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <string>

#include "kf2/config/ini_document.hpp"
#include "kf2/game/adaptive_control_client.hpp"
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
constexpr std::wstring_view kCoreSystemSection = L"Core.System";
constexpr std::wstring_view kRuntimePathsKey = L"Paths";
constexpr std::wstring_view kScriptPathsKey = L"ScriptPaths";
constexpr std::wstring_view kNativeScriptPath = L"..\\..\\KFGame\\Script";
constexpr std::wstring_view kLegacyPublishedRuntimePath =
    L"..\\..\\KFGame\\Published\\BrewedPC";
constexpr std::wstring_view kUrlSection = L"URL";
constexpr std::wstring_view kLocalOptionsKey = L"LocalOptions";
constexpr std::wstring_view kTelemetryMutator =
    L"KF2OptimizerTelemetry.KF2OptimizerTelemetryMutator";
constexpr std::wstring_view kTelemetryMutatorOption =
    L"?Mutator=KF2OptimizerTelemetry.KF2OptimizerTelemetryMutator";
constexpr std::wstring_view kGameEngineSection = L"Engine.GameEngine";
constexpr std::wstring_view kServerActorsKey = L"ServerActors";
constexpr std::wstring_view kTelemetryBootstrapActor =
    L"KF2OptimizerTelemetry.KF2OptimizerTelemetryBootstrap";
constexpr std::wstring_view kStartupPackagesSection =
    L"Engine.StartupPackages";
constexpr std::wstring_view kStartupPackageKey = L"Package";
constexpr std::wstring_view kTelemetryPackage = L"KF2OptimizerTelemetry";
constexpr std::wstring_view kEngineSection = L"Engine.Engine";
constexpr std::wstring_view kViewportClientKey = L"GameViewportClientClassName";
constexpr std::wstring_view kTelemetryViewportClient =
    L"KF2OptimizerTelemetry.KF2OptimizerTelemetryViewport";
constexpr std::wstring_view kNativeViewportClient =
    L"KFGame.KFGameViewportClient";
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
constexpr std::wstring_view kAdaptiveControlTokenKey = L"AdaptiveControlToken";

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

Result<std::optional<OfflineAdaptiveSessionPolicy>>
read_offline_adaptive_session_policy(
    const std::filesystem::path& config_root) {
    if (config_root.empty() || !config_root.is_absolute()) {
        return Result<std::optional<OfflineAdaptiveSessionPolicy>>::failure({
            ErrorCode::invalid_argument,
            L"KF2 configuration root must be absolute", 0});
    }
    auto document = parse_verified(config_root / L"KFEngine.ini");
    if (!document.has_value()) {
        return Result<std::optional<OfflineAdaptiveSessionPolicy>>::failure(
            document.error());
    }
    const auto read_integer = [&](std::wstring_view key)
        -> Result<std::optional<int>> {
        const auto value = document.value().find(kTelemetrySection, key);
        if (!value) {
            return Result<std::optional<int>>::success(std::nullopt);
        }
        const auto duplicate_check = document.value().upsert(
            kTelemetrySection, key, *value);
        if (duplicate_check.shadowed_occurrences != 0 || value->empty()) {
            return Result<std::optional<int>>::failure({
                ErrorCode::invalid_argument,
                L"Adaptive session policy is ambiguous or malformed", 0});
        }
        int parsed = 0;
        for (const wchar_t character : *value) {
            if (character < L'0' || character > L'9' ||
                parsed > (std::numeric_limits<int>::max() - 9) / 10) {
                return Result<std::optional<int>>::failure({
                    ErrorCode::invalid_argument,
                    L"Adaptive session policy is malformed", 0});
            }
            parsed = parsed * 10 + static_cast<int>(character - L'0');
        }
        return Result<std::optional<int>>::success(parsed);
    };
    auto corpse = read_integer(kAdaptiveCorpseMaximumKey);
    auto target = read_integer(kAdaptiveTargetFpsKey);
    auto budget = read_integer(kAdaptiveQualityChangeBudgetKey);
    if (!corpse.has_value() || !target.has_value() || !budget.has_value()) {
        const auto& error = !corpse.has_value() ? corpse.error()
            : !target.has_value() ? target.error() : budget.error();
        return Result<std::optional<OfflineAdaptiveSessionPolicy>>::failure(
            error);
    }
    const bool any = corpse.value().has_value() ||
                     target.value().has_value() ||
                     budget.value().has_value();
    if (!any) {
        return Result<std::optional<OfflineAdaptiveSessionPolicy>>::success(
            std::nullopt);
    }
    if (!corpse.value() || !target.value() || !budget.value() ||
        *corpse.value() < 4 || *corpse.value() > 2000 ||
        !optimizer::valid_target_fps(*target.value()) ||
        *budget.value() < 1 || *budget.value() > 5) {
        return Result<std::optional<OfflineAdaptiveSessionPolicy>>::failure({
            ErrorCode::invalid_argument,
            L"Adaptive session policy is incomplete or outside its safe bounds",
            0});
    }
    return Result<std::optional<OfflineAdaptiveSessionPolicy>>::success(
        OfflineAdaptiveSessionPolicy{
            *corpse.value(), *target.value(), *budget.value()});
}

std::wstring lower_copy(std::wstring_view value) {
    std::wstring lowered{value};
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
        [](wchar_t character) { return std::towlower(character); });
    return lowered;
}

Result<std::wstring> stage_telemetry_mutator_option(
    std::wstring_view current) {
    const auto lowered = lower_copy(current);
    const auto option = lower_copy(kTelemetryMutatorOption);
    if (lowered.ends_with(option)) {
        return Result<std::wstring>::success(std::wstring{current});
    }
    if (lowered.find(L"mutator=") != std::wstring::npos) {
        return Result<std::wstring>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 already has a user-selected local mutator; it was preserved",
             0});
    }
    std::wstring staged{current};
    if (!staged.empty() && staged.back() == L'?') {
        staged.pop_back();
    }
    staged.append(kTelemetryMutatorOption);
    return Result<std::wstring>::success(std::move(staged));
}

Result<std::wstring> remove_telemetry_mutator_option(
    std::wstring_view current) {
    const auto lowered = lower_copy(current);
    const auto option = lower_copy(kTelemetryMutatorOption);
    if (!lowered.ends_with(option)) {
        if (lowered.find(lower_copy(kTelemetryMutator)) != std::wstring::npos) {
            return Result<std::wstring>::failure(
                {ErrorCode::invalid_argument,
                 L"KF2 Optimizer local-mutator option is ambiguous", 0});
        }
        return Result<std::wstring>::success(std::wstring{current});
    }
    return Result<std::wstring>::success(std::wstring{
        current.substr(0, current.size() - kTelemetryMutatorOption.size())});
}

Result<bool> enable_offline_gameplay_logging(
    const std::filesystem::path& config_root,
    bool adaptive_corpse_stagger,
    int adaptive_corpse_maximum,
    int adaptive_target_fps,
    bool adaptive_corpse_debug_markers,
    int adaptive_quality_change_budget,
    std::string_view adaptive_control_token) {
    if (config_root.empty() || !config_root.is_absolute()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Offline gameplay log configuration root is invalid", 0});
    }
    if ((adaptive_corpse_stagger &&
         (adaptive_corpse_maximum < 4 || adaptive_corpse_maximum > 2000 ||
           !optimizer::valid_target_fps(adaptive_target_fps) ||
           adaptive_quality_change_budget < 1 ||
           adaptive_quality_change_budget > 5 ||
           !valid_adaptive_control_token(adaptive_control_token))) ||
        (!adaptive_corpse_stagger &&
         (adaptive_corpse_maximum != 0 || adaptive_target_fps != 0 ||
          adaptive_corpse_debug_markers ||
          !adaptive_control_token.empty()))) {
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
    if (current) {
        const auto boolean = normalized_boolean(*current);
        if (boolean != L"true" && boolean != L"false") {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"KF2 AI-count logging setting is malformed", 0});
        }
    }

    const auto replaced = parsed.value().upsert(
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
    const auto current_viewport = engine.value().find(
        kEngineSection, kViewportClientKey);
    if (!current_viewport ||
        (*current_viewport != kNativeViewportClient &&
         *current_viewport != kTelemetryViewportClient)) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 viewport-client setting is missing or owned by another provider",
             0});
    }
    const auto viewport_replaced = engine.value().replace(
        kEngineSection, kViewportClientKey, kNativeViewportClient);
    if (viewport_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 viewport-client setting is ambiguous", 0});
    }
    const auto current_local_options = engine.value().find(
        kUrlSection, kLocalOptionsKey);
    if (!current_local_options) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 local URL options are missing or ambiguous", 0});
    }
    const auto staged_local_options = stage_telemetry_mutator_option(
        *current_local_options);
    if (!staged_local_options.has_value()) {
        return Result<bool>::failure(staged_local_options.error());
    }
    const auto local_options_replaced = engine.value().replace(
        kUrlSection, kLocalOptionsKey, staged_local_options.value());
    if (local_options_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 local URL options are ambiguous", 0});
    }
    const auto published_runtime_path =
        (config_root.parent_path() / L"Published" / L"BrewedPC")
            .lexically_normal().wstring();
    const auto runtime_path_appended = engine.value().append_unique(
        kCoreSystemSection, kRuntimePathsKey, published_runtime_path);
    if (runtime_path_appended.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 runtime-package search path is ambiguous", 0});
    }
    auto runtime_path_check = engine.value();
    const auto runtime_path_present = runtime_path_check.remove_exact(
        kCoreSystemSection, kRuntimePathsKey, published_runtime_path);
    if (!runtime_path_present.changed ||
        runtime_path_present.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::not_found,
             L"Verified KF2 Published runtime path could not be staged", 0});
    }
    const auto startup_package_removed = engine.value().remove_exact(
        kStartupPackagesSection, kStartupPackageKey, kTelemetryPackage);
    if (startup_package_removed.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 startup-package configuration is ambiguous", 0});
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
    const std::wstring control_token(
        adaptive_control_token.begin(), adaptive_control_token.end());
    const auto control_token_replaced = engine.value().upsert(
        kTelemetrySection, kAdaptiveControlTokenKey, control_token);
    if (control_token_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Adaptive control-token setting is ambiguous", 0});
    }
    if (!replaced.changed && !wave_changed && !viewport_replaced.changed &&
        !local_options_replaced.changed &&
        !runtime_path_appended.changed &&
        !startup_package_removed.changed &&
        !stagger_replaced.changed && !markers_replaced.changed &&
        !maximum_replaced.changed &&
        !target_replaced.changed && !quality_budget_replaced.changed &&
        !control_token_replaced.changed) {
        return Result<bool>::success(false);
    }

    if (replaced.changed || wave_changed) {
        const auto written = platform::windows::atomic_replace_utf8(
            game_ini, parsed.value().serialize());
        if (!written.has_value()) return Result<bool>::failure(written.error());
    }
    if (viewport_replaced.changed || local_options_replaced.changed ||
        runtime_path_appended.changed || startup_package_removed.changed ||
        stagger_replaced.changed ||
        markers_replaced.changed || maximum_replaced.changed ||
        target_replaced.changed || quality_budget_replaced.changed ||
        control_token_replaced.changed) {
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
    auto verified_startup_package_document = verified_engine.has_value()
        ? std::optional<config::IniDocument>{verified_engine.value()}
        : std::nullopt;
    const auto verified_startup_package = verified_startup_package_document
        ? verified_startup_package_document->remove_exact(
              kStartupPackagesSection, kStartupPackageKey, kTelemetryPackage)
        : config::ReplaceResult{};
    auto verified_runtime_path_document = verified_engine.has_value()
        ? std::optional<config::IniDocument>{verified_engine.value()}
        : std::nullopt;
    const auto verified_runtime_path = verified_runtime_path_document
        ? verified_runtime_path_document->remove_exact(
              kCoreSystemSection, kRuntimePathsKey, published_runtime_path)
        : config::ReplaceResult{};
    const auto verified_viewport = verified_engine.has_value()
        ? verified_engine.value().find(kEngineSection, kViewportClientKey)
        : std::optional<std::wstring>{};
    const auto verified_local_options = verified_engine.has_value()
        ? verified_engine.value().find(kUrlSection, kLocalOptionsKey)
        : std::optional<std::wstring>{};
    const auto verified_mutator_option = verified_local_options
        ? stage_telemetry_mutator_option(*verified_local_options)
        : Result<std::wstring>::failure(
              {ErrorCode::not_found, L"KF2 local URL options are missing", 0});
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
    const auto verified_control_token = verified_engine.has_value()
        ? verified_engine.value().find(
              kTelemetrySection, kAdaptiveControlTokenKey)
        : std::optional<std::wstring>{};
    if (!verified_engine.has_value() || !verified_viewport ||
        *verified_viewport != kNativeViewportClient ||
        !verified_local_options || !verified_mutator_option.has_value() ||
        verified_mutator_option.value() != *verified_local_options ||
        !verified_runtime_path.changed ||
        verified_runtime_path.shadowed_occurrences != 0 ||
        verified_startup_package.changed ||
        verified_startup_package.shadowed_occurrences != 0 ||
        !verified_stagger ||
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
            std::to_wstring(adaptive_quality_change_budget) ||
        !verified_control_token || *verified_control_token != control_token) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"KF2 offline telemetry bootstrap policy could not be verified after writing",
             0});
    }
    return Result<bool>::success(true);
}

Result<bool> cleanup_stale_offline_gameplay_configuration(
    const std::filesystem::path& config_root, bool game_running) {
    if (game_running) {
        return Result<bool>::failure(
            {ErrorCode::access_denied,
             L"Running KF2 configuration cannot be repaired", 0});
    }
    if (config_root.empty() || !config_root.is_absolute()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Offline gameplay configuration root is invalid", 0});
    }

    const auto engine_ini = config_root / L"KFEngine.ini";
    auto engine = parse_verified(engine_ini);
    if (!engine.has_value()) return Result<bool>::failure(engine.error());

    bool changed = false;
    const auto viewport = engine.value().find(
        kEngineSection, kViewportClientKey);
    if (viewport && *viewport == kTelemetryViewportClient) {
        const auto restored = engine.value().replace(
            kEngineSection, kViewportClientKey, kNativeViewportClient);
        if (restored.shadowed_occurrences != 0) {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"KF2 viewport-client setting is ambiguous", 0});
        }
        changed = changed || restored.changed;
    }

    const auto local_options = engine.value().find(
        kUrlSection, kLocalOptionsKey);
    if (local_options) {
        const auto cleaned_options = remove_telemetry_mutator_option(
            *local_options);
        if (!cleaned_options.has_value()) {
            return Result<bool>::failure(cleaned_options.error());
        }
        const auto options_restored = engine.value().replace(
            kUrlSection, kLocalOptionsKey, cleaned_options.value());
        if (options_restored.shadowed_occurrences != 0) {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"KF2 local URL options are ambiguous", 0});
        }
        changed = changed || options_restored.changed;
    }

    const auto server_actor_removed = engine.value().remove_exact(
        kGameEngineSection, kServerActorsKey, kTelemetryBootstrapActor);
    if (server_actor_removed.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 Optimizer server-actor entry is ambiguous", 0});
    }
    changed = changed || server_actor_removed.changed;

    const auto startup_package_removed = engine.value().remove_exact(
        kStartupPackagesSection, kStartupPackageKey, kTelemetryPackage);
    if (startup_package_removed.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 Optimizer startup-package entry is ambiguous", 0});
    }
    changed = changed || startup_package_removed.changed;

    const auto absolute_optimizer_runtime_path =
        (config_root.parent_path() / L"Published" / L"BrewedPC")
            .lexically_normal();
    const auto runtime_path_removed = engine.value().remove_exact(
        kCoreSystemSection, kRuntimePathsKey,
        absolute_optimizer_runtime_path.wstring());
    if (runtime_path_removed.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 Optimizer runtime-package path is ambiguous", 0});
    }
    changed = changed || runtime_path_removed.changed;
    const auto legacy_runtime_path_removed = engine.value().remove_exact(
        kCoreSystemSection, kRuntimePathsKey, kLegacyPublishedRuntimePath);
    if (legacy_runtime_path_removed.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Legacy KF2 Optimizer runtime-package path is ambiguous", 0});
    }
    changed = changed || legacy_runtime_path_removed.changed;

    const auto absolute_optimizer_script_path =
        (config_root.parent_path() / L"Published" / L"BrewedPC")
            .lexically_normal();
    const auto script_path = engine.value().find(
        kCoreSystemSection, kScriptPathsKey);
    const bool optimizer_script_path = script_path && (
        std::filesystem::path{*script_path}.lexically_normal() ==
            std::filesystem::path{kLegacyPublishedRuntimePath}.lexically_normal() ||
        std::filesystem::path{*script_path}.lexically_normal() ==
            absolute_optimizer_script_path);
    if (optimizer_script_path) {
        const auto restored = engine.value().replace(
            kCoreSystemSection, kScriptPathsKey, kNativeScriptPath);
        if (restored.shadowed_occurrences != 0) {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"KF2 script-package search path is ambiguous", 0});
        }
        changed = changed || restored.changed;
    }

    const auto removed = engine.value().remove_section(kTelemetrySection);
    if (removed.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 Optimizer telemetry section is ambiguous", 0});
    }
    changed = changed || removed.changed;
    if (!changed) return Result<bool>::success(false);

    const auto written = platform::windows::atomic_replace_utf8(
        engine_ini, engine.value().serialize());
    if (!written.has_value()) return Result<bool>::failure(written.error());

    auto verified = parse_verified(engine_ini);
    if (!verified.has_value()) return Result<bool>::failure(verified.error());
    const auto verified_viewport = verified.value().find(
        kEngineSection, kViewportClientKey);
    const auto verified_local_options = verified.value().find(
        kUrlSection, kLocalOptionsKey);
    const auto verified_cleaned_options = verified_local_options
        ? remove_telemetry_mutator_option(*verified_local_options)
        : Result<std::wstring>::success(L"");
    auto verified_server_actor_document = verified.value();
    const auto verified_server_actor = verified_server_actor_document.remove_exact(
        kGameEngineSection, kServerActorsKey, kTelemetryBootstrapActor);
    const auto verified_script_path = verified.value().find(
        kCoreSystemSection, kScriptPathsKey);
    auto verified_runtime_path_document = verified.value();
    const auto verified_runtime_path = verified_runtime_path_document.remove_exact(
        kCoreSystemSection, kRuntimePathsKey,
        absolute_optimizer_runtime_path.wstring());
    auto verified_legacy_runtime_path_document = verified.value();
    const auto verified_legacy_runtime_path =
        verified_legacy_runtime_path_document.remove_exact(
            kCoreSystemSection, kRuntimePathsKey, kLegacyPublishedRuntimePath);
    if ((verified_viewport &&
         *verified_viewport == kTelemetryViewportClient) ||
        !verified_cleaned_options.has_value() ||
        (verified_local_options &&
         verified_cleaned_options.value() != *verified_local_options) ||
        verified_server_actor.changed ||
        verified_server_actor.shadowed_occurrences != 0 ||
        verified_runtime_path.changed ||
        verified_runtime_path.shadowed_occurrences != 0 ||
        verified_legacy_runtime_path.changed ||
        verified_legacy_runtime_path.shadowed_occurrences != 0 ||
        [&] {
            auto document = verified.value();
            const auto startup_package = document.remove_exact(
                kStartupPackagesSection, kStartupPackageKey,
                kTelemetryPackage);
            return startup_package.changed ||
                   startup_package.shadowed_occurrences != 0;
        }() ||
        (verified_script_path && (
         std::filesystem::path{*verified_script_path}.lexically_normal() ==
             std::filesystem::path{kLegacyPublishedRuntimePath}.lexically_normal() ||
         std::filesystem::path{*verified_script_path}.lexically_normal() ==
             absolute_optimizer_script_path)) ||
        verified.value().find(kTelemetrySection, kAdaptiveControlTokenKey) ||
        verified.value().find(kTelemetrySection, kAdaptiveTargetFpsKey)) {
        return Result<bool>::failure(
            {ErrorCode::io_failure,
             L"Stale KF2 Optimizer telemetry configuration remains after repair",
             0});
    }
    return Result<bool>::success(true);
}

}  // namespace kf2::game
