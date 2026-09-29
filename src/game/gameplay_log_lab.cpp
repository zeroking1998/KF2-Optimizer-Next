#include "kf2/game/gameplay_log_lab.hpp"

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
struct LoggingSetting final {
    std::wstring_view section;
    std::wstring_view key;
    std::wstring_view baseline_key;
};
constexpr std::array<LoggingSetting, 4> kLoggingSettings{{
    {kCountSection, kCountKey, L"OriginalLogAICount"},
    {kWaveSections[0], kWaveKey, L"OriginalLogWaveSpawnTimingShort"},
    {kWaveSections[1], kWaveKey, L"OriginalLogWaveSpawnTimingNormal"},
    {kWaveSections[2], kWaveKey, L"OriginalLogWaveSpawnTimingLong"},
}};
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
constexpr std::wstring_view kGraphicsViewportClient =
    L"KF2OptimizerTelemetry.KF2OptimizerGraphicsViewport";
constexpr std::wstring_view kNativeViewportClient =
    L"KFGame.KFGameViewportClient";
constexpr std::wstring_view kTelemetrySection =
    L"KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe";
constexpr std::wstring_view kAdaptiveCorpseStaggerKey =
    L"bAdaptiveCorpseStagger";
constexpr std::wstring_view kAdaptiveRuntimeEnabledKey =
    L"bAdaptiveRuntimeEnabled";
constexpr std::wstring_view kAdaptiveCorpseDebugMarkersKey =
    L"bAdaptiveCorpseDebugMarkers";
constexpr std::wstring_view kAdaptiveZedDebugMarkersKey =
    L"bAdaptiveZedDebugMarkers";
constexpr std::wstring_view kDetailedRuntimeDiagnosticsKey =
    L"bDetailedRuntimeDiagnostics";
constexpr std::wstring_view kAdaptiveCorpseMaximumKey =
    L"AdaptiveCorpseMaximum";
constexpr std::wstring_view kAdaptiveTargetFpsKey = L"AdaptiveTargetFPS";
constexpr std::wstring_view kAdaptiveQualityChangeBudgetKey =
    L"AdaptiveQualityChangeBudget";
constexpr std::wstring_view kAdaptiveControlTokenKey = L"AdaptiveControlToken";

Result<std::string> read_verified_ini(const std::filesystem::path& path) {
    return platform::windows::read_bounded_verified_file(
        path, kMaximumGameIniBytes);
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

Result<std::wstring> capture_logging_baseline(
    const std::optional<std::wstring>& value) {
    if (!value) return Result<std::wstring>::success(L"Missing");
    const auto normalized = normalized_boolean(*value);
    if (normalized == L"true") return Result<std::wstring>::success(L"True");
    if (normalized == L"false") return Result<std::wstring>::success(L"False");
    return Result<std::wstring>::failure(
        {ErrorCode::invalid_argument,
         L"KF2 gameplay logging setting is malformed", 0});
}

Result<std::wstring> normalize_logging_baseline(std::wstring value) {
    const auto normalized = normalized_boolean(std::move(value));
    if (normalized == L"true") return Result<std::wstring>::success(L"True");
    if (normalized == L"false") return Result<std::wstring>::success(L"False");
    if (normalized == L"missing") {
        return Result<std::wstring>::success(L"Missing");
    }
    return Result<std::wstring>::failure(
        {ErrorCode::invalid_argument,
         L"KF2 gameplay logging recovery baseline is malformed", 0});
}

Result<bool> restore_logging_setting(config::IniDocument& document,
                                     const LoggingSetting& setting,
                                     std::wstring_view baseline) {
    const auto current = document.find(setting.section, setting.key);
    const auto current_state = capture_logging_baseline(current);
    if (!current_state.has_value()) {
        return Result<bool>::failure(current_state.error());
    }
    if (current_state.value() != L"True" &&
        current_state.value() != baseline) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"KF2 gameplay logging changed after the optimizer session", 0});
    }

    config::ReplaceResult restored;
    if (baseline == L"Missing") {
        restored = document.remove_exact(setting.section, setting.key, L"True");
    } else {
        restored = document.replace(setting.section, setting.key, baseline);
    }
    if (restored.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 gameplay logging setting is ambiguous", 0});
    }
    if ((baseline == L"Missing" && current && !restored.changed) ||
        (baseline != L"Missing" && !current)) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"KF2 gameplay logging no longer matches optimizer ownership", 0});
    }
    return Result<bool>::success(restored.changed);
}

Result<bool> verify_logging_baseline(
    const config::IniDocument& document,
    const std::array<std::wstring, kLoggingSettings.size()>& baseline) {
    for (std::size_t index = 0; index < kLoggingSettings.size(); ++index) {
        auto copy = document;
        const auto restored = restore_logging_setting(
            copy, kLoggingSettings[index], baseline[index]);
        if (!restored.has_value() || restored.value()) {
            return Result<bool>::failure(
                {ErrorCode::io_failure,
                 L"KF2 gameplay logging baseline could not be verified", 0});
        }
    }
    return Result<bool>::success(true);
}

Result<bool> verify_stored_logging_baseline(
    const config::IniDocument& document,
    const std::array<std::wstring, kLoggingSettings.size()>& baseline) {
    auto section_check = document;
    const auto section = section_check.remove_section(kTelemetrySection);
    if (!section.changed || section.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"KF2 gameplay logging recovery section is ambiguous", 0});
    }
    for (std::size_t index = 0; index < kLoggingSettings.size(); ++index) {
        const auto& setting = kLoggingSettings[index];
        const auto stored = document.find(
            kTelemetrySection, setting.baseline_key);
        if (!stored) {
            return Result<bool>::failure(
                {ErrorCode::stale_data,
                 L"KF2 gameplay logging recovery baseline is incomplete", 0});
        }
        const auto normalized = normalize_logging_baseline(*stored);
        auto copy = document;
        const auto duplicate_check = copy.upsert(
            kTelemetrySection, setting.baseline_key, *stored);
        if (!normalized.has_value() ||
            normalized.value() != baseline[index] ||
            duplicate_check.shadowed_occurrences != 0) {
            return Result<bool>::failure(
                {ErrorCode::stale_data,
                 L"KF2 gameplay logging recovery baseline is invalid", 0});
        }
    }
    return Result<bool>::success(true);
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
    const auto read_boolean = [&](std::wstring_view key)
        -> Result<std::optional<bool>> {
        const auto value = document.value().find(kTelemetrySection, key);
        if (!value) {
            return Result<std::optional<bool>>::success(std::nullopt);
        }
        const auto duplicate_check = document.value().upsert(
            kTelemetrySection, key, *value);
        const auto normalized = normalized_boolean(*value);
        if (duplicate_check.shadowed_occurrences != 0 ||
            (normalized != L"true" && normalized != L"false")) {
            return Result<std::optional<bool>>::failure({
                ErrorCode::invalid_argument,
                L"Adaptive session policy is ambiguous or malformed", 0});
        }
        return Result<std::optional<bool>>::success(normalized == L"true");
    };
    auto corpse = read_integer(kAdaptiveCorpseMaximumKey);
    auto target = read_integer(kAdaptiveTargetFpsKey);
    auto budget = read_integer(kAdaptiveQualityChangeBudgetKey);
    auto runtime_enabled = read_boolean(kAdaptiveRuntimeEnabledKey);
    if (!corpse.has_value() || !target.has_value() || !budget.has_value() ||
        !runtime_enabled.has_value()) {
        const auto& error = !corpse.has_value() ? corpse.error()
            : !target.has_value() ? target.error()
            : !budget.has_value() ? budget.error()
                                  : runtime_enabled.error();
        return Result<std::optional<OfflineAdaptiveSessionPolicy>>::failure(
            error);
    }
    const bool any = corpse.value().has_value() ||
                     target.value().has_value() ||
                     budget.value().has_value() ||
                     runtime_enabled.value().has_value();
    if (!any) {
        return Result<std::optional<OfflineAdaptiveSessionPolicy>>::success(
            std::nullopt);
    }
    if (!corpse.value() || !target.value() || !budget.value() ||
        !runtime_enabled.value() ||
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
            *corpse.value(), *target.value(), *budget.value(),
            *runtime_enabled.value()});
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
    std::string_view adaptive_control_token,
    bool adaptive_zed_debug_markers,
    bool adaptive_runtime_enabled,
    bool detailed_runtime_diagnostics) {
    if (config_root.empty() || !config_root.is_absolute()) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Offline gameplay log configuration root is invalid", 0});
    }
    const bool configured_session = adaptive_corpse_maximum != 0 ||
        adaptive_target_fps != 0 || adaptive_corpse_debug_markers ||
        adaptive_zed_debug_markers || detailed_runtime_diagnostics ||
        !adaptive_control_token.empty();
    if ((configured_session &&
         (adaptive_corpse_maximum < 4 || adaptive_corpse_maximum > 2000 ||
          !optimizer::valid_target_fps(adaptive_target_fps) ||
          adaptive_quality_change_budget < 1 ||
          adaptive_quality_change_budget > 5 ||
          !valid_adaptive_control_token(adaptive_control_token))) ||
        (!configured_session && adaptive_corpse_stagger)) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Configured telemetry requires corpse maximum 4..2000, target FPS 30..240 and a valid control token",
             0});
    }
    const auto game_ini = config_root / L"KFGame.ini";
    const auto engine_ini = config_root / L"KFEngine.ini";
    auto parsed = parse_verified(game_ini);
    if (!parsed.has_value()) return Result<bool>::failure(parsed.error());
    auto engine = parse_verified(engine_ini);
    if (!engine.has_value()) return Result<bool>::failure(engine.error());

    std::array<std::wstring, kLoggingSettings.size()> logging_baseline;
    std::size_t stored_baseline_count = 0;
    for (std::size_t index = 0; index < kLoggingSettings.size(); ++index) {
        const auto stored = engine.value().find(
            kTelemetrySection, kLoggingSettings[index].baseline_key);
        if (!stored) continue;
        ++stored_baseline_count;
        const auto normalized = normalize_logging_baseline(*stored);
        if (!normalized.has_value()) {
            return Result<bool>::failure(normalized.error());
        }
        logging_baseline[index] = normalized.value();
    }
    if (stored_baseline_count != 0 &&
        stored_baseline_count != kLoggingSettings.size()) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"KF2 gameplay logging recovery baseline is incomplete", 0});
    }
    if (stored_baseline_count == kLoggingSettings.size()) {
        const auto verified_baseline = verify_stored_logging_baseline(
            engine.value(), logging_baseline);
        if (!verified_baseline.has_value()) {
            return Result<bool>::failure(verified_baseline.error());
        }
    }

    bool logging_baseline_changed = false;
    bool logging_changed = false;
    for (std::size_t index = 0; index < kLoggingSettings.size(); ++index) {
        const auto& setting = kLoggingSettings[index];
        const auto current = parsed.value().find(setting.section, setting.key);
        const auto current_state = capture_logging_baseline(current);
        if (!current_state.has_value()) {
            return Result<bool>::failure(current_state.error());
        }
        if (stored_baseline_count == 0) {
            logging_baseline[index] = current_state.value();
            const auto stored = engine.value().upsert(
                kTelemetrySection, setting.baseline_key,
                logging_baseline[index]);
            if (stored.shadowed_occurrences != 0) {
                return Result<bool>::failure(
                    {ErrorCode::invalid_argument,
                     L"KF2 gameplay logging recovery baseline is ambiguous",
                     0});
            }
            logging_baseline_changed =
                logging_baseline_changed || stored.changed;
        } else if (current_state.value() != L"True" &&
                   current_state.value() != logging_baseline[index]) {
            return Result<bool>::failure(
                {ErrorCode::stale_data,
                 L"KF2 gameplay logging changed after baseline capture", 0});
        }

        const auto staged = parsed.value().upsert(
            setting.section, setting.key, L"True");
        if (staged.shadowed_occurrences != 0) {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"KF2 gameplay logging setting is ambiguous", 0});
        }
        logging_changed = logging_changed || staged.changed;
    }
    const auto current_viewport = engine.value().find(
        kEngineSection, kViewportClientKey);
    if (!current_viewport ||
        (*current_viewport != kNativeViewportClient &&
         *current_viewport != kGraphicsViewportClient)) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"KF2 viewport-client setting is missing or owned by another provider",
             0});
    }
    const auto viewport_replaced = engine.value().replace(
        kEngineSection, kViewportClientKey, kGraphicsViewportClient);
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
    if (const auto current_runtime_enabled = engine.value().find(
            kTelemetrySection, kAdaptiveRuntimeEnabledKey);
        current_runtime_enabled) {
        const auto runtime_boolean = normalized_boolean(
            *current_runtime_enabled);
        if (runtime_boolean != L"true" && runtime_boolean != L"false") {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"Adaptive runtime setting is malformed", 0});
        }
    }
    const auto runtime_enabled_replaced = engine.value().upsert(
        kTelemetrySection, kAdaptiveRuntimeEnabledKey,
        adaptive_runtime_enabled ? L"True" : L"False");
    if (runtime_enabled_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Adaptive runtime setting is ambiguous", 0});
    }
    if (const auto current_zed_markers = engine.value().find(
            kTelemetrySection, kAdaptiveZedDebugMarkersKey);
        current_zed_markers) {
        const auto marker_boolean = normalized_boolean(*current_zed_markers);
        if (marker_boolean != L"true" && marker_boolean != L"false") {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"Adaptive Zed debug-marker setting is malformed", 0});
        }
    }
    const auto zed_markers_replaced = engine.value().upsert(
        kTelemetrySection, kAdaptiveZedDebugMarkersKey,
        adaptive_zed_debug_markers ? L"True" : L"False");
    if (zed_markers_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Adaptive Zed debug-marker setting is ambiguous", 0});
    }
    if (const auto current_diagnostics = engine.value().find(
            kTelemetrySection, kDetailedRuntimeDiagnosticsKey);
        current_diagnostics) {
        const auto diagnostics_boolean = normalized_boolean(
            *current_diagnostics);
        if (diagnostics_boolean != L"true" &&
            diagnostics_boolean != L"false") {
            return Result<bool>::failure(
                {ErrorCode::invalid_argument,
                 L"Detailed runtime-diagnostics setting is malformed", 0});
        }
    }
    const auto diagnostics_replaced = engine.value().upsert(
        kTelemetrySection, kDetailedRuntimeDiagnosticsKey,
        detailed_runtime_diagnostics ? L"True" : L"False");
    if (diagnostics_replaced.shadowed_occurrences != 0) {
        return Result<bool>::failure(
            {ErrorCode::invalid_argument,
             L"Detailed runtime-diagnostics setting is ambiguous", 0});
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
    const bool engine_changed = viewport_replaced.changed ||
        local_options_replaced.changed || runtime_path_appended.changed ||
        startup_package_removed.changed || stagger_replaced.changed ||
        runtime_enabled_replaced.changed || markers_replaced.changed ||
        zed_markers_replaced.changed || diagnostics_replaced.changed ||
        maximum_replaced.changed ||
        target_replaced.changed || quality_budget_replaced.changed ||
        control_token_replaced.changed || logging_baseline_changed;
    if (!logging_changed && !engine_changed) {
        return Result<bool>::success(false);
    }

    if (engine_changed) {
        const auto written = platform::windows::atomic_replace_utf8(
            engine_ini, engine.value().serialize());
        if (!written.has_value()) return Result<bool>::failure(written.error());
    }
    if (logging_changed) {
        auto verified_ownership = parse_verified(engine_ini);
        if (!verified_ownership.has_value()) {
            return Result<bool>::failure(verified_ownership.error());
        }
        const auto ownership_matches = verify_stored_logging_baseline(
            verified_ownership.value(), logging_baseline);
        if (!ownership_matches.has_value()) {
            return Result<bool>::failure(ownership_matches.error());
        }
        const auto written = platform::windows::atomic_replace_utf8(
            game_ini, parsed.value().serialize());
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
    const auto verified_runtime_enabled = verified_engine.has_value()
        ? verified_engine.value().find(
              kTelemetrySection, kAdaptiveRuntimeEnabledKey)
        : std::optional<std::wstring>{};
    const auto verified_maximum = verified_engine.has_value()
        ? verified_engine.value().find(
              kTelemetrySection, kAdaptiveCorpseMaximumKey)
        : std::optional<std::wstring>{};
    const auto verified_markers = verified_engine.has_value()
        ? verified_engine.value().find(
              kTelemetrySection, kAdaptiveCorpseDebugMarkersKey)
        : std::optional<std::wstring>{};
    const auto verified_zed_markers = verified_engine.has_value()
        ? verified_engine.value().find(
              kTelemetrySection, kAdaptiveZedDebugMarkersKey)
        : std::optional<std::wstring>{};
    const auto verified_diagnostics = verified_engine.has_value()
        ? verified_engine.value().find(
              kTelemetrySection, kDetailedRuntimeDiagnosticsKey)
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
    const auto verified_logging_baseline = verified_engine.has_value()
        ? verify_stored_logging_baseline(
              verified_engine.value(), logging_baseline)
        : Result<bool>::failure(
              {ErrorCode::not_found,
               L"KF2 gameplay logging recovery baseline is missing", 0});
    if (!verified_engine.has_value() || !verified_viewport ||
        *verified_viewport != kGraphicsViewportClient ||
        !verified_local_options || !verified_mutator_option.has_value() ||
        verified_mutator_option.value() != *verified_local_options ||
        !verified_runtime_path.changed ||
        verified_runtime_path.shadowed_occurrences != 0 ||
        verified_startup_package.changed ||
        verified_startup_package.shadowed_occurrences != 0 ||
        !verified_stagger ||
        normalized_boolean(*verified_stagger) !=
            (adaptive_corpse_stagger ? L"true" : L"false") ||
        !verified_runtime_enabled ||
        normalized_boolean(*verified_runtime_enabled) !=
            (adaptive_runtime_enabled ? L"true" : L"false") ||
        !verified_markers ||
        normalized_boolean(*verified_markers) !=
            (adaptive_corpse_debug_markers ? L"true" : L"false") ||
        !verified_zed_markers ||
        normalized_boolean(*verified_zed_markers) !=
            (adaptive_zed_debug_markers ? L"true" : L"false") ||
        !verified_diagnostics ||
        normalized_boolean(*verified_diagnostics) !=
            (detailed_runtime_diagnostics ? L"true" : L"false") ||
        !verified_maximum ||
        *verified_maximum != std::to_wstring(adaptive_corpse_maximum) ||
        !verified_target ||
        *verified_target != std::to_wstring(adaptive_target_fps) ||
        !verified_quality_budget ||
        *verified_quality_budget !=
            std::to_wstring(adaptive_quality_change_budget) ||
        !verified_control_token || *verified_control_token != control_token ||
        !verified_logging_baseline.has_value()) {
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

    std::array<std::wstring, kLoggingSettings.size()> logging_baseline;
    std::size_t stored_baseline_count = 0;
    for (std::size_t index = 0; index < kLoggingSettings.size(); ++index) {
        const auto stored = engine.value().find(
            kTelemetrySection, kLoggingSettings[index].baseline_key);
        if (!stored) continue;
        ++stored_baseline_count;
        const auto normalized = normalize_logging_baseline(*stored);
        if (!normalized.has_value()) {
            return Result<bool>::failure(normalized.error());
        }
        logging_baseline[index] = normalized.value();
    }
    if (stored_baseline_count != 0 &&
        stored_baseline_count != kLoggingSettings.size()) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"KF2 gameplay logging recovery baseline is incomplete", 0});
    }
    if (stored_baseline_count == kLoggingSettings.size()) {
        const auto verified_baseline = verify_stored_logging_baseline(
            engine.value(), logging_baseline);
        if (!verified_baseline.has_value()) {
            return Result<bool>::failure(verified_baseline.error());
        }
    }

    bool changed = false;
    const auto viewport = engine.value().find(
        kEngineSection, kViewportClientKey);
    if (viewport && (*viewport == kTelemetryViewportClient ||
                     *viewport == kGraphicsViewportClient)) {
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

    if (stored_baseline_count != kLoggingSettings.size()) {
        return Result<bool>::failure(
            {ErrorCode::stale_data,
             L"Stale optimizer configuration has no verified gameplay logging baseline",
             0});
    }

    const auto game_ini = config_root / L"KFGame.ini";
    auto game = parse_verified(game_ini);
    if (!game.has_value()) return Result<bool>::failure(game.error());
    bool game_changed = false;
    for (std::size_t index = 0; index < kLoggingSettings.size(); ++index) {
        const auto restored = restore_logging_setting(
            game.value(), kLoggingSettings[index], logging_baseline[index]);
        if (!restored.has_value()) {
            return Result<bool>::failure(restored.error());
        }
        game_changed = game_changed || restored.value();
    }
    if (game_changed) {
        const auto written = platform::windows::atomic_replace_utf8(
            game_ini, game.value().serialize());
        if (!written.has_value()) return Result<bool>::failure(written.error());
    }
    auto verified_game = parse_verified(game_ini);
    if (!verified_game.has_value()) {
        return Result<bool>::failure(verified_game.error());
    }
    const auto game_restored = verify_logging_baseline(
        verified_game.value(), logging_baseline);
    if (!game_restored.has_value()) {
        return Result<bool>::failure(game_restored.error());
    }

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
    auto finally_verified_game = parse_verified(game_ini);
    const auto final_game_restored = finally_verified_game.has_value()
        ? verify_logging_baseline(
              finally_verified_game.value(), logging_baseline)
        : Result<bool>::failure(finally_verified_game.error());
    if (!final_game_restored.has_value() ||
        (verified_viewport &&
         (*verified_viewport == kTelemetryViewportClient ||
          *verified_viewport == kGraphicsViewportClient)) ||
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
