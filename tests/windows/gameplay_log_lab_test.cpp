#include <Windows.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>

#include "kf2/game/gameplay_log_lab.hpp"

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ':' << __LINE__                            \
                      << ": check failed: " #condition << '\n';                \
            return EXIT_FAILURE;                                                \
        }                                                                       \
    } while (false)

namespace {

void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}};
}

std::string normalize_newlines(std::string text) {
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

std::size_t count_occurrences(std::string_view text, std::string_view needle) {
    std::size_t count = 0;
    for (auto offset = text.find(needle); offset != std::string_view::npos;
         offset = text.find(needle, offset + needle.size())) {
        ++count;
    }
    return count;
}

}  // namespace

int main() {
    namespace fs = std::filesystem;
    constexpr std::string_view control_token =
        "0123456789abcdef0123456789abcdef";
    const auto telemetry_source = normalize_newlines(
        read_bytes(KF2_TELEMETRY_SOURCE));
    const auto mutator_source = normalize_newlines(
        read_bytes(KF2_TELEMETRY_MUTATOR_SOURCE));
    const auto interaction_source = normalize_newlines(
        read_bytes(KF2_TELEMETRY_INTERACTION_SOURCE));
    const auto listener_source = read_bytes(KF2_ADAPTIVE_LISTENER_SOURCE);
    const auto connection_source = read_bytes(KF2_ADAPTIVE_CONNECTION_SOURCE);
    const auto graphics_source = normalize_newlines(
        read_bytes(KF2_ADAPTIVE_GRAPHICS_SOURCE));
    CHECK(telemetry_source.find("AdaptiveControlToken") != std::string::npos);
    CHECK(telemetry_source.find("ValidAdaptiveControlToken") !=
          std::string::npos);
    CHECK(telemetry_source.find("KF2OPT_ADAPTIVE_QUALITY state=applied") !=
          std::string::npos);
    CHECK(telemetry_source.find("Resource ~= \"overdraw\"") !=
          std::string::npos);
    CHECK(telemetry_source.find("Resource ~= \"effects\"") !=
          std::string::npos);
    CHECK(telemetry_source.find("var globalconfig bool bAdaptiveRuntimeEnabled") !=
          std::string::npos);
    CHECK(telemetry_source.find("function bool SetAdaptiveRuntimeEnabled") !=
          std::string::npos);
    CHECK(telemetry_source.find("function int WakeAdaptiveDistanceSleptCorpseBatch") !=
          std::string::npos);
    CHECK(telemetry_source.find("function BeginAdaptiveDistanceSleepRelease") !=
          std::string::npos);
    CHECK(telemetry_source.find("WakeCount < 8") != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_ADAPTIVE_MODE state=disabled readback=verified") !=
          std::string::npos);
    const auto adaptive_runtime_function = telemetry_source.find(
        "function bool SetAdaptiveRuntimeEnabled");
    const auto adaptive_control_function = telemetry_source.find(
        "function bool ApplyAdaptiveResourceControl");
    CHECK(adaptive_runtime_function != std::string::npos);
    CHECK(adaptive_control_function != std::string::npos);
    CHECK(telemetry_source.substr(
              adaptive_runtime_function,
              adaptive_control_function - adaptive_runtime_function).find(
                  "ClearTimer(nameof(SampleTelemetry), self)") ==
          std::string::npos);
    CHECK(interaction_source.find(
        "var bool bProcessAdaptiveRuntimeEnabled") != std::string::npos);
    CHECK(interaction_source.find(
        "CurrentProbe.SetAdaptiveRuntimeEnabled(") != std::string::npos);
    CHECK(connection_source.find(
        "CurrentInteraction.SetProcessAdaptiveRuntimeEnabled(") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "class'KF2OptimizerAdaptiveControlListener'") != std::string::npos);
    CHECK(interaction_source.find("var transient WorldInfo ActiveWorld") ==
          std::string::npos);
    CHECK(interaction_source.find("var bool bGameSessionEnding") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "var KF2OptimizerAdaptiveGraphicsState "
        "ProcessAdaptiveGraphicsState;") != std::string::npos);
    CHECK(interaction_source.find(
        "function KF2OptimizerAdaptiveGraphicsState "
        "GetProcessAdaptiveGraphicsState()") != std::string::npos);
    CHECK(interaction_source.find(
        "ProcessAdaptiveGraphicsState = new(self)\n"
        "            class'KF2OptimizerAdaptiveGraphicsState'") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "CurrentProbe.AdaptiveGraphicsState =\n"
        "            GetProcessAdaptiveGraphicsState()") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "ProcessAdaptiveGraphicsState = None") == std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveGraphicsState = new(self)") == std::string::npos);
    const auto restore_adaptive_graphics = telemetry_source.find(
        "function RestoreAdaptiveGraphics()");
    const auto select_staggered_corpse = telemetry_source.find(
        "function int SelectStaggeredCorpse(");
    CHECK(restore_adaptive_graphics != std::string::npos);
    CHECK(select_staggered_corpse != std::string::npos);
    CHECK(restore_adaptive_graphics < select_staggered_corpse);
    CHECK(telemetry_source.find("AdaptiveGraphicsState = None",
        restore_adaptive_graphics) >= select_staggered_corpse);
    const auto interaction_tick = interaction_source.find(
        "event Tick(float DeltaTime)");
    const auto interaction_post_render = interaction_source.find(
        "event PostRender(Canvas MarkerCanvas)");
    CHECK(interaction_tick != std::string::npos);
    CHECK(interaction_post_render != std::string::npos);
    CHECK(interaction_source.find(
        "if (bGameSessionEnding)", interaction_tick) <
          interaction_post_render);
    CHECK(interaction_source.find(
        "if (bGameSessionEnding)", interaction_post_render) !=
          std::string::npos);
    CHECK(interaction_source.find("bGameSessionEnding = true",
        interaction_source.find("function NotifyGameSessionEnded()")) !=
          std::string::npos);
    CHECK(interaction_source.find("CurrentProbe.QuiesceForWorldTeardown()",
        interaction_source.find("function NotifyGameSessionEnded()")) !=
          std::string::npos);
    CHECK(interaction_source.find("function NotifyPlayerAdded(") !=
          std::string::npos);
    const auto prepare_for_world = interaction_source.find(
        "function PrepareForGameplayWorld()");
    const auto session_ended = interaction_source.find(
        "function NotifyGameSessionEnded()");
    const auto player_added = interaction_source.find(
        "function NotifyPlayerAdded(");
    CHECK(prepare_for_world != std::string::npos);
    CHECK(session_ended != std::string::npos);
    CHECK(player_added != std::string::npos);
    CHECK(interaction_source.find("bGameSessionEnding = false",
        prepare_for_world) < session_ended);
    CHECK(interaction_source.find("state=rearmed", prepare_for_world) <
          session_ended);
    CHECK(interaction_source.find("if (bGameSessionEnding)", session_ended) <
          player_added);
    CHECK(interaction_source.find("bGameSessionEnding = false",
        player_added) == std::string::npos);
    CHECK(mutator_source.find("InsertInteraction(CurrentInteraction)") !=
          std::string::npos);
    const auto interaction_path = mutator_source.find(
        "InteractionPath = PathName(CurrentViewport)$\n"
        "        \".KF2OptimizerTelemetryInteraction\"");
    const auto interaction_lookup = mutator_source.find(
        "FindObject(InteractionPath,\n"
        "            class'KF2OptimizerTelemetryInteraction')");
    const auto existing_interaction = mutator_source.find(
        "state=ready interaction=existing");
    const auto create_interaction = mutator_source.find(
        "new(CurrentViewport, \"KF2OptimizerTelemetryInteraction\")");
    const auto insert_interaction = mutator_source.find(
        "InsertInteraction(CurrentInteraction)");
    CHECK(interaction_path != std::string::npos);
    CHECK(interaction_lookup != std::string::npos);
    CHECK(existing_interaction != std::string::npos);
    CHECK(create_interaction != std::string::npos);
    CHECK(insert_interaction != std::string::npos);
    CHECK(interaction_path < interaction_lookup);
    CHECK(interaction_lookup < existing_interaction);
    CHECK(mutator_source.find(
        "CurrentInteraction.PrepareForGameplayWorld();",
        interaction_lookup) < existing_interaction);
    CHECK(existing_interaction < create_interaction);
    CHECK(create_interaction < insert_interaction);
    CHECK(mutator_source.find(
        "FindObject(\"KF2OptimizerTelemetryInteraction\"") ==
          std::string::npos);
    CHECK(count_occurrences(mutator_source,
        "new(CurrentViewport, \"KF2OptimizerTelemetryInteraction\")") == 1);
    CHECK(count_occurrences(mutator_source,
        "InsertInteraction(CurrentInteraction)") == 1);
    CHECK(count_occurrences(mutator_source,
        "CurrentInteraction.PrepareForGameplayWorld();") == 2);

    // Each map creates a new mutator, but all three mutators share the same
    // process-lifetime viewport outer. The fully qualified object path must
    // therefore resolve to one interaction instead of one per gameplay world.
    std::set<std::string> viewport_interactions;
    std::size_t created_interactions = 0;
    std::size_t existing_interactions = 0;
    std::size_t session_ended_callbacks = 0;
    const std::string persistent_interaction_path =
        "KFGameEngine.KFGameViewportClient."
        "KF2OptimizerTelemetryInteraction";
    for (int world = 0; world < 3; ++world) {
        const auto [unused, inserted] =
            viewport_interactions.insert(persistent_interaction_path);
        static_cast<void>(unused);
        if (inserted) {
            ++created_interactions;
        } else {
            ++existing_interactions;
        }
        ++session_ended_callbacks;
    }
    CHECK(viewport_interactions.size() == 1);
    CHECK(created_interactions == 1);
    CHECK(existing_interactions == 2);
    CHECK(session_ended_callbacks == 3);
    CHECK(count_occurrences(interaction_source,
        "state=session_ended") == 1);
    CHECK(interaction_source.find("var transient WorldInfo") ==
          std::string::npos);
    CHECK(interaction_source.find("var transient PlayerController") ==
          std::string::npos);
    CHECK(interaction_source.find(
        "var transient KF2OptimizerTelemetryProbe") == std::string::npos);
    CHECK(interaction_source.find(
        "var transient KF2OptimizerAdaptiveControlListener") ==
          std::string::npos);
    CHECK(interaction_source.find("var transient Canvas") ==
          std::string::npos);
    CHECK(mutator_source.find("WorldInfo.NetMode != NM_Standalone") !=
          std::string::npos);
    CHECK(listener_source.find("BindPort(0, false)") != std::string::npos);
    CHECK(listener_source.find("state=ready port=") != std::string::npos);
    CHECK(connection_source.find("KF2OPT_ACK ") != std::string::npos);
    CHECK(connection_source.find("Probe.ApplyAdaptiveResourceControl") !=
          std::string::npos);
    CHECK(connection_source.find("ApplyAdaptiveTargetFPS") ==
          std::string::npos);
    CHECK(connection_source.find(
        "Left(Peer, 10) != \"127.0.0.1:\"") != std::string::npos);
    CHECK(connection_source.find("reason=non_loopback") !=
          std::string::npos);
    CHECK(connection_source.find("Len(Line) > 128") != std::string::npos);
    CHECK(graphics_source.find("Requested.Flex") == std::string::npos);
    CHECK(graphics_source.find(
        "static function ApplyOverdraw") != std::string::npos);
    CHECK(graphics_source.find(
        "static function ApplyEffects") != std::string::npos);
    CHECK(graphics_source.find(
        "ApplyOverdraw(Requested, Snapshot.OverdrawQuality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "ApplyEffects(Requested, Snapshot.EffectsQuality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "else if (Resource ~= \"overdraw\") Snapshot.OverdrawQuality = Quality") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "else if (Resource ~= \"effects\") Snapshot.EffectsQuality = Quality") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.RamQuality = Quality;\n"
        "        Snapshot.OverdrawQuality = Quality;") ==
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.RamQuality = Quality;\n"
        "        Snapshot.EffectsQuality = Quality;") ==
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.FX.DropParticleDistortion = true") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.FX.MaxPersistentSplatsPerFrame = Min") !=
          std::string::npos);
    CHECK(graphics_source.find("Requested.CharacterDetail.MaxDeadBodies") ==
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.CharacterDetail.ShouldCorpseCollideWithDeadAfterSleep = false") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.CharacterDetail.ShouldCorpseCollideWithDead = false") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.CharacterDetail.ShouldCorpseCollideWithLiving = false") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.bOriginalCorpseCollideWithDead") != std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.bOriginalCorpseCollideWithLiving") != std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.bOriginalCorpseCollideWithDeadAfterSleep") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.CharacterDetail.ShouldCorpseCollideWithDead =\n"
        "        Snapshot.bOriginalCorpseCollideWithDead") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Observed.CharacterDetail.ShouldCorpseCollideWithLiving") !=
          std::string::npos);
    CHECK(graphics_source.find("Requested.CharacterDetail.bAllowPhysics") ==
          std::string::npos);
    CHECK(graphics_source.find("KinematicUpdateDistFactorScale = FMax") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.TextureResolution.ShadowmapBias = Max") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.MotionBlur.MotionBlurQuality = Min") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.EnvironmentDetail.AllowLightFunctions = false") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.Shadows.ShadowFadeResolution = Max") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.OriginalShadowmapTextureBias") != std::string::npos);
    CHECK(graphics_source.find(
        "Observed.TextureResolution.ShadowmapBias") != std::string::npos);
    CHECK(graphics_source.find("RestoreOwnedSettings") != std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.GpuQuality = Max(Snapshot.GpuQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.CpuQuality = Max(Snapshot.CpuQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.VramQuality = Max(Snapshot.VramQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.RamQuality = Max(Snapshot.RamQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.OverdrawQuality = Max(\n"
        "            Snapshot.OverdrawQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.EffectsQuality = Max(\n"
        "            Snapshot.EffectsQuality, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find("Snapshot.EffectsQuality = 100") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Requested.FX.MaxGoreEffects, Max(2, Quality / 10)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool ApplyAdaptiveEffectRuntimeReadback(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "GoreManager.MaxBloodEffects =\n"
        "        class'KFGoreManager'.default.MaxBloodEffects") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "GoreManager.BloodFXEmitterPool.MaxActiveEffects =\n"
        "            GoreManager.MaxBloodEffects") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "ImpactEffectManager.ImpactEffectDecalManager.MaxActiveDecals =\n"
        "                ImpactEffectManager.MaxImpactEffectDecals") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.ImpactFXEmitterPool.MaxActiveEffects =\n"
        "            DesiredImpactEffects") != std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.ImpactFXEmitterPool.MaxActiveEffects ==\n"
        "            DesiredImpactEffects") != std::string::npos);
    CHECK(telemetry_source.find(
        " impact_pool=\"$") != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_EFFECT_RUNTIME state=applied") != std::string::npos);
    CHECK(telemetry_source.find(
        "!ApplyAdaptiveEffectRuntimeReadback(Resource, Quality)") !=
          std::string::npos);
    CHECK(graphics_source.find("Quality >= 90") != std::string::npos);
    CHECK(graphics_source.find("Quality <= 80") != std::string::npos);
    CHECK(graphics_source.find(
        "FMax(0.25, float(Quality) / 100.0)") != std::string::npos);
    CHECK(telemetry_source.find("AdaptivePresetIndex") == std::string::npos);
    CHECK(telemetry_source.find(" preset=") == std::string::npos);
    CHECK(telemetry_source.find(" stage=") != std::string::npos);
    CHECK(telemetry_source.find(
        "Resource ~= \"recover\" || Quality >= 95") ==
          std::string::npos);
    CHECK(graphics_source.find(
        "KF2OPT_ADAPTIVE_ROLLBACK state=applied") != std::string::npos);
    CHECK(graphics_source.find(
        "KF2OPT_ADAPTIVE_ROLLBACK state=failed") != std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.NetMode != NM_Standalone") != std::string::npos);
    CHECK(telemetry_source.find(
        "GoreManager.CorpsePool.Length <= AdaptiveCorpseRuntimeLimit") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig int AdaptiveCorpseMaximum") != std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig int AdaptiveTargetFPS") != std::string::npos);
    CHECK(telemetry_source.find("ApplyAdaptiveTargetFPS") ==
          std::string::npos);
    CHECK(interaction_source.find("t.MaxFPS") == std::string::npos);
    CHECK(interaction_source.find("MaxSmoothedFrameRate") ==
          std::string::npos);
    CHECK(interaction_source.find("KF2OPT_FRAME_RATE") ==
          std::string::npos);
    CHECK(interaction_source.find("KF2OPT_TARGET_FPS") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig int AdaptiveQualityChangeBudget") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int GetAdaptiveCorpseAttackScale()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool HasConfirmedAdaptivePerformancePressure()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (!HasConfirmedAdaptivePerformancePressure())") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "PhysicsPressureLevel = AdaptiveCorpsePressureLevel > 0") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveGraphicsQuality = 100") != std::string::npos);
    CHECK(telemetry_source.find(
        "Step = Step * AttackScale") != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceActionInterval / float(AttackScale)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "LodActionInterval / float(AttackScale)") != std::string::npos);
    CHECK(telemetry_source.find(
        "0.20 / float(PhysicsPressureLevel)") != std::string::npos);
    CHECK(telemetry_source.find(
        "ActionInterval / float(AttackScale)") != std::string::npos);
    CHECK(telemetry_source.find(
        "TargetFrameMs * 60.0 / 58.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveFrameBaselineMs * 1.3") == std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseTarget = AdaptiveCorpseMaximum") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseTarget = Clamp(AdaptiveCorpseOriginalLimit, 4, 2000)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseRuntimeLimit = AdaptiveCorpseTarget") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseRuntimeLimit = 72") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "function AdjustAdaptiveCorpseCapacity") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdjustAdaptiveCorpseCapacity(GoreManager, 0)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpsePressureLevel)") != std::string::npos);
    CHECK(telemetry_source.find(
        "SetTimer(0.45, true, nameof(StaggerCorpseCleanup), self)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "GoreManager.RemoveAndDeleteCorpse(SelectedIndex)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpsePressureLevel <= 0") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseCurrentFramePressureLevel <= 0") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.RealTimeSeconds - AdaptiveFramePressureObservedRealTime > 0.4") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.TimeOfDeath < 1.5") != std::string::npos);
    CHECK(telemetry_source.find(
        "GameInfo.IsZedTimeActive()") != std::string::npos);
    CHECK(telemetry_source.find(
        "SetTimer(0.25, true, nameof(AdaptiveCorpseLoadControl), self)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int SleepBaselineAwakeMonsterCorpses(") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "function AdaptiveCorpseAgingControl()") != std::string::npos);
    CHECK(telemetry_source.find(
        "SetTimer(0.05, true, nameof(AdaptiveCorpseAgingControl), self)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var int AdaptiveCorpseAgingCursor") != std::string::npos);
    CHECK(telemetry_source.find(
        "var int AdaptiveCorpseAgingReductions") != std::string::npos);
    CHECK(telemetry_source.find("MinimumTierOneAge = 3.0") !=
          std::string::npos);
    CHECK(telemetry_source.find("MinimumTierTwoAge = 8.0") !=
          std::string::npos);
    CHECK(telemetry_source.find("MinimumTierThreeAge = 15.0") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "RegisterAdaptiveCorpsePhysicsAction(Candidate, PhysicsAction)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_AGING state=applied") != std::string::npos);
    CHECK(telemetry_source.find(
        "ClearTimer(nameof(AdaptiveCorpseAgingControl), self)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Tier >= 3 ||") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Tier == 2 && DistanceUnits >= 1000") != std::string::npos);
    CHECK(telemetry_source.find(
        "Tier == 1 && DistanceUnits >= 1200") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "FindAdaptiveDistanceSleptCorpse(Candidate) == -1") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "!DeferAdaptiveDistanceResleepAfterNativeWake(Candidate)") !=
          std::string::npos);
    const auto aging_apply = telemetry_source.find(
        "function bool ApplyOneAdaptiveCorpseAging(");
    const auto aging_control = telemetry_source.find(
        "function AdaptiveCorpseAgingControl()", aging_apply);
    CHECK(aging_apply != std::string::npos);
    CHECK(aging_control != std::string::npos);
    const auto aging_body = telemetry_source.substr(
        aging_apply, aging_control - aging_apply);
    CHECK(aging_body.find("for (") == std::string::npos);
    const auto aging_sleep = aging_body.find(
        "Candidate.Mesh.PutRigidBodyToSleep()");
    const auto aging_sleep_gate_begin = aging_body.find(
        "MaximumSpeedSquared =");
    const auto aging_sleep_gate_end = aging_body.find(
        "FindAdaptiveDistanceSleptCorpse(Candidate)",
        aging_sleep_gate_begin);
    const auto aging_sleep_gate = aging_body.substr(
        aging_sleep_gate_begin,
        aging_sleep_gate_end - aging_sleep_gate_begin);
    const auto aging_readback = aging_body.find(
        "if (Candidate.Mesh.RigidBodyIsAwake())", aging_sleep);
    const auto aging_ownership = aging_body.find(
        "RegisterAdaptiveCorpsePhysicsAction(Candidate, PhysicsAction)",
        aging_readback);
    const auto aging_receipt = aging_body.find(
        "KF2OPT_CORPSE_AGING state=applied action=", aging_ownership);
    const auto aging_freeze = aging_body.find(
        "Candidate.SetPhysics(PHYS_None)", aging_readback);
    const auto aging_freeze_readback = aging_body.find(
        "if (Candidate.Physics != PHYS_None)", aging_freeze);
    const auto aging_registration_rollback = aging_body.find(
        "Candidate.SetPhysics(PHYS_RigidBody)", aging_ownership);
    const auto aging_registration_rollback_readback = aging_body.find(
        "Candidate.Physics != PHYS_RigidBody",
        aging_registration_rollback);
    const auto aging_registration_ownership_release = aging_body.find(
        "AdaptiveCorpseLodPhysicsFrozen[EntryIndex] = false",
        aging_registration_rollback_readback);
    const auto aging_final_safety_definition = aging_body.find(
        "bFinalTierInteractionSafe = DistanceUnits >= 800 &&");
    const auto aging_final_safety_visibility = aging_body.find(
        "!bRecentlyRendered", aging_final_safety_definition);
    const auto aging_sleep_final_safety = aging_body.find(
        "(Tier < 3 ||",
        aging_final_safety_visibility);
    const auto aging_final_lod_ready_definition = aging_body.find(
        "bFinalTierLodReady =");
    const auto aging_final_lod_ready_gate = aging_body.find(
        "bFinalTierInteractionSafe && bFinalTierLodReady",
        aging_final_lod_ready_definition);
    const auto aging_lod_distance_guard = aging_body.find(
        "Tier < 3 && DistanceUnits < 300");
    const auto aging_lod_final_safety = aging_body.find(
        "Tier >= 3 && !bFinalTierInteractionSafe",
        aging_sleep_final_safety);
    const auto aging_lod_write = aging_body.find(
        "Candidate.Mesh.MinLodModel = TargetMinLod",
        aging_lod_distance_guard);
    const auto aging_lod_path = aging_body.substr(
        aging_lod_distance_guard, aging_lod_write - aging_lod_distance_guard);
    CHECK(aging_sleep != std::string::npos);
    CHECK(aging_sleep_gate_begin != std::string::npos);
    CHECK(aging_sleep_gate_end != std::string::npos);
    CHECK(aging_sleep_gate.find("!bRecentlyRendered") == std::string::npos);
    CHECK(aging_readback != std::string::npos);
    CHECK(aging_ownership != std::string::npos);
    CHECK(aging_receipt != std::string::npos);
    CHECK(aging_freeze != std::string::npos);
    CHECK(aging_freeze_readback != std::string::npos);
    CHECK(aging_registration_rollback != std::string::npos);
    CHECK(aging_registration_rollback_readback != std::string::npos);
    CHECK(aging_registration_ownership_release != std::string::npos);
    CHECK(aging_final_safety_definition != std::string::npos);
    CHECK(aging_final_safety_visibility != std::string::npos);
    CHECK(aging_sleep_final_safety != std::string::npos);
    CHECK(aging_final_lod_ready_definition != std::string::npos);
    CHECK(aging_final_lod_ready_gate != std::string::npos);
    CHECK(aging_lod_final_safety != std::string::npos);
    CHECK(aging_final_safety_definition < aging_sleep);
    CHECK(aging_final_safety_visibility < aging_sleep);
    CHECK(aging_sleep_final_safety < aging_sleep);
    CHECK(aging_final_lod_ready_definition < aging_sleep);
    CHECK(aging_final_lod_ready_gate < aging_sleep);
    CHECK(aging_sleep < aging_readback);
    CHECK(aging_readback < aging_freeze);
    CHECK(aging_freeze < aging_freeze_readback);
    CHECK(aging_freeze_readback < aging_ownership);
    CHECK(aging_ownership < aging_registration_rollback);
    CHECK(aging_registration_rollback < aging_registration_rollback_readback);
    CHECK(aging_registration_rollback_readback <
          aging_registration_ownership_release);
    CHECK(aging_ownership < aging_receipt);
    CHECK(aging_lod_distance_guard != std::string::npos);
    CHECK(aging_lod_final_safety < aging_lod_write);
    CHECK(aging_lod_write != std::string::npos);
    CHECK(aging_lod_distance_guard < aging_lod_write);
    CHECK(aging_lod_path.find("!bRecentlyRendered") == std::string::npos);
    CHECK(aging_body.find(
        "recently_rendered=\"$(bRecentlyRendered ? 1 : 0)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseLodPersistentAging.AddItem(true)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "if (!AdaptiveCorpseLodPersistentAging[Index] &&") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseLodPhysicsFrozen[EntryIndex] = true") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.SetPhysics(PHYS_RigidBody)") != std::string::npos);
    const auto aging_restore = telemetry_source.find(
        "function bool RemoveAdaptiveCorpseLodEntry(");
    const auto aging_restore_end = telemetry_source.find(
        "function PruneAdaptiveCorpseLodEntries()", aging_restore);
    CHECK(aging_restore != std::string::npos);
    CHECK(aging_restore_end != std::string::npos);
    const auto aging_restore_body = telemetry_source.substr(
        aging_restore, aging_restore_end - aging_restore);
    const auto aging_restore_physics = aging_restore_body.find(
        "Candidate.SetPhysics(PHYS_RigidBody)");
    const auto aging_restore_readback = aging_restore_body.find(
        "Candidate.Physics != PHYS_RigidBody", aging_restore_physics);
    const auto aging_restore_unregister = aging_restore_body.find(
        "UnregisterAdaptiveCorpsePhysicsAction(", aging_restore_readback);
    const auto aging_restore_lod_write = aging_restore_body.find(
        "Candidate.Mesh.MinLodModel =", aging_restore_unregister);
    const auto aging_restore_lod_readback = aging_restore_body.find(
        "Candidate.Mesh.MinLodModel !=",
        aging_restore_lod_write);
    const auto aging_restore_remove = aging_restore_body.find(
        "AdaptiveCorpseLodCorpses.Remove", aging_restore_lod_readback);
    CHECK(aging_restore_physics != std::string::npos);
    CHECK(aging_restore_readback != std::string::npos);
    CHECK(aging_restore_unregister != std::string::npos);
    CHECK(aging_restore_lod_write != std::string::npos);
    CHECK(aging_restore_lod_readback != std::string::npos);
    CHECK(aging_restore_remove != std::string::npos);
    CHECK(aging_restore_physics < aging_restore_readback);
    CHECK(aging_restore_readback < aging_restore_unregister);
    CHECK(aging_restore_unregister < aging_restore_lod_write);
    CHECK(aging_restore_lod_write < aging_restore_lod_readback);
    CHECK(aging_restore_lod_readback < aging_restore_remove);
    CHECK(aging_restore_unregister < aging_restore_remove);
    CHECK(telemetry_source.find(
        "function bool UnregisterAdaptiveCorpsePhysicsAction(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpsePhysicsActionIds[Slot] = \"__deleted__\"") !=
          std::string::npos);
    const auto aging_prune = telemetry_source.find(
        "function PruneAdaptiveCorpseLodEntries()");
    const auto aging_prune_end = telemetry_source.find(
        "function RestoreNearAdaptiveCorpseLods()", aging_prune);
    CHECK(aging_prune != std::string::npos);
    CHECK(aging_prune_end != std::string::npos);
    const auto aging_prune_body = telemetry_source.substr(
        aging_prune, aging_prune_end - aging_prune);
    const auto aging_prune_frozen = aging_prune_body.find(
        "AdaptiveCorpseLodPhysicsFrozen[Index]");
    const auto aging_prune_safe_release = aging_prune_body.find(
        "RemoveAdaptiveCorpseLodEntry(Index, true)", aging_prune_frozen);
    const auto aging_prune_external_lod_release = aging_prune_body.find(
        "Index, AdaptiveCorpseLodPhysicsFrozen[Index]",
        aging_prune_safe_release);
    CHECK(aging_prune_frozen != std::string::npos);
    CHECK(aging_prune_safe_release != std::string::npos);
    CHECK(aging_prune_external_lod_release != std::string::npos);
    CHECK(telemetry_source.find(
        "SetTimer(0.05, true, nameof(RestoreOneAdaptiveCorpseLod), self)") !=
          std::string::npos);
    const auto runtime_toggle = telemetry_source.find(
        "function bool SetAdaptiveRuntimeEnabled(bool bEnabled)");
    const auto runtime_disable_release = telemetry_source.find(
        "BeginAdaptiveCorpseLodRelease()", runtime_toggle);
    const auto runtime_toggle_end = telemetry_source.find(
        "function bool ApplyAdaptiveResourceControl(", runtime_toggle);
    CHECK(runtime_toggle != std::string::npos);
    CHECK(runtime_disable_release != std::string::npos);
    CHECK(runtime_toggle_end != std::string::npos);
    const auto runtime_after_release = telemetry_source.substr(
        runtime_disable_release, runtime_toggle_end - runtime_disable_release);
    CHECK(runtime_after_release.find(
        "ClearTimer(nameof(RestoreOneAdaptiveCorpseLod), self)") ==
          std::string::npos);
    const auto aging_control_body = telemetry_source.substr(
        aging_control,
        telemetry_source.find("function ", aging_control + 10) -
            aging_control);
    const auto aging_zed_time = aging_control_body.find(
        "GameInfo.IsZedTimeActive()");
    const auto aging_apply_call = aging_control_body.find(
        "ApplyOneAdaptiveCorpseAging(GoreManager)");
    CHECK(aging_zed_time != std::string::npos);
    CHECK(aging_apply_call != std::string::npos);
    CHECK(aging_zed_time < aging_apply_call);
    CHECK(telemetry_source.find(
        "Candidate = GoreManager.CorpsePool[AdaptiveCorpseAgingCursor]") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "0.05 + ((WeightedVisibleZeds - 1.0) / 79.0) * 0.95") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.bDropDetail") != std::string::npos);
    CHECK(telemetry_source.find(
        "WorldInfo.DeltaSeconds * 1000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "KFPawn_Monster(Candidate) == None") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.PutRigidBodyToSleep()") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.bNoSkeletonUpdate = true") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.SpecialMove == SM_DeathAnim") != std::string::npos);
    CHECK(telemetry_source.find(
        "VSizeSq(Candidate.Velocity) > MaximumSpeedSquared") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.SkeletalMesh.LODInfo.Length < 2") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.ForcedLodModel != 0") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.MinLodModel = TargetMinLod") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "BeginAdaptiveCorpseLodRelease()") != std::string::npos);
    const auto lod_restore_function = telemetry_source.find(
        "function RestoreNearAdaptiveCorpseLods()");
    const auto lod_restore_threshold = telemetry_source.find(
        "DistanceSquared < 62500.0", lod_restore_function);
    const auto lod_selector_function = telemetry_source.find(
        "function KFPawn SelectVisibleMonsterCorpseForLod(");
    const auto lod_apply_threshold = telemetry_source.find(
        "DistanceSquared < 90000.0", lod_selector_function);
    CHECK(lod_restore_threshold != std::string::npos);
    CHECK(lod_apply_threshold != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_LOD state=applied") != std::string::npos);
    CHECK(telemetry_source.find(
        "SelectDistantAwakeMonsterCorpseForSleep") != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumDistanceSquared = 1440000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumDistanceSquared = 1000000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumDistanceSquared = 722500.0") != std::string::npos);
    CHECK(telemetry_source.find("MinimumDistanceSquared = 7840000.0") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDistanceSleptCorpses.Length >=") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.LastRenderTime >") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.WakeRigidBody()") != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared >= 640000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "AwakeTotal = CountAwakeMonsterCorpses(GoreManager)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_DISTANCE state=sleep") != std::string::npos);
    CHECK(telemetry_source.find(
        "FindAdaptiveDistanceSleptCorpse(Candidate) != -1") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "struct AdaptiveDistanceSleepEntry") != std::string::npos);
    CHECK(telemetry_source.find(
        "struct AdaptiveDistanceSleepTransitionEntry") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var int NativeWakeDistanceUnits") != std::string::npos);
    CHECK(telemetry_source.find(
        "var float NativeWakeObservedRealTime") != std::string::npos);
    CHECK(telemetry_source.find(
        "function bool DeferAdaptiveDistanceResleepAfterNativeWake(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "DeferAdaptiveDistanceResleepAfterNativeWake(Candidate)") !=
          std::string::npos);
    CHECK(telemetry_source.find("NativeWakeAge < 1.0") !=
          std::string::npos);
    CHECK(telemetry_source.find("NativeWakeAge >= 5.0") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "CurrentDistanceUnits < NativeWakeDistanceUnits + 250") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function bool RememberAdaptiveDistanceSleepTransition(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveDistanceSleepTransitions.Length = 8192") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Slot = (Slot + 1) & 8191") != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_DISTANCE state=removed") != std::string::npos);
    CHECK(count_occurrences(
        telemetry_source, "KF2OPT_CORPSE_DISTANCE state=removed") == 2);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseManager.CorpsePool.Find(Candidate) >= 0") !=
          std::string::npos);
    CHECK(telemetry_source.find("Index, \"native_wake\"") !=
          std::string::npos);
    CHECK(telemetry_source.find("Index, \"physics_changed\"") !=
          std::string::npos);
    CHECK(telemetry_source.find("Index, \"deleted\"") !=
          std::string::npos);
    CHECK(telemetry_source.find("Index, \"reused\"") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_DISTANCE state=resleep") != std::string::npos);
    CHECK(telemetry_source.find(" previous_reason=") != std::string::npos);
    CHECK(telemetry_source.find("removal_reason=tracking_lost") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "state=tracking_full capacity=8192") != std::string::npos);
    CHECK(telemetry_source.find(
        "action=disabled_to_preserve_traceability") != std::string::npos);
    const auto distance_prune = telemetry_source.find(
        "function PruneAdaptiveDistanceSleptCorpses()");
    const auto native_wake_transition = telemetry_source.find(
        "Candidate.Mesh.RigidBodyIsAwake()", distance_prune);
    const auto native_wake_release = telemetry_source.find(
        "Index, \"native_wake\"", native_wake_transition);
    CHECK(distance_prune != std::string::npos);
    CHECK(native_wake_transition != std::string::npos);
    CHECK(native_wake_release != std::string::npos);
    CHECK(native_wake_transition < native_wake_release);
    CHECK(telemetry_source.find(
        "function string GetAdaptiveCorpseActionId(KFPawn Candidate)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "string(Candidate.Name)$\":\"$") != std::string::npos);
    CHECK(telemetry_source.find(
        "int(Candidate.TimeOfDeath * 1000.0)") != std::string::npos);
    CHECK(telemetry_source.find(
        "function int GetAdaptiveCorpseDistanceUnits(KFPawn Candidate)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function string FormatAdaptiveCorpseDistanceMeters(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceDecimeters = (DistanceUnits + 5) / 10") !=
          std::string::npos);
    CHECK(count_occurrences(telemetry_source, "corpse_id=") == 12);
    CHECK(count_occurrences(telemetry_source, " distance_units=") == 12);
    CHECK(count_occurrences(telemetry_source, " distance_m=") == 12);
    const auto distance_marker = telemetry_source.find(
        "AdaptiveCorpseDebugMarkers[Index].Action$\" | \"");
    CHECK(distance_marker != std::string::npos);
    CHECK(telemetry_source.find(
        "GetAdaptiveCorpseDistanceUnits(Candidate), true)", distance_marker) !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig bool bAdaptiveCorpseDebugMarkers") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig bool bAdaptiveZedDebugMarkers") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function RegisterAdaptiveCorpseDebugMarker(") != std::string::npos);
    CHECK(telemetry_source.find(
        "function DrawAdaptiveCorpseDebugMarkers(Canvas MarkerCanvas)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function DrawAdaptiveZedDebugMarkers(Canvas MarkerCanvas)") !=
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveZedDebugMarkers.Length >= 64") !=
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveZedDebugRefreshRealTime + 0.10") !=
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveCorpseDebugMarkers.Length >= 24") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "GetAdaptiveCorpseActionId(Candidate)") != std::string::npos);
    CHECK(count_occurrences(
        telemetry_source, "RegisterAdaptiveCorpseDebugMarker(Candidate,") == 6);
    CHECK(interaction_source.find("event PostRender(Canvas MarkerCanvas)") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "var transient KF2OptimizerTelemetryProbe ActiveProbe") ==
          std::string::npos);
    CHECK(interaction_source.find("var transient WorldInfo ActiveWorld") ==
          std::string::npos);
    CHECK(interaction_source.find(
        "foreach CurrentWorld.DynamicActors(") != std::string::npos);
    CHECK(interaction_source.find(
        "CurrentProbe.DrawAdaptiveCorpseDebugMarkers(MarkerCanvas)") !=
          std::string::npos);
    CHECK(interaction_source.find(
        "CurrentProbe.DrawAdaptiveZedDebugMarkers(MarkerCanvas)") !=
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveDistancePhysicsSleeps % 4") ==
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveVisibleRagdollSleeps % 4") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int GetAdaptiveCorpseScenePressureLevel(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int GetAdaptiveLivingEnemyPressureLevel(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int ResolveAdaptiveLivingEnemyPressureLevel(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveLivingVisualPressureLevel") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveLivingVisualPendingPressureLevel") != std::string::npos);
    CHECK(telemetry_source.find(
        "float(AdaptiveLivingVisualPressureLevel) / 5.0 + 0.03") !=
          std::string::npos);
    CHECK(telemetry_source.find("HoldSeconds = 0.75") != std::string::npos);
    CHECK(telemetry_source.find("HoldSeconds = 1.25") != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveLivingVisualLastChangeRealTime < 1.5") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function float GetAdaptiveLivingEnemyPressureScale(") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_TELEMETRY_PROFILE schema=2") != std::string::npos);
    CHECK(telemetry_source.find("SampleSequence % 10 == 0") !=
          std::string::npos);
    CHECK(telemetry_source.find("GetSystemTime(") !=
          std::string::npos);
    CHECK(telemetry_source.find("GetProfileElapsedMilliseconds(") !=
          std::string::npos);
    CHECK(telemetry_source.find("timer=system_clock_ms resolution_us=1000") !=
          std::string::npos);
    CHECK(telemetry_source.find("clock_anomalies=") != std::string::npos);
    CHECK(telemetry_source.find("ElapsedMilliseconds <= 10000") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "StartMilliseconds >= 86390000 && EndMilliseconds <= 10000") !=
          std::string::npos);
    CHECK(telemetry_source.find("state=\"$ProfileState") !=
          std::string::npos);
    CHECK(telemetry_source.find("native_nodes=submitted") !=
          std::string::npos);
    CHECK(telemetry_source.find("ProfNodeStart(\"KF2OPT_Telemetry_Total\")") !=
          std::string::npos);
    CHECK(telemetry_source.find("ProfNodeStop(ProfileTotalNode)") !=
          std::string::npos);
    CHECK(telemetry_source.find("particle_pools_ms=") != std::string::npos);
    CHECK(telemetry_source.find("world_emitters_ms=") != std::string::npos);
    CHECK(telemetry_source.find("unclassified_ms=") != std::string::npos);
    CHECK(telemetry_source.find("Clock(ProfileTotalSeconds)") ==
          std::string::npos);
    CHECK(telemetry_source.find("total_us=") == std::string::npos);
    CHECK(telemetry_source.find(
        "WeightedVisibleZeds = float(VisibleLivingZeds)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared < 360000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared < 1440000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.3") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "(WeightedVisibleZeds - 4.0) / 76.0") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveVisibleLivingZeds = LivingRecentlyRendered") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "PhysicsPressureLevel <= 0 && bRecentlyRendered") !=
          std::string::npos);
    const auto stagger_start = telemetry_source.find(
        "function AdaptiveCorpseLoadControl()");
    const auto zed_time_guard = telemetry_source.find(
        "GameInfo.IsZedTimeActive()", stagger_start);
    const auto wake_stage = telemetry_source.find(
        "WakeNearAdaptiveDistanceSleptCorpses()", stagger_start);
    const auto baseline_sleep_stage = telemetry_source.find(
        "SleepBaselineAwakeMonsterCorpses(GoreManager)", stagger_start);
    const auto distant_sleep_stage = telemetry_source.find(
        "SleepOneDistantMonsterCorpse(", stagger_start);
    CHECK(stagger_start != std::string::npos);
    CHECK(zed_time_guard != std::string::npos);
    CHECK(baseline_sleep_stage == std::string::npos);
    CHECK(wake_stage != std::string::npos);
    CHECK(distant_sleep_stage != std::string::npos);
    CHECK(zed_time_guard < wake_stage);
    CHECK(zed_time_guard < distant_sleep_stage);
    const auto frame_only_action_gate = telemetry_source.find(
        "if (AdaptiveCorpsePressureLevel <= 0)", stagger_start);
    CHECK(frame_only_action_gate == std::string::npos);
    CHECK(telemetry_source.find(
        "PhysicsPressureLevel = Max(", stagger_start) != std::string::npos);
    CHECK(telemetry_source.find(
        "RagdollPressureLevel = Max(", stagger_start) != std::string::npos);

    const auto cleanup_start = telemetry_source.find(
        "function StaggerCorpseCleanup()");
    const auto cleanup_zed_time_guard = telemetry_source.find(
        "GameInfo.IsZedTimeActive()", cleanup_start);
    const auto cleanup_delete = telemetry_source.find(
        "GoreManager.RemoveAndDeleteCorpse(SelectedIndex)", cleanup_start);
    CHECK(cleanup_start != std::string::npos);
    CHECK(cleanup_zed_time_guard != std::string::npos);
    CHECK(cleanup_delete != std::string::npos);
    CHECK(cleanup_zed_time_guard < cleanup_delete);

    const auto wake_function = telemetry_source.find(
        "function int WakeNearAdaptiveDistanceSleptCorpses()");
    const auto wake_threshold = telemetry_source.find(
        "DistanceSquared >= 640000.0", wake_function);
    const auto wake_call = telemetry_source.find(
        "Candidate.Mesh.WakeRigidBody()", wake_function);
    const auto wake_readback = telemetry_source.find(
        "if (!Candidate.Mesh.RigidBodyIsAwake())", wake_call);
    const auto wake_tracking_release = telemetry_source.find(
        "RemoveAdaptiveDistanceSleptCorpseEntry(Index, \"optimizer_wake\")",
        wake_function);
    const auto wake_receipt = telemetry_source.find(
        "KF2OPT_CORPSE_DISTANCE state=wake", wake_function);
    CHECK(wake_function != std::string::npos);
    CHECK(wake_threshold != std::string::npos);
    CHECK(wake_call != std::string::npos);
    CHECK(wake_readback != std::string::npos);
    CHECK(wake_tracking_release != std::string::npos);
    CHECK(wake_receipt != std::string::npos);
    CHECK(wake_threshold < wake_call);
    CHECK(wake_call < wake_readback);
    CHECK(wake_readback < wake_tracking_release);
    CHECK(wake_tracking_release < wake_receipt);
    CHECK(telemetry_source.find(
        "function int WakeNearAdaptiveDistanceSleptCorpses()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "WakeCount < Max(3, AttackScale)", stagger_start) ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "WakeNearAdaptiveDistanceSleptCorpses();", stagger_start) !=
          std::string::npos);

    const auto lod_selector = telemetry_source.find(
        "function KFPawn SelectVisibleMonsterCorpseForLod(");
    const auto lod_apply = telemetry_source.find(
        "function bool ApplyOneAdaptiveCorpseLod(", lod_selector);
    CHECK(lod_selector != std::string::npos);
    CHECK(lod_apply != std::string::npos);
    CHECK(telemetry_source.substr(
        lod_selector, lod_apply - lod_selector).find(
            "Candidate.Mesh.RigidBodyIsAwake()") == std::string::npos);
    CHECK(telemetry_source.substr(
        lod_selector, lod_apply - lod_selector).find(
            "FindAdaptiveDistanceSleptCorpse(Candidate)") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "MaximumMinLod = Candidate.Mesh.SkeletalMesh.LODInfo.Length - 1",
        lod_selector) != std::string::npos);
    CHECK(telemetry_source.find(
        "AdaptiveCorpseLodCorpses.Length >=", lod_apply) ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "CandidateTarget = 2", lod_selector) != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared >= 1440000.0", lod_selector) != std::string::npos);
    CHECK(telemetry_source.find(
        "CandidateTarget = 5", lod_selector) != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared >= 640000.0", lod_selector) != std::string::npos);
    CHECK(telemetry_source.find(
        "CandidateTarget = 4", lod_selector) != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared >= 250000.0", lod_selector) != std::string::npos);
    CHECK(telemetry_source.find(
        "CandidateTarget = 3", lod_selector) != std::string::npos);
    CHECK(telemetry_source.find(
        "CandidateTarget += Clamp(PressureLevel, 0, 5)", lod_selector) !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared >= 12960000.0", lod_selector) ==
          std::string::npos);
    CHECK(count_occurrences(telemetry_source,
        "Max(AdaptiveCorpsePressureLevel, ScenePressureLevel)") >= 2);
    CHECK(count_occurrences(telemetry_source, "EnemyPressureLevel);") >= 2);
    CHECK(telemetry_source.find(
        "function ApplyLivingEnemyVisualPressure(") != std::string::npos);
    CHECK(telemetry_source.find(
        "EnemyPressureLevel = ResolveAdaptiveLivingEnemyPressureLevel(",
        stagger_start) != std::string::npos);
    CHECK(telemetry_source.find(
        "ApplyLivingEnemyVisualPressure(EnemyPressureLevel, EnemyPressureScale);",
        stagger_start) != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.AnimationLODDistanceFactor =") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.AnimationLODFrameRate =") != std::string::npos);
    CHECK(telemetry_source.find(
        "TierScale = float(EnemyPressureLevel) / 5.0") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "TargetMinLod = 1 + int(TierScale * float(MaximumMinLod))") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "TargetAnimRate = Clamp(1 + EnemyPressureLevel, 2, 6)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "FMin(0.55, 0.15 + TierScale * 0.40)") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.MinLodModel == TargetMinLod") != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared < 90000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.bSkipTickAnimNodes =") == std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.bSkipGetBoneAtoms =") == std::string::npos);
    CHECK(telemetry_source.find(
        "RestoreAllAdaptiveLivingVisuals();") != std::string::npos);
    CHECK(telemetry_source.find(
        "function PruneAdaptiveLivingVisualEntries()") != std::string::npos);
    CHECK(telemetry_source.find(
        "PruneAdaptiveLivingVisualEntries();") != std::string::npos);
    CHECK(telemetry_source.find(
        "readback=verified", lod_apply) != std::string::npos);

    const auto destroyed = telemetry_source.find("event Destroyed()");
    const auto destroyed_end = telemetry_source.find(
        "Super.Destroyed();", destroyed);
    CHECK(destroyed != std::string::npos);
    CHECK(destroyed_end != std::string::npos);
    const auto quiesce = telemetry_source.find(
        "function QuiesceForWorldTeardown()");
    CHECK(quiesce != std::string::npos);
    CHECK(telemetry_source.find("if (bAdaptiveRuntimeQuiesced)", quiesce) !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "state=stopped reason=world_teardown", quiesce) != std::string::npos);
    CHECK(telemetry_source.find("QuiesceForWorldTeardown();", destroyed) <
          destroyed_end);
    const auto quiesce_end = telemetry_source.find(
        "event Destroyed()", quiesce);
    CHECK(quiesce_end != std::string::npos);
    const auto quiesce_body = telemetry_source.substr(
        quiesce, quiesce_end - quiesce);
    const auto destroyed_body = telemetry_source.substr(
        destroyed, destroyed_end - destroyed);
    CHECK(destroyed_body.find("RestoreAdaptiveGraphics()") ==
          std::string::npos);
    CHECK(destroyed_body.find("RestoreAllAdaptiveCorpseLods()") ==
          std::string::npos);
    CHECK(destroyed_body.find("RestoreAllAdaptiveLivingVisuals()") ==
          std::string::npos);
    CHECK(quiesce_body.find("AdaptiveLivingVisualZeds.Length = 0") !=
          std::string::npos);
    CHECK(quiesce_body.find("AdaptiveCorpseLodCorpses.Length = 0") !=
          std::string::npos);

    const auto ragdoll_selector = telemetry_source.find(
        "function KFPawn SelectVisibleAwakeMonsterCorpseForSleep(");
    const auto ragdoll_function = telemetry_source.find(
        "function bool SleepOneVisibleMonsterCorpse(");
    const auto ragdoll_ownership_array = telemetry_source.find(
        "var array<string> AdaptiveCorpsePhysicsActionIds");
    const auto ragdoll_ownership_count = telemetry_source.find(
        "var int AdaptiveCorpsePhysicsActionIdCount");
    const auto ragdoll_ownership_capacity = telemetry_source.find(
        "AdaptiveCorpsePhysicsActionIds.Length = 8192");
    const auto ragdoll_ownership_hash = telemetry_source.find(
        "function int GetAdaptiveCorpsePhysicsActionHash(string ActionId)");
    const auto ragdoll_ownership_lookup = telemetry_source.find(
        "function int FindAdaptiveCorpsePhysicsActionId(");
    const auto ragdoll_ownership_filter = telemetry_source.find(
        "FindAdaptiveCorpsePhysicsActionId(\"ragdoll\",", ragdoll_selector);
    const auto ragdoll_ownership_id = telemetry_source.find(
        "GetAdaptiveCorpseActionId(Candidate)) != -1",
        ragdoll_ownership_filter);
    const auto ragdoll_ownership_register = telemetry_source.find(
        "RegisterAdaptiveCorpsePhysicsAction(Candidate, \"ragdoll\")",
        ragdoll_selector);
    const auto ragdoll_awake_filter = telemetry_source.find(
        "!Candidate.Mesh.RigidBodyIsAwake()", ragdoll_selector);
    const auto ragdoll_local_player = telemetry_source.find(
        "LocalPC = GetALocalPlayerController();", ragdoll_selector);
    const auto ragdoll_player_required = telemetry_source.find(
        "if (LocalPC == None || LocalPC.Pawn == None)",
        ragdoll_local_player);
    const auto ragdoll_distance_measurement = telemetry_source.find(
        "DistanceSquared = VSizeSq(", ragdoll_player_required);
    const auto ragdoll_near_safety_gate = telemetry_source.find(
        "DistanceSquared < 640000.0", ragdoll_distance_measurement);
    const auto ragdoll_sleep_call = telemetry_source.find(
        "Candidate.Mesh.PutRigidBodyToSleep()", ragdoll_function);
    const auto ragdoll_sleep_readback = telemetry_source.find(
        "if (Candidate.Mesh.RigidBodyIsAwake())", ragdoll_sleep_call);
    const auto ragdoll_sleep_register = telemetry_source.find(
        "RegisterAdaptiveCorpsePhysicsAction(Candidate, \"ragdoll\")",
        ragdoll_sleep_readback);
    const auto ragdoll_counter = telemetry_source.find(
        "++AdaptiveVisibleRagdollSleeps", ragdoll_function);
    const auto ragdoll_receipt = telemetry_source.find(
        "KF2OPT_CORPSE_RAGDOLL state=sleep", ragdoll_function);
    const auto ragdoll_near_rejection = telemetry_source.find(
        "KF2OPT_CORPSE_RAGDOLL state=rejected_near", ragdoll_selector);
    const auto ragdoll_ownership_receipt = telemetry_source.find(
        "ownership_tracked=", ragdoll_receipt);
    CHECK(ragdoll_selector != std::string::npos);
    CHECK(ragdoll_function != std::string::npos);
    CHECK(ragdoll_ownership_array != std::string::npos);
    CHECK(ragdoll_ownership_count != std::string::npos);
    CHECK(ragdoll_ownership_capacity != std::string::npos);
    CHECK(ragdoll_ownership_hash != std::string::npos);
    CHECK(ragdoll_ownership_lookup != std::string::npos);
    CHECK(ragdoll_ownership_filter != std::string::npos);
    CHECK(ragdoll_ownership_id != std::string::npos);
    CHECK(ragdoll_ownership_register != std::string::npos);
    CHECK(telemetry_source.find("EligibleAfterRealTime") ==
          std::string::npos);
    CHECK(telemetry_source.find("WorldInfo.RealTimeSeconds + 3.0") ==
          std::string::npos);
    CHECK(telemetry_source.find("struct AdaptiveCorpseRagdollSleepEntry") ==
          std::string::npos);
    CHECK(ragdoll_awake_filter != std::string::npos);
    CHECK(ragdoll_local_player != std::string::npos);
    CHECK(ragdoll_player_required != std::string::npos);
    CHECK(ragdoll_distance_measurement != std::string::npos);
    CHECK(ragdoll_near_safety_gate != std::string::npos);
    CHECK(ragdoll_sleep_call != std::string::npos);
    CHECK(ragdoll_sleep_readback != std::string::npos);
    CHECK(ragdoll_sleep_register != std::string::npos);
    CHECK(ragdoll_counter != std::string::npos);
    CHECK(ragdoll_receipt != std::string::npos);
    CHECK(ragdoll_near_rejection != std::string::npos);
    CHECK(ragdoll_ownership_receipt != std::string::npos);
    CHECK(ragdoll_ownership_filter < ragdoll_awake_filter);
    CHECK(ragdoll_local_player < ragdoll_function);
    CHECK(ragdoll_player_required < ragdoll_function);
    CHECK(ragdoll_distance_measurement < ragdoll_function);
    CHECK(ragdoll_near_safety_gate < ragdoll_function);
    CHECK(ragdoll_sleep_call < ragdoll_sleep_readback);
    CHECK(ragdoll_sleep_readback < ragdoll_sleep_register);
    CHECK(ragdoll_sleep_register < ragdoll_counter);
    CHECK(ragdoll_counter < ragdoll_receipt);
    CHECK(telemetry_source.find("minimum_distance_units=800",
                                ragdoll_near_rejection) != std::string::npos);
    CHECK(telemetry_source.find("minimum_distance_m=8.0",
                                ragdoll_near_rejection) != std::string::npos);
    CHECK(telemetry_source.find("eligible=0 reason=near_player",
                                ragdoll_near_rejection) != std::string::npos);
    CHECK(telemetry_source.find("scene_level=", ragdoll_receipt) !=
          std::string::npos);
    CHECK(telemetry_source.find("enemy_level=", ragdoll_receipt) !=
          std::string::npos);
    CHECK(telemetry_source.find("frame_level=", ragdoll_receipt) !=
          std::string::npos);
    CHECK(telemetry_source.find("minimum_distance_units=800",
                                ragdoll_receipt) != std::string::npos);
    CHECK(telemetry_source.find("minimum_distance_m=8.0",
                                ragdoll_receipt) != std::string::npos);
    CHECK(telemetry_source.find("zed_time=0 eligible=1",
                                ragdoll_receipt) != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumAge = SeverePressure ? 0.75 : 1.5", ragdoll_selector) !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "VisibleAwake >= 3", stagger_start) != std::string::npos);
    CHECK(telemetry_source.find(
        "VisibleAwake >= 6", stagger_start) != std::string::npos);
    CHECK(telemetry_source.find(
        "DesiredVisibleAwake = 2", stagger_start) != std::string::npos);
    CHECK(telemetry_source.find("Zed.SetHidden(") == std::string::npos);
    CHECK(telemetry_source.find("Zed.bHidden") == std::string::npos);

    const fs::path root{KF2_TEST_ROOT};
    std::error_code error;
    fs::remove_all(root, error);
    fs::create_directories(root);
    const auto game_ini = root / L"KFGame.ini";
    const auto engine_ini = root / L"KFEngine.ini";
    constexpr std::string_view original =
        "\xEF\xBB\xBF[Other]\r\nKeep=1\r\n\r\n"
        "[KFGameContent.KFGameInfo_Survival]\r\n"
        "bLogAICount=False ; temporary lab switch\r\n"
        "MaxPlayers=6\r\n";
    write_bytes(game_ini, original);
    constexpr std::string_view original_engine =
        "[URL]\r\n"
        "LocalOptions=\r\n"
        "\r\n[Core.System]\r\n"
        "ScriptPaths=..\\..\\KFGame\\Script\r\n"
        "\r\n[Engine.Engine]\r\n"
        "GameViewportClientClassName=KFGame.KFGameViewportClient\r\n"
        "\r\n[Engine.GameEngine]\r\n"
        "ServerActors=IpDrv.WebServer\r\n"
        "bUseTextureStreaming=True\r\n";
    write_bytes(engine_ini, original_engine);

    const auto enabled = kf2::game::enable_offline_gameplay_logging(root);
    CHECK(enabled.has_value());
    CHECK(enabled.value());
    const auto changed = read_bytes(game_ini);
    CHECK(changed.starts_with("\xEF\xBB\xBF"));
    CHECK(changed.find("bLogAICount=True ; temporary lab switch\r\n") !=
          std::string::npos);
    CHECK(changed.find("MaxPlayers=6\r\n") != std::string::npos);
    const auto changed_engine = read_bytes(engine_ini);
    const auto published_runtime_path =
        (root.parent_path() / L"Published" / L"BrewedPC")
            .lexically_normal().string();
    CHECK(changed_engine.find("ServerActors=IpDrv.WebServer\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find(
        "Package=KF2OptimizerTelemetry\r\n") == std::string::npos);
    CHECK(changed_engine.find(
        "ScriptPaths=..\\..\\KFGame\\Script\r\n") != std::string::npos);
    CHECK(changed_engine.find(
        "Paths=" + published_runtime_path + "\r\n") != std::string::npos);
    CHECK(changed_engine.find(
        "GameViewportClientClassName=KFGame.KFGameViewportClient\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find(
        "LocalOptions=?Mutator=KF2OptimizerTelemetry."
        "KF2OptimizerTelemetryMutator\r\n") != std::string::npos);
    CHECK(changed_engine.find(
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("bAdaptiveCorpseStagger=False\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("bAdaptiveCorpseDebugMarkers=False\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("bAdaptiveZedDebugMarkers=False\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("AdaptiveCorpseMaximum=0\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("AdaptiveTargetFPS=0\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("AdaptiveQualityChangeBudget=1\r\n") !=
          std::string::npos);
    for (const auto section : {
             "KFGame.KFAISpawnManager_Short",
             "KFGame.KFAISpawnManager_Normal",
             "KFGame.KFAISpawnManager_Long"}) {
        CHECK(changed.find("[" + std::string{section} + "]\r\n"
                           "bLogWaveSpawnTiming=True\r\n") !=
              std::string::npos);
    }

    const auto already_enabled =
        kf2::game::enable_offline_gameplay_logging(root);
    CHECK(already_enabled.has_value());
    CHECK(!already_enabled.value());
    CHECK(read_bytes(game_ini) == changed);
    CHECK(read_bytes(engine_ini) == changed_engine);

    const auto adaptive_enabled =
        kf2::game::enable_offline_gameplay_logging(
            root, true, 350, 137, true, 2, control_token, true);
    CHECK(adaptive_enabled.has_value());
    CHECK(adaptive_enabled.value());
    const auto adaptive_engine = read_bytes(engine_ini);
    CHECK(adaptive_engine.find("bAdaptiveCorpseStagger=True\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("bAdaptiveRuntimeEnabled=True\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("bAdaptiveCorpseDebugMarkers=True\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("bAdaptiveZedDebugMarkers=True\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("AdaptiveCorpseMaximum=350\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("AdaptiveTargetFPS=137\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("AdaptiveQualityChangeBudget=2\r\n") !=
           std::string::npos);
    CHECK(adaptive_engine.find(
        "AdaptiveControlToken=0123456789abcdef0123456789abcdef\r\n") !=
          std::string::npos);
    const auto observed_policy =
        kf2::game::read_offline_adaptive_session_policy(root);
    CHECK(observed_policy.has_value());
    CHECK(observed_policy.value().has_value());
    CHECK(observed_policy.value()->target_fps == 137);
    CHECK(observed_policy.value()->corpse_maximum == 350);
    CHECK(observed_policy.value()->quality_change_budget == 2);
    const auto adaptive_unchanged =
        kf2::game::enable_offline_gameplay_logging(
            root, true, 350, 137, true, 2, control_token, true);
    CHECK(adaptive_unchanged.has_value());
    CHECK(!adaptive_unchanged.value());
    const auto adaptive_initially_off =
        kf2::game::enable_offline_gameplay_logging(
            root, true, 350, 137, true, 2, control_token, true, false);
    CHECK(adaptive_initially_off.has_value());
    CHECK(adaptive_initially_off.value());
    CHECK(read_bytes(engine_ini).find(
              "bAdaptiveRuntimeEnabled=False\r\n") != std::string::npos);

    CHECK(!kf2::game::cleanup_stale_offline_gameplay_configuration(
        root, true).has_value());
    const auto stale_cleaned =
        kf2::game::cleanup_stale_offline_gameplay_configuration(root, false);
    CHECK(stale_cleaned.has_value());
    CHECK(stale_cleaned.value());
    const auto cleaned_engine = read_bytes(engine_ini);
    CHECK(cleaned_engine.find(
        "GameViewportClientClassName=KFGame.KFGameViewportClient") !=
          std::string::npos);
    CHECK(cleaned_engine.find(
        "ScriptPaths=..\\..\\KFGame\\Script") != std::string::npos);
    CHECK(cleaned_engine.find("Paths=" + published_runtime_path) ==
          std::string::npos);
    CHECK(cleaned_engine.find("ServerActors=IpDrv.WebServer") !=
          std::string::npos);
    CHECK(cleaned_engine.find("KF2OptimizerTelemetryBootstrap") ==
          std::string::npos);
    CHECK(cleaned_engine.find("Package=KF2OptimizerTelemetry") ==
          std::string::npos);
    CHECK(cleaned_engine.find("KF2OptimizerTelemetryMutator") ==
          std::string::npos);
    CHECK(cleaned_engine.find("[KF2OptimizerTelemetry.") ==
          std::string::npos);
    CHECK(cleaned_engine.find("AdaptiveControlToken=") == std::string::npos);
    const auto already_clean =
        kf2::game::cleanup_stale_offline_gameplay_configuration(root, false);
    CHECK(already_clean.has_value());
    CHECK(!already_clean.value());

    const std::string existing_local_options_engine =
        "[URL]\r\n"
        "LocalOptions=?Foo=Bar\r\n"
        "\r\n[Core.System]\r\n"
        "ScriptPaths=..\\..\\KFGame\\Script\r\n"
        "\r\n[Engine.Engine]\r\n"
        "GameViewportClientClassName=KFGame.KFGameViewportClient\r\n";
    write_bytes(engine_ini, existing_local_options_engine);
    const auto existing_options_enabled =
        kf2::game::enable_offline_gameplay_logging(root);
    CHECK(existing_options_enabled.has_value());
    CHECK(existing_options_enabled.value());
    CHECK(read_bytes(engine_ini).find(
        "LocalOptions=?Foo=Bar?Mutator=KF2OptimizerTelemetry."
        "KF2OptimizerTelemetryMutator\r\n") != std::string::npos);
    const auto existing_options_cleaned =
        kf2::game::cleanup_stale_offline_gameplay_configuration(root, false);
    CHECK(existing_options_cleaned.has_value());
    CHECK(existing_options_cleaned.value());
    CHECK(read_bytes(engine_ini).find(
        "LocalOptions=?Foo=Bar\r\n") != std::string::npos);

    const std::string foreign_mutator_engine =
        "[URL]\r\n"
        "LocalOptions=?Mutator=Example.Custom\r\n"
        "\r\n[Core.System]\r\n"
        "ScriptPaths=..\\..\\KFGame\\Script\r\n"
        "\r\n[Engine.Engine]\r\n"
        "GameViewportClientClassName=KFGame.KFGameViewportClient\r\n";
    write_bytes(engine_ini, foreign_mutator_engine);
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());
    CHECK(read_bytes(engine_ini) == foreign_mutator_engine);

    const std::string legacy_absolute_path_engine =
        "[Core.System]\r\n"
        "ScriptPaths=" +
        (root.parent_path() / L"Published" / L"BrewedPC")
            .lexically_normal().string() +
        "\r\n[Engine.Engine]\r\n"
        "GameViewportClientClassName=KF2OptimizerTelemetry."
        "KF2OptimizerTelemetryViewport\r\n";
    write_bytes(engine_ini, legacy_absolute_path_engine);
    const auto legacy_path_cleaned =
        kf2::game::cleanup_stale_offline_gameplay_configuration(root, false);
    CHECK(legacy_path_cleaned.has_value());
    CHECK(legacy_path_cleaned.value());
    CHECK(read_bytes(engine_ini).find(
        "ScriptPaths=..\\..\\KFGame\\Script") != std::string::npos);

    const std::string foreign_viewport_engine =
        "[Engine.Engine]\r\n"
        "GameViewportClientClassName=Example.CustomViewport\r\n"
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n"
        "AdaptiveControlToken=0123456789abcdef0123456789abcdef\r\n";
    write_bytes(engine_ini, foreign_viewport_engine);
    const auto foreign_viewport_cleaned =
        kf2::game::cleanup_stale_offline_gameplay_configuration(root, false);
    CHECK(foreign_viewport_cleaned.has_value());
    CHECK(foreign_viewport_cleaned.value());
    const auto foreign_viewport_after = read_bytes(engine_ini);
    CHECK(foreign_viewport_after.find(
        "GameViewportClientClassName=Example.CustomViewport") !=
          std::string::npos);
    CHECK(foreign_viewport_after.find("[KF2OptimizerTelemetry.") ==
          std::string::npos);

    const std::string ambiguous_engine =
        "[Engine.Engine]\r\n"
        "GameViewportClientClassName=KF2OptimizerTelemetry."
        "KF2OptimizerTelemetryViewport\r\n"
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n"
        "AdaptiveControlToken=0123456789abcdef0123456789abcdef\r\n"
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n"
        "AdaptiveTargetFPS=60\r\n";
    write_bytes(engine_ini, ambiguous_engine);
    CHECK(!kf2::game::read_offline_adaptive_session_policy(root).has_value());
    CHECK(!kf2::game::cleanup_stale_offline_gameplay_configuration(
        root, false).has_value());
    CHECK(read_bytes(engine_ini) == ambiguous_engine);
    write_bytes(engine_ini, adaptive_engine);

    write_bytes(engine_ini,
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\n"
        "bAdaptiveCorpseStagger=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 137, false, 1, control_token).has_value());
    write_bytes(engine_ini,
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\n"
        "bAdaptiveCorpseDebugMarkers=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 137, true, 1, control_token).has_value());
    write_bytes(engine_ini,
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\n"
        "bAdaptiveZedDebugMarkers=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 137, false, 1, control_token, true).has_value());
    write_bytes(engine_ini, adaptive_engine);

    write_bytes(engine_ini,
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\n"
        "AdaptiveTargetFPS=invalid\n");
    CHECK(!kf2::game::read_offline_adaptive_session_policy(root).has_value());
    write_bytes(engine_ini,
        "[Engine.Engine]\n"
        "GameViewportClientClassName=KFGame.KFGameViewportClient\n");
    const auto missing_policy =
        kf2::game::read_offline_adaptive_session_policy(root);
    CHECK(missing_policy.has_value());
    CHECK(!missing_policy.value().has_value());
    write_bytes(engine_ini, adaptive_engine);

    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 3, 60, false, 1, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 2001, 60, false, 1, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 29, false, 1, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 241, false, 1, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 60, false, 0, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 60, false, 6, control_token).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(root, false, 350, 0).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(root, false, 0, 60).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, false, 0, 0, true).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, false, 0, 0, false, 1, {}, true).has_value());
    CHECK(!kf2::game::enable_offline_gameplay_logging(
        root, true, 350, 60, false, 2, "invalid").has_value());

    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=False\nbLogAICount=False\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\nMaxPlayers=6\n");
    const auto missing_ai_count =
        kf2::game::enable_offline_gameplay_logging(root);
    CHECK(missing_ai_count.has_value());
    CHECK(missing_ai_count.value());
    CHECK(read_bytes(game_ini).find(
        "[KFGameContent.KFGameInfo_Survival]\n"
        "MaxPlayers=6\n"
        "bLogAICount=True\n") != std::string::npos);

    write_bytes(game_ini,
        "[Other]\nbLogAICount=False\n");
    const auto unrelated_ai_count =
        kf2::game::enable_offline_gameplay_logging(root);
    CHECK(unrelated_ai_count.has_value());
    CHECK(unrelated_ai_count.value());
    CHECK(read_bytes(game_ini).find(
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=True\n") != std::string::npos);

    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=False\n"
        "[KFGame.KFAISpawnManager_Short]\n"
        "bLogWaveSpawnTiming=Maybe\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=False\n"
        "[KFGame.KFAISpawnManager_Short]\n"
        "bLogWaveSpawnTiming=False\n"
        "bLogWaveSpawnTiming=False\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=True\n");
    const auto wave_only = kf2::game::enable_offline_gameplay_logging(root);
    CHECK(wave_only.has_value());
    CHECK(wave_only.value());
    CHECK(read_bytes(game_ini).find(
        "[KFGame.KFAISpawnManager_Short]\n"
        "bLogWaveSpawnTiming=True\n") != std::string::npos);

    write_bytes(game_ini,
        "[KFGameContent.KFGameInfo_Survival]\n"
        "bLogAICount=False\n");
    const auto hardlink = root / L"KFGame-hardlink.ini";
    CHECK(CreateHardLinkW(hardlink.c_str(), game_ini.c_str(), nullptr) != FALSE);
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());
    fs::remove(hardlink, error);

    fs::remove(game_ini, error);
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(game_ini, original);
    fs::remove(engine_ini, error);
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(game_ini, original);
    write_bytes(engine_ini,
        "[Engine.GameEngine]\nServerActors=IpDrv.WebServer\n"
        "[Engine.GameEngine]\nServerActors=Other.Actor\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());
    fs::remove_all(root, error);
    return EXIT_SUCCESS;
}
