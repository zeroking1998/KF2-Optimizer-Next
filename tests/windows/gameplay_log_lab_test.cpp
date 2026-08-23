#include <Windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
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
    const auto telemetry_source = read_bytes(KF2_TELEMETRY_SOURCE);
    const auto viewport_source = read_bytes(KF2_TELEMETRY_VIEWPORT_SOURCE);
    const auto listener_source = read_bytes(KF2_ADAPTIVE_LISTENER_SOURCE);
    const auto connection_source = read_bytes(KF2_ADAPTIVE_CONNECTION_SOURCE);
    const auto graphics_source = read_bytes(KF2_ADAPTIVE_GRAPHICS_SOURCE);
    CHECK(telemetry_source.find("AdaptiveControlToken") != std::string::npos);
    CHECK(telemetry_source.find("ValidAdaptiveControlToken") !=
          std::string::npos);
    CHECK(telemetry_source.find("KF2OPT_ADAPTIVE_QUALITY state=applied") !=
          std::string::npos);
    CHECK(viewport_source.find(
        "class'KF2OptimizerAdaptiveControlListener'") != std::string::npos);
    CHECK(viewport_source.find("var transient WorldInfo ActiveWorld") ==
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
    CHECK(graphics_source.find("KinematicUpdateDistFactorScale = FMax") !=
          std::string::npos);
    CHECK(graphics_source.find("RestoreOwnedSettings") != std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.GpuLevel = Max(Snapshot.GpuLevel, Level)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.CpuLevel = Max(Snapshot.CpuLevel, Level)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.VramLevel = Max(Snapshot.VramLevel, Level)") !=
          std::string::npos);
    CHECK(graphics_source.find(
        "Snapshot.RamLevel = Max(Snapshot.RamLevel, Level)") !=
          std::string::npos);
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
    CHECK(viewport_source.find("t.MaxFPS") == std::string::npos);
    CHECK(viewport_source.find("MaxSmoothedFrameRate") ==
          std::string::npos);
    CHECK(viewport_source.find("KF2OPT_FRAME_RATE") ==
          std::string::npos);
    CHECK(viewport_source.find("KF2OPT_TARGET_FPS") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "var globalconfig int AdaptiveQualityChangeBudget") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int GetAdaptiveCorpseAttackScale()") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "Step = Step * AttackScale") != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceActionInterval / float(AttackScale)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "LodActionInterval / float(AttackScale)") != std::string::npos);
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
        "RestoreAllAdaptiveCorpseLods()") != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared < 640000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_LOD state=active") != std::string::npos);
    CHECK(telemetry_source.find(
        "SelectDistantAwakeMonsterCorpseForSleep") != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumDistanceSquared = 12250000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumDistanceSquared = 6250000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "MinimumDistanceSquared = 3240000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.LastRenderTime >") != std::string::npos);
    CHECK(telemetry_source.find(
        "Candidate.Mesh.WakeRigidBody()") != std::string::npos);
    CHECK(telemetry_source.find(
        "DistanceSquared >= 2250000.0") != std::string::npos);
    CHECK(telemetry_source.find(
        "AwakeTotal = CountAwakeMonsterCorpses(GoreManager)") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "KF2OPT_CORPSE_DISTANCE state=sleep") != std::string::npos);
    CHECK(telemetry_source.find(
        "FindAdaptiveDistanceSleptCorpse(Candidate) != -1") !=
          std::string::npos);
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
    CHECK(count_occurrences(telemetry_source, "corpse_id=") == 3);
    CHECK(count_occurrences(telemetry_source, "distance_units=") == 3);
    CHECK(telemetry_source.find(
        "var globalconfig bool bAdaptiveCorpseDebugMarkers") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "function RegisterAdaptiveCorpseDebugMarker(") != std::string::npos);
    CHECK(telemetry_source.find(
        "function DrawAdaptiveCorpseDebugMarkers(Canvas MarkerCanvas)") !=
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveCorpseDebugMarkers.Length >= 24") !=
          std::string::npos);
    CHECK(telemetry_source.find(
        "GetAdaptiveCorpseActionId(Candidate)") != std::string::npos);
    CHECK(count_occurrences(
        telemetry_source, "RegisterAdaptiveCorpseDebugMarker(Candidate,") == 3);
    CHECK(viewport_source.find("event PostRender(Canvas MarkerCanvas)") !=
          std::string::npos);
    CHECK(viewport_source.find("Super.PostRender(MarkerCanvas)") !=
          std::string::npos);
    CHECK(viewport_source.find(
        "var transient KF2OptimizerTelemetryProbe ActiveProbe") ==
          std::string::npos);
    CHECK(viewport_source.find("var transient WorldInfo ActiveWorld") ==
          std::string::npos);
    CHECK(viewport_source.find(
        "foreach CurrentWorld.DynamicActors(") != std::string::npos);
    CHECK(viewport_source.find(
        "CurrentProbe.DrawAdaptiveCorpseDebugMarkers(MarkerCanvas)") !=
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveDistancePhysicsSleeps % 4") ==
          std::string::npos);
    CHECK(telemetry_source.find("AdaptiveVisibleRagdollSleeps % 4") ==
          std::string::npos);
    CHECK(telemetry_source.find(
        "function int GetAdaptiveCorpseScenePressureLevel(") !=
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
        "WakeOneNearAdaptiveDistanceSleptCorpse()", stagger_start);
    const auto distant_sleep_stage = telemetry_source.find(
        "SleepOneDistantMonsterCorpse(", stagger_start);
    CHECK(stagger_start != std::string::npos);
    CHECK(zed_time_guard != std::string::npos);
    CHECK(wake_stage != std::string::npos);
    CHECK(distant_sleep_stage != std::string::npos);
    CHECK(zed_time_guard < wake_stage);
    CHECK(zed_time_guard < distant_sleep_stage);
    const auto frame_only_action_gate = telemetry_source.find(
        "if (AdaptiveCorpsePressureLevel <= 0)", stagger_start);
    CHECK(frame_only_action_gate != std::string::npos);
    CHECK(distant_sleep_stage < frame_only_action_gate);

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
        "function bool WakeOneNearAdaptiveDistanceSleptCorpse()");
    const auto wake_threshold = telemetry_source.find(
        "DistanceSquared >= 2250000.0", wake_function);
    const auto wake_call = telemetry_source.find(
        "Candidate.Mesh.WakeRigidBody()", wake_function);
    const auto wake_readback = telemetry_source.find(
        "if (!Candidate.Mesh.RigidBodyIsAwake())", wake_call);
    const auto wake_tracking_release = telemetry_source.find(
        "RemoveAdaptiveDistanceSleptCorpseEntry(Index)", wake_function);
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

    const auto ragdoll_selector = telemetry_source.find(
        "function KFPawn SelectVisibleAwakeMonsterCorpseForSleep(");
    const auto ragdoll_function = telemetry_source.find(
        "function bool SleepOneVisibleMonsterCorpse(");
    const auto ragdoll_ownership_array = telemetry_source.find(
        "var array<string> AdaptiveCorpseRagdollSleepIds");
    const auto ragdoll_ownership_count = telemetry_source.find(
        "var int AdaptiveCorpseRagdollSleepIdCount");
    const auto ragdoll_ownership_capacity = telemetry_source.find(
        "AdaptiveCorpseRagdollSleepIds.Length = 8192");
    const auto ragdoll_ownership_hash = telemetry_source.find(
        "function int GetAdaptiveCorpseRagdollSleepHash(string CorpseId)");
    const auto ragdoll_ownership_lookup = telemetry_source.find(
        "function int FindAdaptiveCorpseRagdollSleepId(string CorpseId)");
    const auto ragdoll_ownership_filter = telemetry_source.find(
        "FindAdaptiveCorpseRagdollSleepId(", ragdoll_selector);
    const auto ragdoll_ownership_id = telemetry_source.find(
        "GetAdaptiveCorpseActionId(Candidate)) != -1",
        ragdoll_ownership_filter);
    const auto ragdoll_ownership_register = telemetry_source.find(
        "RegisterAdaptiveCorpseRagdollSleep(Candidate)",
        ragdoll_selector);
    const auto ragdoll_awake_filter = telemetry_source.find(
        "!Candidate.Mesh.RigidBodyIsAwake()", ragdoll_selector);
    const auto ragdoll_sleep_call = telemetry_source.find(
        "Candidate.Mesh.PutRigidBodyToSleep()", ragdoll_function);
    const auto ragdoll_sleep_readback = telemetry_source.find(
        "if (Candidate.Mesh.RigidBodyIsAwake())", ragdoll_sleep_call);
    const auto ragdoll_sleep_register = telemetry_source.find(
        "RegisterAdaptiveCorpseRagdollSleep(Candidate)",
        ragdoll_sleep_readback);
    const auto ragdoll_counter = telemetry_source.find(
        "++AdaptiveVisibleRagdollSleeps", ragdoll_function);
    const auto ragdoll_receipt = telemetry_source.find(
        "KF2OPT_CORPSE_RAGDOLL state=sleep", ragdoll_function);
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
    CHECK(ragdoll_sleep_call != std::string::npos);
    CHECK(ragdoll_sleep_readback != std::string::npos);
    CHECK(ragdoll_sleep_register != std::string::npos);
    CHECK(ragdoll_counter != std::string::npos);
    CHECK(ragdoll_receipt != std::string::npos);
    CHECK(ragdoll_ownership_receipt != std::string::npos);
    CHECK(ragdoll_ownership_filter < ragdoll_awake_filter);
    CHECK(ragdoll_sleep_call < ragdoll_sleep_readback);
    CHECK(ragdoll_sleep_readback < ragdoll_sleep_register);
    CHECK(ragdoll_sleep_register < ragdoll_counter);
    CHECK(ragdoll_counter < ragdoll_receipt);
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
        "[Engine.Engine]\r\n"
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
    CHECK(changed_engine.find("ServerActors=IpDrv.WebServer\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find(
        "GameViewportClientClassName=KF2OptimizerTelemetry.KF2OptimizerTelemetryViewport\r\n") !=
        std::string::npos);
    CHECK(changed_engine.find("KFGame.KFGameViewportClient") ==
          std::string::npos);
    CHECK(changed_engine.find(
        "[KF2OptimizerTelemetry.KF2OptimizerTelemetryProbe]\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("bAdaptiveCorpseStagger=False\r\n") !=
          std::string::npos);
    CHECK(changed_engine.find("bAdaptiveCorpseDebugMarkers=False\r\n") !=
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
            root, true, 350, 137, true, 2, control_token);
    CHECK(adaptive_enabled.has_value());
    CHECK(adaptive_enabled.value());
    const auto adaptive_engine = read_bytes(engine_ini);
    CHECK(adaptive_engine.find("bAdaptiveCorpseStagger=True\r\n") !=
          std::string::npos);
    CHECK(adaptive_engine.find("bAdaptiveCorpseDebugMarkers=True\r\n") !=
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
    const auto adaptive_unchanged =
        kf2::game::enable_offline_gameplay_logging(
            root, true, 350, 137, true, 2, control_token);
    CHECK(adaptive_unchanged.has_value());
    CHECK(!adaptive_unchanged.value());

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
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

    write_bytes(game_ini,
        "[Other]\nbLogAICount=False\n");
    CHECK(!kf2::game::enable_offline_gameplay_logging(root).has_value());

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
