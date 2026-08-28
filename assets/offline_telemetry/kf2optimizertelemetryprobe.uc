// Telemetry actor for protected offline KF2 laboratory sessions.
// The interaction inserted by the Published mutator creates this actor only
// after a standalone gameplay world exists, so the normal KF2 menu flow and
// native viewport remain preserved. The actor
// destroys itself outside a local standalone world. Its optional Adaptive
// actuators use only KF2's official
// confirmed-corpse cleanup and rigid-body sleep paths and remain disabled unless
// Adaptive explicitly enables them in the protected session INI.
class KF2OptimizerTelemetryProbe extends Info config(Engine);

struct AdaptiveCorpseDebugMarkerEntry
{
    var KFPawn Corpse;
    var string CorpseId;
    var string Action;
    var float ExpiresRealTime;
};

var int SampleSequence;
var globalconfig bool bAdaptiveCorpseStagger;
var globalconfig bool bAdaptiveCorpseDebugMarkers;
var globalconfig int AdaptiveCorpseMaximum;
var globalconfig int AdaptiveTargetFPS;
var globalconfig int AdaptiveQualityChangeBudget;
var globalconfig string AdaptiveControlToken;
var transient KF2OptimizerAdaptiveGraphicsState AdaptiveGraphicsState;
var int AdaptiveGraphicsQuality;
var string AdaptiveGraphicsResource;
var int AdaptiveLastControlSequence;
var bool bAdaptiveCorpseStaggerInitialized;
var int AdaptiveCorpseTarget;
var int AdaptiveCorpseOriginalLimit;
var int AdaptiveCorpseRuntimeLimit;
var int AdaptiveCorpsesRemoved;
var KFGoreManager AdaptiveCorpseManager;
var float AdaptiveFrameTimeEmaMs;
var float AdaptiveFrameBaselineMs;
var int AdaptiveFrameBaselineSamples;
var int AdaptiveCorpsePressureSamples;
var int AdaptiveCorpseRecoverySamples;
var int AdaptiveCorpsePressureLevel;
var int AdaptiveCorpseCurrentFramePressureLevel;
var float AdaptiveFramePressureObservedRealTime;
var int AdaptiveCorpsesSlept;
var int AdaptiveBaselinePhysicsSleeps;
var int AdaptiveSkeletonReductions;
var float AdaptiveLastCorpseSleepRealTime;
var float AdaptiveLastCorpseCapacityRealTime;
var array<KFPawn> AdaptiveCorpseLodCorpses;
var array<int> AdaptiveCorpseLodOriginalMinModels;
var array<int> AdaptiveCorpseLodAppliedMinModels;
var int AdaptiveCorpseLodReductions;
var int AdaptiveCorpseLodRestores;
var float AdaptiveLastCorpseLodRealTime;
var array<KFPawn> AdaptiveDistanceSleptCorpses;
var int AdaptiveDistancePhysicsSleeps;
var int AdaptiveDistancePhysicsWakes;
var int AdaptiveVisibleRagdollSleeps;
var array<string> AdaptiveCorpseRagdollSleepIds;
var int AdaptiveCorpseRagdollSleepIdCount;
var float AdaptiveLastDistancePhysicsRealTime;
var int AdaptiveVisibleLivingZeds;
var float AdaptiveVisibleLivingObservedRealTime;
var int AdaptiveCorpseScenePressureLevel;
var array<AdaptiveCorpseDebugMarkerEntry> AdaptiveCorpseDebugMarkers;
var array<KFPawn_Monster> AdaptiveLivingVisualZeds;
var array<int> AdaptiveLivingOriginalMinLods;
var array<int> AdaptiveLivingAppliedMinLods;
var array<float> AdaptiveLivingOriginalAnimDistances;
var array<float> AdaptiveLivingAppliedAnimDistances;
var array<int> AdaptiveLivingOriginalAnimRates;
var array<int> AdaptiveLivingAppliedAnimRates;
var int AdaptiveLivingVisualReductions;
var int AdaptiveLivingVisualRestores;

function bool ValidAdaptiveControlToken(string Candidate)
{
    return Len(AdaptiveControlToken) == 32 && Candidate == AdaptiveControlToken;
}

function bool ApplyAdaptiveResourceControl(
    string Token, int Sequence, string Resource, int Quality)
{
    local int QualityStage;

    if (!ValidAdaptiveControlToken(Token) || Sequence <= 0 ||
        Sequence <= AdaptiveLastControlSequence ||
        Quality < 10 || Quality > 100 ||
        !((Resource ~= "gpu") || (Resource ~= "vram") ||
          (Resource ~= "cpu") || (Resource ~= "ram") ||
          (Resource ~= "mixed") || (Resource ~= "recover")))
    {
        return false;
    }
    QualityStage = Clamp((Quality + 9) / 10, 1, 10);

    if (AdaptiveGraphicsState == None)
    {
        AdaptiveGraphicsState = new(self)
            class'KF2OptimizerAdaptiveGraphicsState';
    }
    if (AdaptiveGraphicsState == None ||
        !class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(
            AdaptiveGraphicsState, Resource, Quality))
    {
        `log("KF2OPT_ADAPTIVE_QUALITY state=failed seq="$Sequence$
             " resource="$Resource$" quality="$Quality$
             " reason=readback_mismatch");
        return false;
    }

    AdaptiveGraphicsQuality = Quality;
    AdaptiveGraphicsResource = Resource;
    AdaptiveLastControlSequence = Sequence;
    `log("KF2OPT_ADAPTIVE_QUALITY state=applied seq="$Sequence$
         " resource="$Resource$" quality="$Quality$
         " stage="$QualityStage$
         " readback=verified");
    return true;
}

function RestoreAdaptiveGraphics()
{
    if (!class'KF2OptimizerAdaptiveGraphics'.static.RestoreOriginal(
            AdaptiveGraphicsState))
    {
        `log("KF2OPT_ADAPTIVE_QUALITY state=restore_failed reason=readback_mismatch");
        return;
    }
    AdaptiveGraphicsQuality = 100;
    AdaptiveGraphicsResource = "recover";
    AdaptiveGraphicsState = None;
    `log("KF2OPT_ADAPTIVE_QUALITY state=restored readback=verified");
}

function int SelectStaggeredCorpse(KFGoreManager GoreManager)
{
    local int Index;
    local int SelectedIndex;
    local int CandidateTier;
    local int SelectedTier;
    local float CandidateDeathTime;
    local float SelectedDeathTime;
    local bool Offscreen;
    local bool Sleeping;
    local KFPawn Candidate;

    SelectedIndex = -1;
    SelectedTier = 100;
    SelectedDeathTime = WorldInfo.TimeSeconds + 1.0;
    for (Index = 0; Index < GoreManager.CorpsePool.Length; ++Index)
    {
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.TimeOfDeath <= 0.0 ||
            WorldInfo.TimeSeconds - Candidate.TimeOfDeath < 1.5)
        {
            continue;
        }
        Offscreen = Candidate.Mesh == None ||
            Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.3;
        Sleeping = Candidate.Physics != PHYS_RigidBody ||
            Candidate.Mesh == None || !Candidate.Mesh.RigidBodyIsAwake();
        if (Offscreen && Sleeping)
        {
            CandidateTier = 0;
        }
        else if (Offscreen)
        {
            CandidateTier = 1;
        }
        else if (Sleeping)
        {
            CandidateTier = 2;
        }
        else
        {
            CandidateTier = 3;
        }
        CandidateDeathTime = Candidate.TimeOfDeath;
        if (CandidateTier < SelectedTier ||
            (CandidateTier == SelectedTier &&
             CandidateDeathTime < SelectedDeathTime))
        {
            SelectedIndex = Index;
            SelectedTier = CandidateTier;
            SelectedDeathTime = CandidateDeathTime;
        }
    }
    return SelectedIndex;
}

function int GetAdaptiveCorpseAttackScale()
{
    if (AdaptiveQualityChangeBudget < 1 ||
        AdaptiveQualityChangeBudget > 5)
    {
        return 1;
    }
    return AdaptiveQualityChangeBudget;
}

function bool HasConfirmedAdaptivePerformancePressure()
{
    return AdaptiveLastControlSequence > 0 &&
        AdaptiveGraphicsQuality >= 10 && AdaptiveGraphicsQuality < 100 &&
        !(AdaptiveGraphicsResource ~= "recover");
}

function InitializeAdaptiveCorpseStagger(KFGoreManager GoreManager)
{
    AdaptiveCorpseManager = GoreManager;
    AdaptiveCorpseOriginalLimit = GoreManager.MaxDeadBodies;
    if (AdaptiveCorpseMaximum >= 4 && AdaptiveCorpseMaximum <= 2000)
    {
        AdaptiveCorpseTarget = AdaptiveCorpseMaximum;
    }
    else
    {
        AdaptiveCorpseTarget = Clamp(AdaptiveCorpseOriginalLimit, 4, 2000);
    }
    // The user's value is the stable ceiling. Scene-driven physics sleep never
    // changes it; only confirmed frame-time pressure may move the separate
    // runtime cleanup threshold below.
    AdaptiveCorpseRuntimeLimit = AdaptiveCorpseTarget;
    GoreManager.MaxDeadBodies = AdaptiveCorpseTarget;
    AdaptiveLastCorpseCapacityRealTime = WorldInfo.RealTimeSeconds;
    AdaptiveCorpsePressureSamples = 0;
    AdaptiveCorpseRecoverySamples = 0;
    AdaptiveCorpsePressureLevel = 0;
    AdaptiveCorpseCurrentFramePressureLevel = 0;
    AdaptiveFramePressureObservedRealTime = 0.0;
    bAdaptiveCorpseStaggerInitialized = true;
    `log("KF2OPT_CORPSE_STAGGER state=enabled target="$
         AdaptiveCorpseTarget$" runtime_limit="$AdaptiveCorpseRuntimeLimit$
         " quality_steps="$GetAdaptiveCorpseAttackScale()$
         " physics=distance_visibility_density frame_pressure=amplifier"$
         " cleanup_batch=1 cleanup_interval_ms=450");
}

function AdjustAdaptiveCorpseCapacity(
    KFGoreManager GoreManager, int ConfirmedPressureLevel)
{
    local int BaseLimit;
    local int AttackScale;
    local int Step;
    local int NewLimit;
    local float Interval;

    if (GoreManager == None || WorldInfo == None)
    {
        return;
    }
    AttackScale = GetAdaptiveCorpseAttackScale();
    Interval = ConfirmedPressureLevel >= 2 ? 0.8 : 1.4;
    if (ConfirmedPressureLevel <= 0)
    {
        Interval = 1.8;
    }
    else
    {
        Interval = FMax(0.25, Interval / float(AttackScale));
    }
    if (WorldInfo.RealTimeSeconds - AdaptiveLastCorpseCapacityRealTime <
        Interval)
    {
        return;
    }

    NewLimit = AdaptiveCorpseRuntimeLimit;
    if (ConfirmedPressureLevel > 0)
    {
        BaseLimit = Min(AdaptiveCorpseRuntimeLimit,
                        GoreManager.CorpsePool.Length);
        Step = ConfirmedPressureLevel >= 2
            ? Clamp(GoreManager.CorpsePool.Length / 12, 4, 32)
            : Clamp(GoreManager.CorpsePool.Length / 24, 2, 16);
        Step = Step * AttackScale;
        NewLimit = Max(4, BaseLimit - Step);
    }
    else if (AdaptiveCorpseRecoverySamples >= 8 &&
             AdaptiveCorpseRuntimeLimit < AdaptiveCorpseTarget)
    {
        Step = Clamp((AdaptiveCorpseTarget -
                      AdaptiveCorpseRuntimeLimit) / 8, 4, 64);
        NewLimit = Min(AdaptiveCorpseTarget,
                       AdaptiveCorpseRuntimeLimit + Step);
    }

    if (NewLimit != AdaptiveCorpseRuntimeLimit)
    {
        AdaptiveCorpseRuntimeLimit = NewLimit;
        AdaptiveLastCorpseCapacityRealTime = WorldInfo.RealTimeSeconds;
        `log("KF2OPT_CORPSE_CAPACITY runtime_limit="$
             AdaptiveCorpseRuntimeLimit$" selected_max="$AdaptiveCorpseTarget$
             " pressure="$ConfirmedPressureLevel$" quality_steps="$
             AttackScale$" pool="$
             GoreManager.CorpsePool.Length);
    }
}

function StaggerCorpseCleanup()
{
    local int SelectedIndex;
    local KFGoreManager GoreManager;
    local KFGameInfo GameInfo;

    if (!bAdaptiveCorpseStagger || WorldInfo == None ||
        WorldInfo.NetMode != NM_Standalone)
    {
        return;
    }
    GoreManager = KFGoreManager(WorldInfo.MyGoreEffectManager);
    if (GoreManager == None)
    {
        return;
    }
    if (!bAdaptiveCorpseStaggerInitialized ||
        AdaptiveCorpseManager != GoreManager)
    {
        InitializeAdaptiveCorpseStagger(GoreManager);
        return;
    }
    // Exceeding the selected count alone never deletes a corpse. Require both
    // the hysteresis-confirmed state and a fresh current frame-time pressure
    // sample, so recovery immediately stops cleanup without threshold chatter.
    if (GoreManager.CorpsePool.Length <= AdaptiveCorpseRuntimeLimit ||
        AdaptiveCorpsePressureLevel <= 0 ||
        AdaptiveCorpseCurrentFramePressureLevel <= 0 ||
        AdaptiveFramePressureObservedRealTime <= 0.0 ||
        WorldInfo.RealTimeSeconds - AdaptiveFramePressureObservedRealTime > 0.4)
    {
        return;
    }
    GameInfo = KFGameInfo(WorldInfo.Game);
    if (GameInfo != None && GameInfo.IsZedTimeActive())
    {
        return;
    }
    SelectedIndex = SelectStaggeredCorpse(GoreManager);
    if (SelectedIndex >= 0 &&
        GoreManager.RemoveAndDeleteCorpse(SelectedIndex))
    {
        ++AdaptiveCorpsesRemoved;
        if (AdaptiveCorpsesRemoved == 1 || AdaptiveCorpsesRemoved % 8 == 0)
        {
            `log("KF2OPT_CORPSE_STAGGER state=active removed="$
                 AdaptiveCorpsesRemoved$" remaining="$
                 GoreManager.CorpsePool.Length$" runtime_limit="$
                 AdaptiveCorpseRuntimeLimit$" selected_max="$
                 AdaptiveCorpseTarget$" pressure="$
                 AdaptiveCorpseCurrentFramePressureLevel);
        }
    }
}

function int CountVisibleAwakeMonsterCorpses(
    KFGoreManager GoreManager, out int VisibleCorpseCount)
{
    local int Index;
    local int Count;
    local KFPawn Candidate;

    VisibleCorpseCount = 0;
    for (Index = 0; Index < GoreManager.CorpsePool.Length; ++Index)
    {
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
            Candidate.TimeOfDeath <= 0.0)
        {
            continue;
        }
        if (Candidate.Mesh.LastRenderTime > WorldInfo.TimeSeconds - 0.3)
        {
            ++VisibleCorpseCount;
            if (Candidate.Physics == PHYS_RigidBody &&
                Candidate.Mesh.RigidBodyIsAwake())
            {
                ++Count;
            }
        }
    }
    return Count;
}

function int CountAwakeMonsterCorpses(KFGoreManager GoreManager)
{
    local int Index;
    local int Count;
    local KFPawn Candidate;

    for (Index = 0; Index < GoreManager.CorpsePool.Length; ++Index)
    {
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate != None && !Candidate.bDeleteMe &&
            KFPawn_Monster(Candidate) != None && Candidate.Mesh != None &&
            Candidate.TimeOfDeath > 0.0 &&
            Candidate.Physics == PHYS_RigidBody &&
            Candidate.Mesh.RigidBodyIsAwake())
        {
            ++Count;
        }
    }
    return Count;
}

function int GetAdaptiveCorpseScenePressureLevel(
    int VisibleLivingZeds, bool bLivingVisibilityFresh,
    int VisibleCorpses, int VisibleAwakeCorpses, int AwakeCorpses,
    int VisibleCorpseThreshold)
{
    if (AwakeCorpses <= 0)
    {
        return 0;
    }
    if (VisibleAwakeCorpses >= VisibleCorpseThreshold + 2 ||
        VisibleCorpses >= VisibleCorpseThreshold + 4 ||
        AwakeCorpses >= Max(10, VisibleCorpseThreshold * 2) ||
        (bLivingVisibilityFresh && VisibleLivingZeds >= 12 &&
         AwakeCorpses >= 4))
    {
        return 2;
    }
    if (VisibleAwakeCorpses >= VisibleCorpseThreshold ||
        VisibleCorpses >= VisibleCorpseThreshold + 2 ||
        AwakeCorpses >= Max(6, VisibleCorpseThreshold + 3) ||
        (bLivingVisibilityFresh && VisibleLivingZeds >= 8 &&
         AwakeCorpses >= 2))
    {
        return 1;
    }
    return 0;
}

function float GetAdaptiveLivingEnemyPressureScale(
    int VisibleLivingZeds, bool bLivingVisibilityFresh)
{
    local float DistanceSquared;
    local float WeightedVisibleZeds;
    local KFPawn_Monster Candidate;
    local PlayerController LocalPC;

    if (!bLivingVisibilityFresh || VisibleLivingZeds <= 0)
    {
        return 0.0;
    }
    LocalPC = GetALocalPlayerController();
    if (LocalPC == None || LocalPC.ViewTarget == None)
    {
        return 0.0;
    }
    // Every recently visible enemy contributes once. A visible enemy within
    // 1,200 units contributes one extra point and one within 600 contributes
    // two extra points, so a close crowd raises pressure sooner than a distant
    // crowd of the same size.
    WeightedVisibleZeds = float(VisibleLivingZeds);
    foreach WorldInfo.AllPawns(class'KFPawn_Monster', Candidate)
    {
        if (Candidate == None || Candidate.bDeleteMe ||
            !Candidate.IsAliveAndWell() || Candidate.Mesh == None ||
            Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.3)
        {
            continue;
        }
        DistanceSquared = VSizeSq(
            Candidate.Location - LocalPC.ViewTarget.Location);
        if (DistanceSquared < 360000.0)
        {
            WeightedVisibleZeds += 2.0;
        }
        else if (DistanceSquared < 1440000.0)
        {
            WeightedVisibleZeds += 1.0;
        }
    }
    // Even one visible living Zed receives a small visual-only baseline away
    // from the player. Pressure then scales continuously to maximum at eighty
    // weighted Zeds. Movement, collision, hit detection and gameplay physics
    // remain native; only real mesh LOD and animation update inputs change.
    return FClamp(
        0.05 + ((WeightedVisibleZeds - 1.0) / 79.0) * 0.95, 0.05, 1.0);
}

function int GetAdaptiveLivingEnemyPressureLevel(float PressureScale)
{
    if (PressureScale <= 0.0)
    {
        return 0;
    }
    // Integer-only corpse and mesh controls consume five evenly distributed
    // levels derived from the continuous pressure signal.
    return Clamp(1 + int(PressureScale * 4.999), 1, 5);
}

function int FindAdaptiveLivingVisualEntry(KFPawn_Monster Candidate)
{
    local int Index;

    for (Index = 0; Index < AdaptiveLivingVisualZeds.Length; ++Index)
    {
        if (AdaptiveLivingVisualZeds[Index] == Candidate)
        {
            return Index;
        }
    }
    return -1;
}

function RemoveAdaptiveLivingVisualEntry(int Index, bool bRestore)
{
    local KFPawn_Monster Candidate;

    if (Index < 0 || Index >= AdaptiveLivingVisualZeds.Length ||
        Index >= AdaptiveLivingOriginalMinLods.Length ||
        Index >= AdaptiveLivingAppliedMinLods.Length ||
        Index >= AdaptiveLivingOriginalAnimDistances.Length ||
        Index >= AdaptiveLivingAppliedAnimDistances.Length ||
        Index >= AdaptiveLivingOriginalAnimRates.Length ||
        Index >= AdaptiveLivingAppliedAnimRates.Length)
    {
        return;
    }
    Candidate = AdaptiveLivingVisualZeds[Index];
    if (bRestore && Candidate != None && !Candidate.bDeleteMe &&
        Candidate.Mesh != None)
    {
        if (Candidate.Mesh.MinLodModel == AdaptiveLivingAppliedMinLods[Index])
        {
            Candidate.Mesh.MinLodModel = AdaptiveLivingOriginalMinLods[Index];
        }
        if (Candidate.Mesh.AnimationLODDistanceFactor ==
            AdaptiveLivingAppliedAnimDistances[Index])
        {
            Candidate.Mesh.AnimationLODDistanceFactor =
                AdaptiveLivingOriginalAnimDistances[Index];
        }
        if (Candidate.Mesh.AnimationLODFrameRate ==
            AdaptiveLivingAppliedAnimRates[Index])
        {
            Candidate.Mesh.AnimationLODFrameRate =
                AdaptiveLivingOriginalAnimRates[Index];
        }
        ++AdaptiveLivingVisualRestores;
    }
    AdaptiveLivingVisualZeds.Remove(Index, 1);
    AdaptiveLivingOriginalMinLods.Remove(Index, 1);
    AdaptiveLivingAppliedMinLods.Remove(Index, 1);
    AdaptiveLivingOriginalAnimDistances.Remove(Index, 1);
    AdaptiveLivingAppliedAnimDistances.Remove(Index, 1);
    AdaptiveLivingOriginalAnimRates.Remove(Index, 1);
    AdaptiveLivingAppliedAnimRates.Remove(Index, 1);
}

function RestoreAllAdaptiveLivingVisuals()
{
    local int Index;

    for (Index = AdaptiveLivingVisualZeds.Length - 1; Index >= 0; --Index)
    {
        RemoveAdaptiveLivingVisualEntry(Index, true);
    }
}

function PruneAdaptiveLivingVisualEntries()
{
    local int Index;
    local KFPawn_Monster Candidate;

    for (Index = AdaptiveLivingVisualZeds.Length - 1; Index >= 0; --Index)
    {
        Candidate = AdaptiveLivingVisualZeds[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            !Candidate.IsAliveAndWell() || Candidate.Mesh == None)
        {
            RemoveAdaptiveLivingVisualEntry(Index, false);
        }
        else if (Candidate.Mesh.MinLodModel !=
                     AdaptiveLivingAppliedMinLods[Index] ||
                 Candidate.Mesh.AnimationLODDistanceFactor !=
                     AdaptiveLivingAppliedAnimDistances[Index] ||
                 Candidate.Mesh.AnimationLODFrameRate !=
                     AdaptiveLivingAppliedAnimRates[Index])
        {
            // Another KF2 system took ownership. Stop tracking instead of
            // overwriting its values during a later restore.
            RemoveAdaptiveLivingVisualEntry(Index, false);
        }
    }
}

function ApplyLivingEnemyVisualPressure(
    int EnemyPressureLevel, float PressureScale)
{
    local int EntryIndex;
    local int MaximumMinLod;
    local int TargetMinLod;
    local int TargetAnimRate;
    local float DistanceSquared;
    local float MinimumDistance;
    local float MinimumDistanceSquared;
    local float TargetAnimDistance;
    local KFPawn_Monster Candidate;
    local PlayerController LocalPC;

    if (EnemyPressureLevel <= 0 || PressureScale <= 0.0)
    {
        RestoreAllAdaptiveLivingVisuals();
        return;
    }
    PruneAdaptiveLivingVisualEntries();
    LocalPC = GetALocalPlayerController();
    if (LocalPC == None || LocalPC.ViewTarget == None)
    {
        return;
    }
    MinimumDistance = 600.0 - PressureScale * 300.0;
    MinimumDistanceSquared = MinimumDistance * MinimumDistance;
    foreach WorldInfo.AllPawns(class'KFPawn_Monster', Candidate)
    {
        if (Candidate == None || Candidate.bDeleteMe ||
            !Candidate.IsAliveAndWell() || Candidate.Mesh == None ||
            Candidate.Mesh.SkeletalMesh == None ||
            Candidate.Mesh.SkeletalMesh.LODInfo.Length < 2 ||
            Candidate.Mesh.ForcedLodModel != 0)
        {
            continue;
        }
        DistanceSquared = VSizeSq(
            Candidate.Location - LocalPC.ViewTarget.Location);
        EntryIndex = FindAdaptiveLivingVisualEntry(Candidate);
        if (DistanceSquared < MinimumDistanceSquared)
        {
            if (EntryIndex >= 0 && DistanceSquared < 90000.0)
            {
                RemoveAdaptiveLivingVisualEntry(EntryIndex, true);
            }
            continue;
        }
        MaximumMinLod = Candidate.Mesh.SkeletalMesh.LODInfo.Length - 1;
        // Each pressure tier consumes another real mesh LOD when available.
        // The final mesh LOD remains the hard limit for every Zed type.
        TargetMinLod = 1 + int(PressureScale * float(MaximumMinLod));
        if (DistanceSquared >= 1440000.0)
        {
            ++TargetMinLod;
        }
        TargetMinLod = Min(TargetMinLod, MaximumMinLod);
        TargetAnimDistance = FMin(0.55, 0.15 + PressureScale * 0.40);
        TargetAnimRate = Clamp(2 + int(PressureScale * 4.999), 2, 6);
        if (DistanceSquared >= 1440000.0)
        {
            TargetAnimRate = Min(6, TargetAnimRate + 1);
        }
        if (EntryIndex < 0)
        {
            EntryIndex = AdaptiveLivingVisualZeds.Length;
            AdaptiveLivingVisualZeds.AddItem(Candidate);
            AdaptiveLivingOriginalMinLods.AddItem(Candidate.Mesh.MinLodModel);
            AdaptiveLivingAppliedMinLods.AddItem(TargetMinLod);
            AdaptiveLivingOriginalAnimDistances.AddItem(
                Candidate.Mesh.AnimationLODDistanceFactor);
            AdaptiveLivingAppliedAnimDistances.AddItem(TargetAnimDistance);
            AdaptiveLivingOriginalAnimRates.AddItem(
                Candidate.Mesh.AnimationLODFrameRate);
            AdaptiveLivingAppliedAnimRates.AddItem(TargetAnimRate);
        }
        else
        {
            AdaptiveLivingAppliedMinLods[EntryIndex] = TargetMinLod;
            AdaptiveLivingAppliedAnimDistances[EntryIndex] = TargetAnimDistance;
            AdaptiveLivingAppliedAnimRates[EntryIndex] = TargetAnimRate;
        }
        if (Candidate.Mesh.MinLodModel == TargetMinLod &&
            Candidate.Mesh.AnimationLODDistanceFactor == TargetAnimDistance &&
            Candidate.Mesh.AnimationLODFrameRate == TargetAnimRate)
        {
            continue;
        }
        Candidate.Mesh.MinLodModel = TargetMinLod;
        Candidate.Mesh.AnimationLODDistanceFactor = TargetAnimDistance;
        Candidate.Mesh.AnimationLODFrameRate = TargetAnimRate;
        if (Candidate.Mesh.MinLodModel != TargetMinLod ||
            Candidate.Mesh.AnimationLODDistanceFactor != TargetAnimDistance ||
            Candidate.Mesh.AnimationLODFrameRate != TargetAnimRate)
        {
            RemoveAdaptiveLivingVisualEntry(EntryIndex, false);
            continue;
        }
        ++AdaptiveLivingVisualReductions;
        `log("KF2OPT_LIVING_VISUAL state=applied enemy_level="$
             EnemyPressureLevel$" pressure_pct="$int(PressureScale * 100.0)$
             " min_lod="$TargetMinLod$" anim_factor="$
             TargetAnimDistance$" anim_rate="$TargetAnimRate$" distance_units="$
             GetAdaptiveCorpseDistanceUnits(Candidate)$" readback=verified");
    }
}

function RefreshSleepingCorpseAnimationState(KFGoreManager GoreManager)
{
    local int Index;
    local KFPawn Candidate;

    for (Index = 0; Index < GoreManager.CorpsePool.Length; ++Index)
    {
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
            Candidate.Physics != PHYS_RigidBody ||
            Candidate.Mesh.RigidBodyIsAwake())
        {
            continue;
        }
        Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true;
        if (!Candidate.Mesh.bNoSkeletonUpdate)
        {
            // This mirrors KFPawn.Dying.OnSleepRBPhysics: a sleeping corpse
            // no longer needs per-frame skeleton evaluation. OnWakeRBPhysics
            // restores it automatically if gameplay wakes the ragdoll again.
            Candidate.Mesh.bNoSkeletonUpdate = true;
            ++AdaptiveSkeletonReductions;
        }
    }
}

function int SleepBaselineAwakeMonsterCorpses(KFGoreManager GoreManager)
{
    local int Index;
    local int SleepsThisPass;
    local int ForcedSleep;
    local float CorpseAge;
    local float MinimumSettleAge;
    local float MaximumFullPhysicsAge;
    local float SettledSpeedSquared;
    local KFPawn Candidate;

    // This is the always-on low-cost corpse baseline. It deliberately does
    // not consult FPS, quality, distance, scene pressure, enemy pressure or
    // the user's visible-corpse maximum. A new ragdoll gets a short physical
    // reaction window, then settled bodies sleep early and every remaining
    // awake body receives a hard upper bound on full rigid-body simulation.
    MinimumSettleAge = 0.75;
    MaximumFullPhysicsAge = 2.0;
    SettledSpeedSquared = 90000.0;

    for (Index = 0; Index < GoreManager.CorpsePool.Length; ++Index)
    {
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
            Candidate.TimeOfDeath <= 0.0 ||
            Candidate.Physics != PHYS_RigidBody ||
            Candidate.SpecialMove == SM_DeathAnim ||
            !Candidate.Mesh.RigidBodyIsAwake())
        {
            continue;
        }
        CorpseAge = WorldInfo.TimeSeconds - Candidate.TimeOfDeath;
        if (CorpseAge < MinimumSettleAge ||
            (CorpseAge < MaximumFullPhysicsAge &&
             VSizeSq(Candidate.Velocity) > SettledSpeedSquared))
        {
            continue;
        }

        Candidate.Mesh.PutRigidBodyToSleep();
        if (Candidate.Mesh.RigidBodyIsAwake())
        {
            continue;
        }
        Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true;
        Candidate.Mesh.bNoSkeletonUpdate = true;
        ForcedSleep = CorpseAge >= MaximumFullPhysicsAge ? 1 : 0;
        ++SleepsThisPass;
        ++AdaptiveBaselinePhysicsSleeps;
        ++AdaptiveCorpsesSlept;
        RegisterAdaptiveCorpseDebugMarker(Candidate, "BASE_SLEEP");
        `log("KF2OPT_CORPSE_BASELINE state=sleep slept="$
             AdaptiveBaselinePhysicsSleeps$" batch="$SleepsThisPass$
             " forced="$ForcedSleep$" age_ms="$int(CorpseAge * 1000.0)$
             " speed_units="$int(VSize(Candidate.Velocity))$" corpse_id="$
             GetAdaptiveCorpseActionId(Candidate)$" effective_awake=0");
    }
    return SleepsThisPass;
}

function int FindAdaptiveCorpseLodEntry(KFPawn Candidate)
{
    local int Index;

    for (Index = 0; Index < AdaptiveCorpseLodCorpses.Length; ++Index)
    {
        if (AdaptiveCorpseLodCorpses[Index] == Candidate)
        {
            return Index;
        }
    }
    return -1;
}

function RemoveAdaptiveCorpseLodEntry(int Index, bool bRestore)
{
    local KFPawn Candidate;

    if (Index < 0 || Index >= AdaptiveCorpseLodCorpses.Length ||
        Index >= AdaptiveCorpseLodOriginalMinModels.Length ||
        Index >= AdaptiveCorpseLodAppliedMinModels.Length)
    {
        return;
    }
    Candidate = AdaptiveCorpseLodCorpses[Index];
    if (bRestore && Candidate != None && !Candidate.bDeleteMe &&
        Candidate.Mesh != None &&
        Candidate.Mesh.MinLodModel == AdaptiveCorpseLodAppliedMinModels[Index])
    {
        Candidate.Mesh.MinLodModel =
            AdaptiveCorpseLodOriginalMinModels[Index];
        ++AdaptiveCorpseLodRestores;
    }
    AdaptiveCorpseLodCorpses.Remove(Index, 1);
    AdaptiveCorpseLodOriginalMinModels.Remove(Index, 1);
    AdaptiveCorpseLodAppliedMinModels.Remove(Index, 1);
}

function PruneAdaptiveCorpseLodEntries()
{
    local int Index;
    local KFPawn Candidate;

    for (Index = AdaptiveCorpseLodCorpses.Length - 1; Index >= 0; --Index)
    {
        Candidate = AdaptiveCorpseLodCorpses[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None)
        {
            RemoveAdaptiveCorpseLodEntry(Index, false);
        }
        else if (Candidate.Mesh.MinLodModel !=
                 AdaptiveCorpseLodAppliedMinModels[Index])
        {
            // A different system changed the value. Stop owning it instead of
            // overwriting an external decision during restore.
            RemoveAdaptiveCorpseLodEntry(Index, false);
        }
    }
}

function RestoreNearAdaptiveCorpseLods()
{
    local int Index;
    local float DistanceSquared;
    local KFPawn Candidate;
    local PlayerController LocalPC;

    LocalPC = GetALocalPlayerController();
    if (LocalPC == None || LocalPC.ViewTarget == None)
    {
        return;
    }
    for (Index = AdaptiveCorpseLodCorpses.Length - 1; Index >= 0; --Index)
    {
        Candidate = AdaptiveCorpseLodCorpses[Index];
        if (Candidate == None || Candidate.Mesh == None)
        {
            RemoveAdaptiveCorpseLodEntry(Index, false);
            continue;
        }
        DistanceSquared = VSizeSq(
            Candidate.Location - LocalPC.ViewTarget.Location);
        // Restore only after crossing the inner 250-unit boundary. LOD is
        // applied outside 300 units, leaving a stable hysteresis band.
        if (DistanceSquared < 62500.0)
        {
            RemoveAdaptiveCorpseLodEntry(Index, true);
        }
    }
}

function RestoreAllAdaptiveCorpseLods()
{
    local int Index;

    for (Index = AdaptiveCorpseLodCorpses.Length - 1; Index >= 0; --Index)
    {
        RemoveAdaptiveCorpseLodEntry(Index, true);
    }
}

function int FindAdaptiveDistanceSleptCorpse(KFPawn Candidate)
{
    local int Index;

    for (Index = 0; Index < AdaptiveDistanceSleptCorpses.Length; ++Index)
    {
        if (AdaptiveDistanceSleptCorpses[Index] == Candidate)
        {
            return Index;
        }
    }
    return -1;
}

function string GetAdaptiveCorpseActionId(KFPawn Candidate)
{
    if (Candidate == None)
    {
        return "none";
    }
    // Actor names are unique within the current world. TimeOfDeath keeps the
    // receipt unique even if KF2 later reuses a pooled pawn instance.
    return string(Candidate.Name)$":"$
        int(Candidate.TimeOfDeath * 1000.0);
}

function int GetAdaptiveCorpseRagdollSleepHash(string CorpseId)
{
    local int Index;
    local int HashValue;

    HashValue = 5381;
    for (Index = 0; Index < Len(CorpseId); ++Index)
    {
        HashValue = ((HashValue << 5) + HashValue) ^
            Asc(Mid(CorpseId, Index, 1));
    }
    return HashValue & 8191;
}

function bool EnsureAdaptiveCorpseRagdollSleepIds()
{
    if (AdaptiveCorpseRagdollSleepIds.Length == 0)
    {
        // This is four times the maximum supported live corpse pool. Keeping
        // only stable strings avoids retaining deleted Pawn objects while the
        // power-of-two table keeps the hot selector lookup bounded.
        AdaptiveCorpseRagdollSleepIds.Length = 8192;
    }
    return AdaptiveCorpseRagdollSleepIds.Length == 8192;
}

function int FindAdaptiveCorpseRagdollSleepId(string CorpseId)
{
    local int Probe;
    local int Slot;

    if (CorpseId == "" || AdaptiveCorpseRagdollSleepIds.Length != 8192)
    {
        return -1;
    }
    Slot = GetAdaptiveCorpseRagdollSleepHash(CorpseId);
    for (Probe = 0; Probe < 8192; ++Probe)
    {
        if (AdaptiveCorpseRagdollSleepIds[Slot] == CorpseId)
        {
            return Slot;
        }
        if (AdaptiveCorpseRagdollSleepIds[Slot] == "")
        {
            return -1;
        }
        Slot = (Slot + 1) & 8191;
    }
    return -1;
}

function bool RegisterAdaptiveCorpseRagdollSleep(KFPawn Candidate)
{
    local int Probe;
    local int Slot;
    local string CorpseId;

    if (Candidate == None || !EnsureAdaptiveCorpseRagdollSleepIds())
    {
        return false;
    }
    CorpseId = GetAdaptiveCorpseActionId(Candidate);
    Slot = GetAdaptiveCorpseRagdollSleepHash(CorpseId);
    for (Probe = 0; Probe < 8192; ++Probe)
    {
        if (AdaptiveCorpseRagdollSleepIds[Slot] == CorpseId)
        {
            return true;
        }
        if (AdaptiveCorpseRagdollSleepIds[Slot] == "")
        {
            AdaptiveCorpseRagdollSleepIds[Slot] = CorpseId;
            ++AdaptiveCorpseRagdollSleepIdCount;
            return true;
        }
        Slot = (Slot + 1) & 8191;
    }
    // Never evict an owned ID: eviction would make an old corpse eligible for
    // repeated work. A saturated table disables only further Ragdoll sleeps;
    // distance, LOD and capacity control continue independently.
    return false;
}

function int GetAdaptiveCorpseDistanceUnits(KFPawn Candidate)
{
    local PlayerController LocalPC;

    LocalPC = GetALocalPlayerController();
    if (Candidate == None || LocalPC == None || LocalPC.ViewTarget == None)
    {
        return -1;
    }
    return int(VSize(Candidate.Location - LocalPC.ViewTarget.Location));
}

function PruneAdaptiveCorpseDebugMarkers()
{
    local int Index;
    local KFPawn Candidate;

    for (Index = AdaptiveCorpseDebugMarkers.Length - 1; Index >= 0; --Index)
    {
        Candidate = AdaptiveCorpseDebugMarkers[Index].Corpse;
        if (Candidate == None || Candidate.bDeleteMe || WorldInfo == None ||
            WorldInfo.RealTimeSeconds >=
                AdaptiveCorpseDebugMarkers[Index].ExpiresRealTime ||
            GetAdaptiveCorpseActionId(Candidate) !=
                AdaptiveCorpseDebugMarkers[Index].CorpseId)
        {
            AdaptiveCorpseDebugMarkers.Remove(Index, 1);
        }
    }
}

function RegisterAdaptiveCorpseDebugMarker(
    KFPawn Candidate, string Action)
{
    local int Index;
    local string CorpseId;

    if (!bAdaptiveCorpseDebugMarkers || Candidate == None || WorldInfo == None)
    {
        return;
    }
    PruneAdaptiveCorpseDebugMarkers();
    CorpseId = GetAdaptiveCorpseActionId(Candidate);
    for (Index = 0; Index < AdaptiveCorpseDebugMarkers.Length; ++Index)
    {
        if (AdaptiveCorpseDebugMarkers[Index].Corpse == Candidate &&
            AdaptiveCorpseDebugMarkers[Index].CorpseId == CorpseId)
        {
            AdaptiveCorpseDebugMarkers[Index].Action = Action;
            AdaptiveCorpseDebugMarkers[Index].ExpiresRealTime =
                WorldInfo.RealTimeSeconds + 10.0;
            return;
        }
    }
    if (AdaptiveCorpseDebugMarkers.Length >= 24)
    {
        AdaptiveCorpseDebugMarkers.Remove(0, 1);
    }
    Index = AdaptiveCorpseDebugMarkers.Length;
    AdaptiveCorpseDebugMarkers.Length = Index + 1;
    AdaptiveCorpseDebugMarkers[Index].Corpse = Candidate;
    AdaptiveCorpseDebugMarkers[Index].CorpseId = CorpseId;
    AdaptiveCorpseDebugMarkers[Index].Action = Action;
    AdaptiveCorpseDebugMarkers[Index].ExpiresRealTime =
        WorldInfo.RealTimeSeconds + 10.0;
}

function DrawAdaptiveCorpseDebugMarkers(Canvas MarkerCanvas)
{
    local int Index;
    local vector MarkerLocation;
    local vector ScreenPosition;
    local vector ViewLocation;
    local rotator ViewRotation;
    local KFPawn Candidate;
    local PlayerController LocalPC;

    if (!bAdaptiveCorpseDebugMarkers || !bAdaptiveCorpseStagger ||
        MarkerCanvas == None || WorldInfo == None ||
        WorldInfo.NetMode != NM_Standalone)
    {
        return;
    }
    LocalPC = GetALocalPlayerController();
    if (LocalPC == None || LocalPC.ViewTarget == None)
    {
        return;
    }
    PruneAdaptiveCorpseDebugMarkers();
    LocalPC.GetPlayerViewPoint(ViewLocation, ViewRotation);
    for (Index = 0; Index < AdaptiveCorpseDebugMarkers.Length; ++Index)
    {
        Candidate = AdaptiveCorpseDebugMarkers[Index].Corpse;
        if (Candidate == None || Candidate.Mesh == None ||
            Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.2 ||
            !LocalPC.FastTrace(Candidate.Location, ViewLocation))
        {
            continue;
        }
        MarkerLocation = Candidate.Location;
        MarkerLocation.Z += 72.0;
        ScreenPosition = MarkerCanvas.Project(MarkerLocation);
        if (ScreenPosition.X < 8.0 || ScreenPosition.X > MarkerCanvas.ClipX - 8.0 ||
            ScreenPosition.Y < 8.0 || ScreenPosition.Y > MarkerCanvas.ClipY - 8.0)
        {
            continue;
        }
        if (AdaptiveCorpseDebugMarkers[Index].Action == "WAKE")
        {
            MarkerCanvas.SetDrawColor(80, 255, 120, 255);
        }
        else if (AdaptiveCorpseDebugMarkers[Index].Action == "DIST_SLEEP")
        {
            MarkerCanvas.SetDrawColor(80, 200, 255, 255);
        }
        else
        {
            MarkerCanvas.SetDrawColor(255, 170, 60, 255);
        }
        MarkerCanvas.SetPos(ScreenPosition.X - 90.0, ScreenPosition.Y - 18.0);
        MarkerCanvas.DrawText(
            "KF2OPT "$AdaptiveCorpseDebugMarkers[Index].Action$" "$
            AdaptiveCorpseDebugMarkers[Index].CorpseId, false, 0.8, 0.8);
    }
}

function RemoveAdaptiveDistanceSleptCorpseEntry(int Index)
{
    if (Index >= 0 && Index < AdaptiveDistanceSleptCorpses.Length)
    {
        AdaptiveDistanceSleptCorpses.Remove(Index, 1);
    }
}

function PruneAdaptiveDistanceSleptCorpses()
{
    local int Index;
    local KFPawn Candidate;

    for (Index = AdaptiveDistanceSleptCorpses.Length - 1;
         Index >= 0; --Index)
    {
        Candidate = AdaptiveDistanceSleptCorpses[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
            Candidate.TimeOfDeath <= 0.0 ||
            Candidate.Physics != PHYS_RigidBody)
        {
            RemoveAdaptiveDistanceSleptCorpseEntry(Index);
        }
    }
}

function int WakeNearAdaptiveDistanceSleptCorpses()
{
    local int Index;
    local int WakeCount;
    local float DistanceSquared;
    local bool bWasSleeping;
    local KFPawn Candidate;
    local PlayerController LocalPC;

    LocalPC = GetALocalPlayerController();
    if (LocalPC == None || LocalPC.ViewTarget == None)
    {
        return 0;
    }
    PruneAdaptiveDistanceSleptCorpses();
    for (Index = AdaptiveDistanceSleptCorpses.Length - 1;
         Index >= 0; --Index)
    {
        Candidate = AdaptiveDistanceSleptCorpses[Index];
        DistanceSquared = VSizeSq(
            Candidate.Location - LocalPC.ViewTarget.Location);
        // Wake every tracked corpse inside the 800-unit interaction radius
        // in the same control pass. This has no corpse-count limit, but
        // avoids reviving physics merely because a corpse is at mid distance.
        if (DistanceSquared >= 640000.0)
        {
            continue;
        }
        bWasSleeping = !Candidate.Mesh.RigidBodyIsAwake();
        if (bWasSleeping)
        {
            Candidate.Mesh.WakeRigidBody();
            if (!Candidate.Mesh.RigidBodyIsAwake())
            {
                continue;
            }
            Candidate.Mesh.bNoSkeletonUpdate = false;
            ++AdaptiveDistancePhysicsWakes;
            ++WakeCount;
            RegisterAdaptiveCorpseDebugMarker(Candidate, "WAKE");
        }
        RemoveAdaptiveDistanceSleptCorpseEntry(Index);
        if (bWasSleeping)
        {
            `log("KF2OPT_CORPSE_DISTANCE state=wake woken="$
                 AdaptiveDistancePhysicsWakes$" tracked="$
                 AdaptiveDistanceSleptCorpses.Length$" corpse_id="$
                 GetAdaptiveCorpseActionId(Candidate)$" distance_units="$
                 GetAdaptiveCorpseDistanceUnits(Candidate)$
                 " effective_awake=1");
        }
    }
    return WakeCount;
}

function KFPawn SelectDistantAwakeMonsterCorpseForSleep(
    KFGoreManager GoreManager, int PhysicsPressureLevel)
{
    local int Index;
    local float DistanceSquared;
    local float MinimumAge;
    local float MinimumDistanceSquared;
    local float MaximumSpeedSquared;
    local float Score;
    local float SelectedScore;
    local bool bRecentlyRendered;
    local KFPawn Candidate;
    local KFPawn Selected;
    local PlayerController LocalPC;

    LocalPC = GetALocalPlayerController();
    if (LocalPC == None || LocalPC.ViewTarget == None)
    {
        return None;
    }
    // Distance is the primary gate. Scene or confirmed frame pressure only
    // moves the thresholds inward and permits settled, still-visible bodies.
    MinimumAge = 2.5;
    MinimumDistanceSquared = 1440000.0;
    MaximumSpeedSquared = 62500.0;
    if (PhysicsPressureLevel >= 2)
    {
        MinimumAge = 1.0;
        MinimumDistanceSquared = 722500.0;
        MaximumSpeedSquared = 250000.0;
    }
    else if (PhysicsPressureLevel >= 1)
    {
        MinimumAge = 1.75;
        MinimumDistanceSquared = 1000000.0;
        MaximumSpeedSquared = 122500.0;
    }
    for (Index = 0; Index < GoreManager.CorpsePool.Length; ++Index)
    {
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
            Candidate.TimeOfDeath <= 0.0 ||
            WorldInfo.TimeSeconds - Candidate.TimeOfDeath < MinimumAge ||
            Candidate.Physics != PHYS_RigidBody ||
            Candidate.SpecialMove == SM_DeathAnim ||
            !Candidate.Mesh.RigidBodyIsAwake() ||
            VSizeSq(Candidate.Velocity) > MaximumSpeedSquared ||
            FindAdaptiveDistanceSleptCorpse(Candidate) != -1)
        {
            continue;
        }
        DistanceSquared = VSizeSq(
            Candidate.Location - LocalPC.ViewTarget.Location);
        if (DistanceSquared < MinimumDistanceSquared)
        {
            continue;
        }
        bRecentlyRendered = Candidate.Mesh.LastRenderTime >
            WorldInfo.TimeSeconds - 0.3;
        if (PhysicsPressureLevel <= 0 && bRecentlyRendered)
        {
            continue;
        }
        // Stale LastRenderTime means the native renderer already found the
        // corpse offscreen or occluded. Prefer it without tracing or hiding.
        Score = DistanceSquared +
            (WorldInfo.TimeSeconds - Candidate.TimeOfDeath) * 1000.0;
        if (!bRecentlyRendered)
        {
            Score += 100000000.0;
        }
        if (Selected == None || Score > SelectedScore)
        {
            Selected = Candidate;
            SelectedScore = Score;
        }
    }
    return Selected;
}

function bool SleepOneDistantMonsterCorpse(
    KFGoreManager GoreManager, int PhysicsPressureLevel,
    int VisibleLivingZeds, int VisibleCorpses)
{
    local KFPawn Candidate;

    Candidate = SelectDistantAwakeMonsterCorpseForSleep(
        GoreManager, PhysicsPressureLevel);
    if (Candidate == None || Candidate.Mesh == None)
    {
        return false;
    }
    Candidate.Mesh.PutRigidBodyToSleep();
    if (Candidate.Mesh.RigidBodyIsAwake())
    {
        return false;
    }
    Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true;
    Candidate.Mesh.bNoSkeletonUpdate = true;
    AdaptiveDistanceSleptCorpses.AddItem(Candidate);
    ++AdaptiveDistancePhysicsSleeps;
    ++AdaptiveCorpsesSlept;
    RegisterAdaptiveCorpseDebugMarker(Candidate, "DIST_SLEEP");
    `log("KF2OPT_CORPSE_DISTANCE state=sleep physics_level="$
         PhysicsPressureLevel$" frame_level="$AdaptiveCorpsePressureLevel$
         " scene_level="$AdaptiveCorpseScenePressureLevel$
         " quality_steps="$GetAdaptiveCorpseAttackScale()$" slept="$
         AdaptiveDistancePhysicsSleeps$" tracked="$
         AdaptiveDistanceSleptCorpses.Length$" visible_living="$
         VisibleLivingZeds$" visible_corpses="$VisibleCorpses$
         " corpse_id="$GetAdaptiveCorpseActionId(Candidate)$
         " distance_units="$GetAdaptiveCorpseDistanceUnits(Candidate)$
         " effective_awake=0");
    return true;
}

function KFPawn SelectVisibleMonsterCorpseForLod(
    KFGoreManager GoreManager, int PressureLevel, out int TargetMinLod)
{
    local int Index;
    local int CandidateTarget;
    local int MaximumMinLod;
    local float DistanceSquared;
    local float Score;
    local float SelectedScore;
    local KFPawn Candidate;
    local KFPawn Selected;
    local PlayerController LocalPC;

    TargetMinLod = 0;
    LocalPC = GetALocalPlayerController();
    if (LocalPC == None || LocalPC.ViewTarget == None)
    {
        return None;
    }
    for (Index = 0; Index < GoreManager.CorpsePool.Length; ++Index)
    {
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
            Candidate.Mesh.SkeletalMesh == None ||
            Candidate.Mesh.SkeletalMesh.LODInfo.Length < 2 ||
            Candidate.Mesh.ForcedLodModel != 0 ||
            Candidate.TimeOfDeath <= 0.0 ||
            WorldInfo.TimeSeconds - Candidate.TimeOfDeath < 0.75 ||
            Candidate.SpecialMove == SM_DeathAnim ||
            Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.3)
        {
            continue;
        }
        DistanceSquared = VSizeSq(
            Candidate.Location - LocalPC.ViewTarget.Location);
        // Enter corpse LOD outside 300 units. The separate 250-unit restore
        // boundary prevents repeated apply/restore work near the camera.
        if (DistanceSquared < 90000.0)
        {
            continue;
        }
        MaximumMinLod = Candidate.Mesh.SkeletalMesh.LODInfo.Length - 1;
        // Corpse-only rendering starts two LODs lower than the source mesh and
        // advances quickly with distance. The mesh's real final LOD remains
        // the hard cap below, so short LOD chains are always respected.
        CandidateTarget = 2;
        if (DistanceSquared >= 1440000.0)
        {
            CandidateTarget = 5;
        }
        else if (DistanceSquared >= 640000.0)
        {
            CandidateTarget = 4;
        }
        else if (DistanceSquared >= 250000.0)
        {
            CandidateTarget = 3;
        }
        CandidateTarget = Max(
            CandidateTarget, Candidate.Mesh.PredictedLODLevel);
        CandidateTarget += Clamp(PressureLevel, 0, 5);
        CandidateTarget = Min(CandidateTarget, MaximumMinLod);
        if (Candidate.Mesh.MinLodModel >= CandidateTarget)
        {
            continue;
        }
        Score = DistanceSquared +
            float(Candidate.Mesh.PredictedLODLevel) * 1000000.0 +
            (WorldInfo.TimeSeconds - Candidate.TimeOfDeath) * 1000.0;
        if (Selected == None || Score > SelectedScore)
        {
            Selected = Candidate;
            SelectedScore = Score;
            TargetMinLod = CandidateTarget;
        }
    }
    return Selected;
}

function bool ApplyOneAdaptiveCorpseLod(
    KFGoreManager GoreManager, int PressureLevel)
{
    local int EntryIndex;
    local int PreviousMinLod;
    local int TargetMinLod;
    local KFPawn Candidate;

    PruneAdaptiveCorpseLodEntries();
    Candidate = SelectVisibleMonsterCorpseForLod(
        GoreManager, PressureLevel, TargetMinLod);
    if (Candidate == None || Candidate.Mesh == None || TargetMinLod <= 0)
    {
        return false;
    }
    EntryIndex = FindAdaptiveCorpseLodEntry(Candidate);
    if (EntryIndex < 0)
    {
        EntryIndex = AdaptiveCorpseLodCorpses.Length;
        AdaptiveCorpseLodCorpses.AddItem(Candidate);
        AdaptiveCorpseLodOriginalMinModels.AddItem(
            Candidate.Mesh.MinLodModel);
        AdaptiveCorpseLodAppliedMinModels.AddItem(TargetMinLod);
    }
    else
    {
        AdaptiveCorpseLodAppliedMinModels[EntryIndex] = TargetMinLod;
    }
    PreviousMinLod = Candidate.Mesh.MinLodModel;
    Candidate.Mesh.MinLodModel = TargetMinLod;
    if (Candidate.Mesh.MinLodModel != TargetMinLod)
    {
        RemoveAdaptiveCorpseLodEntry(EntryIndex, false);
        return false;
    }
    ++AdaptiveCorpseLodReductions;
    RegisterAdaptiveCorpseDebugMarker(Candidate, "LOD_"$TargetMinLod);
    `log("KF2OPT_CORPSE_LOD state=applied pressure_level="$
         PressureLevel$" previous_lod="$PreviousMinLod$" target_lod="$
         TargetMinLod$" reduced="$AdaptiveCorpseLodReductions$" tracked="$
         AdaptiveCorpseLodCorpses.Length$" corpse_id="$
         GetAdaptiveCorpseActionId(Candidate)$" distance_units="$
         GetAdaptiveCorpseDistanceUnits(Candidate)$" readback=verified");
    return true;
}

function int GetAdaptiveCorpseFramePressureLevel(
    int AwakeLoad, int AwakeThreshold, int CorpseBurden)
{
    local float FrameMs;
    local float TargetFrameMs;
    local float WarningThresholdMs;
    local float CorrectiveThresholdMs;
    local float CriticalThresholdMs;

    FrameMs = FClamp(WorldInfo.DeltaSeconds * 1000.0, 4.0, 100.0);
    if (AdaptiveFrameTimeEmaMs <= 0.0)
    {
        AdaptiveFrameTimeEmaMs = FrameMs;
    }
    else
    {
        AdaptiveFrameTimeEmaMs =
            AdaptiveFrameTimeEmaMs * 0.85 + FrameMs * 0.15;
    }

    // Learn normal map performance only while corpse pressure is absent.
    // This makes the rule relative for 30, 60, 144 or faster systems.
    if (AwakeLoad < AwakeThreshold && !WorldInfo.bDropDetail)
    {
        if (AdaptiveFrameBaselineMs <= 0.0)
        {
            AdaptiveFrameBaselineMs = AdaptiveFrameTimeEmaMs;
        }
        else if (AdaptiveFrameBaselineSamples < 16)
        {
            AdaptiveFrameBaselineMs = AdaptiveFrameBaselineMs * 0.8 +
                AdaptiveFrameTimeEmaMs * 0.2;
        }
        else if (AdaptiveFrameTimeEmaMs <
                 AdaptiveFrameBaselineMs * 1.1)
        {
            AdaptiveFrameBaselineMs = AdaptiveFrameBaselineMs * 0.98 +
                AdaptiveFrameTimeEmaMs * 0.02;
        }
        AdaptiveFrameBaselineSamples =
            Min(1000000, AdaptiveFrameBaselineSamples + 1);
    }

    // The target, not a historical map baseline, owns the canonical bands.
    // 60/59 is WATCH, 60/58 is the first corrective level, and 60/57 is
    // critical. Sleeping/visible mix affects which corpse action is selected,
    // but does not postpone a useful runtime-budget reduction.
    // Corpse, physics and LOD work is subordinate to the app governor. Local
    // frame timing confirms that pressure is still current; the authenticated
    // resource command proves CPU/GPU/VRAM/RAM attribution first.
    if (!HasConfirmedAdaptivePerformancePressure())
    {
        return 0;
    }
    if (AdaptiveTargetFPS >= 30 && AdaptiveTargetFPS <= 240)
    {
        TargetFrameMs = 1000.0 / float(AdaptiveTargetFPS);
        WarningThresholdMs = TargetFrameMs * 60.0 / 59.0;
        CorrectiveThresholdMs = TargetFrameMs * 60.0 / 58.0;
        CriticalThresholdMs = TargetFrameMs * 60.0 / 57.0;
        if (CorpseBurden <= 4)
        {
            return 0;
        }
        if (AdaptiveFrameTimeEmaMs >= CriticalThresholdMs ||
            (WorldInfo.bDropDetail &&
             AdaptiveFrameTimeEmaMs >= CorrectiveThresholdMs))
        {
            return 2;
        }
        if (AdaptiveFrameTimeEmaMs >= CorrectiveThresholdMs)
        {
            return 1;
        }
        // Crossing the warning band is intentionally observable but does not
        // spend quality before the corrective guard is confirmed.
        if (AdaptiveFrameTimeEmaMs >= WarningThresholdMs)
        {
            return 0;
        }
        return 0;
    }

    // Invalid/missing target configuration fails closed. This package is
    // installed only through the validated protected-session transaction.
    return 0;
    /* Legacy fallback deliberately unreachable after target validation:
    if (AwakeLoad >= AwakeThreshold && WorldInfo.bDropDetail)
    {
        return 1;
    }
    return 0; */
}

function KFPawn SelectVisibleAwakeMonsterCorpseForSleep(
    KFGoreManager GoreManager, bool SeverePressure)
{
    local int Index;
    local float MinimumAge;
    local float MaximumSpeedSquared;
    local float SelectedDeathTime;
    local KFPawn Candidate;
    local KFPawn Selected;

    MinimumAge = SeverePressure ? 0.75 : 1.5;
    MaximumSpeedSquared = SeverePressure ? 360000.0 : 160000.0;
    SelectedDeathTime = WorldInfo.TimeSeconds + 1.0;
    if (!EnsureAdaptiveCorpseRagdollSleepIds() ||
        AdaptiveCorpseRagdollSleepIdCount >= 8192)
    {
        return None;
    }
    for (Index = 0; Index < GoreManager.CorpsePool.Length; ++Index)
    {
        Candidate = GoreManager.CorpsePool[Index];
        if (Candidate == None || Candidate.bDeleteMe ||
            KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
            Candidate.TimeOfDeath <= 0.0 ||
            WorldInfo.TimeSeconds - Candidate.TimeOfDeath < MinimumAge ||
            Candidate.Physics != PHYS_RigidBody ||
            Candidate.SpecialMove == SM_DeathAnim ||
            FindAdaptiveDistanceSleptCorpse(Candidate) != -1 ||
            FindAdaptiveCorpseRagdollSleepId(
                GetAdaptiveCorpseActionId(Candidate)) != -1 ||
            Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.3 ||
            !Candidate.Mesh.RigidBodyIsAwake() ||
            VSizeSq(Candidate.Velocity) > MaximumSpeedSquared)
        {
            continue;
        }
        if (Selected == None || Candidate.TimeOfDeath < SelectedDeathTime)
        {
            Selected = Candidate;
            SelectedDeathTime = Candidate.TimeOfDeath;
        }
    }
    return Selected;
}

function bool SleepOneVisibleMonsterCorpse(
    KFGoreManager GoreManager, bool SeverePressure, int VisibleAwakeBefore)
{
    local KFPawn Candidate;

    Candidate = SelectVisibleAwakeMonsterCorpseForSleep(
        GoreManager, SeverePressure);
    if (Candidate == None || Candidate.Mesh == None)
    {
        return false;
    }
    Candidate.Mesh.PutRigidBodyToSleep();
    if (Candidate.Mesh.RigidBodyIsAwake())
    {
        return false;
    }
    Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true;
    Candidate.Mesh.bNoSkeletonUpdate = true;
    if (!RegisterAdaptiveCorpseRagdollSleep(Candidate))
    {
        `log("KF2OPT_CORPSE_RAGDOLL state=tracking_full capacity=8192");
        return false;
    }
    ++AdaptiveCorpsesSlept;
    ++AdaptiveVisibleRagdollSleeps;
    RegisterAdaptiveCorpseDebugMarker(Candidate, "RAGDOLL_SLEEP");
    `log("KF2OPT_CORPSE_RAGDOLL state=sleep level="$
         AdaptiveCorpsePressureLevel$" quality_steps="$
         GetAdaptiveCorpseAttackScale()$" slept="$
         AdaptiveVisibleRagdollSleeps$" visible_awake_before="$
         VisibleAwakeBefore$" distance_tracked="$
         AdaptiveDistanceSleptCorpses.Length$" ownership_tracked="$
         AdaptiveCorpseRagdollSleepIdCount$" corpse_id="$
         GetAdaptiveCorpseActionId(Candidate)$" distance_units="$
         GetAdaptiveCorpseDistanceUnits(Candidate)$" effective_awake=0");
    return true;
}

function AdaptiveCorpseLoadControl()
{
    local int AttackScale;
    local int AwakeTotal;
    local int AwakeThreshold;
    local int VisibleCorpses;
    local int VisibleAwake;
    local int VisibleLivingZeds;
    local int VisibleThreshold;
    local int DesiredVisibleAwake;
    local int DesiredPressureLevel;
    local int CurrentPressureLevel;
    local int ScenePressureLevel;
    local int EnemyPressureLevel;
    local float EnemyPressureScale;
    local int PhysicsPressureLevel;
    local int RagdollPressureLevel;
    local bool bLivingVisibilityFresh;
    local float ActionInterval;
    local float DistanceActionInterval;
    local float LodActionInterval;
    local KFGoreManager GoreManager;
    local KFGameInfo GameInfo;

    if (!bAdaptiveCorpseStagger || WorldInfo == None ||
        WorldInfo.NetMode != NM_Standalone)
    {
        return;
    }
    GoreManager = KFGoreManager(WorldInfo.MyGoreEffectManager);
    if (GoreManager == None)
    {
        return;
    }
    if (!bAdaptiveCorpseStaggerInitialized ||
        AdaptiveCorpseManager != GoreManager)
    {
        InitializeAdaptiveCorpseStagger(GoreManager);
        return;
    }
    GameInfo = KFGameInfo(WorldInfo.Game);
    if (GameInfo != None && GameInfo.IsZedTimeActive())
    {
        return;
    }

    SleepBaselineAwakeMonsterCorpses(GoreManager);
    PruneAdaptiveDistanceSleptCorpses();
    AttackScale = GetAdaptiveCorpseAttackScale();
    // Proximity is gameplay-critical: wake every matching tracked corpse now,
    // independently of quality level or the number of nearby bodies.
    WakeNearAdaptiveDistanceSleptCorpses();
    PruneAdaptiveCorpseLodEntries();
    RestoreNearAdaptiveCorpseLods();
    RefreshSleepingCorpseAnimationState(GoreManager);
    VisibleAwake = CountVisibleAwakeMonsterCorpses(
        GoreManager, VisibleCorpses);
    AwakeTotal = CountAwakeMonsterCorpses(GoreManager);
    VisibleThreshold = Clamp((AdaptiveCorpseTarget + 3) / 4, 3, 8);
    AwakeThreshold = VisibleThreshold;
    bLivingVisibilityFresh = AdaptiveVisibleLivingObservedRealTime > 0.0 &&
        WorldInfo.RealTimeSeconds - AdaptiveVisibleLivingObservedRealTime <= 1.5;
    VisibleLivingZeds = AdaptiveVisibleLivingZeds;
    if (!bLivingVisibilityFresh)
    {
        VisibleLivingZeds = -1;
    }
    ScenePressureLevel = GetAdaptiveCorpseScenePressureLevel(
        VisibleLivingZeds, bLivingVisibilityFresh, VisibleCorpses,
        VisibleAwake, AwakeTotal, VisibleThreshold);
    EnemyPressureScale = GetAdaptiveLivingEnemyPressureScale(
        VisibleLivingZeds, bLivingVisibilityFresh);
    EnemyPressureLevel = GetAdaptiveLivingEnemyPressureLevel(
        EnemyPressureScale);
    ApplyLivingEnemyVisualPressure(EnemyPressureLevel, EnemyPressureScale);
    if (ScenePressureLevel != AdaptiveCorpseScenePressureLevel)
    {
        `log("KF2OPT_CORPSE_SCENE state=changed level="$
             ScenePressureLevel$" previous="$AdaptiveCorpseScenePressureLevel$
             " visible_living="$VisibleLivingZeds$" visible_corpses="$
             VisibleCorpses$" visible_awake="$VisibleAwake$" awake_total="$
             AwakeTotal);
        AdaptiveCorpseScenePressureLevel = ScenePressureLevel;
    }
    CurrentPressureLevel = GetAdaptiveCorpseFramePressureLevel(
        AwakeTotal, AwakeThreshold, GoreManager.CorpsePool.Length);
    AdaptiveCorpseCurrentFramePressureLevel = CurrentPressureLevel;
    AdaptiveFramePressureObservedRealTime = WorldInfo.RealTimeSeconds;
    if (CurrentPressureLevel <= 0)
    {
        AdaptiveCorpsePressureSamples =
            Max(0, AdaptiveCorpsePressureSamples - 1);
        AdaptiveCorpseRecoverySamples =
            Min(1000, AdaptiveCorpseRecoverySamples + 1);
        if (AdaptiveCorpsePressureLevel > 0 &&
            AdaptiveCorpseRecoverySamples >= 8)
        {
            `log("KF2OPT_CORPSE_LOAD state=recovered slept="$
                 AdaptiveCorpsesSlept$" frame_ms="$
                 int(AdaptiveFrameTimeEmaMs));
            AdaptiveCorpsePressureLevel = 0;
            AdaptiveCorpsePressureSamples = 0;
        }
        AdjustAdaptiveCorpseCapacity(GoreManager, 0);
    }
    else
    {
        AdaptiveCorpseRecoverySamples = 0;
        AdaptiveCorpsePressureSamples =
            Min(12, AdaptiveCorpsePressureSamples + 1);
        DesiredPressureLevel = 0;
        if (AdaptiveCorpsePressureSamples >= 3)
        {
            DesiredPressureLevel = 1;
        }
        if (CurrentPressureLevel >= 2 && AdaptiveCorpsePressureSamples >= 4)
        {
            DesiredPressureLevel = 2;
        }
        if (DesiredPressureLevel > AdaptiveCorpsePressureLevel)
        {
            AdaptiveCorpsePressureLevel = DesiredPressureLevel;
            `log("KF2OPT_CORPSE_LOAD state=pressure level="$
                 AdaptiveCorpsePressureLevel$" visible_awake="$VisibleAwake$
                 " awake_total="$AwakeTotal$" threshold="$AwakeThreshold$
                 " frame_ms="$
                 int(AdaptiveFrameTimeEmaMs)$" baseline_ms="$
                 int(AdaptiveFrameBaselineMs)$" resource="$
                 AdaptiveGraphicsResource$" quality="$
                 AdaptiveGraphicsQuality);
        }
        if (AdaptiveCorpsePressureLevel > 0)
        {
            AdjustAdaptiveCorpseCapacity(
                GoreManager, AdaptiveCorpsePressureLevel);
        }
    }

    // Distance and visible density are safe preventive gates. Authenticated
    // frame/resource pressure makes them stronger but is not required before
    // far, settled corpse work can be reduced.
    PhysicsPressureLevel = Max(
        Max(AdaptiveCorpsePressureLevel, ScenePressureLevel),
        EnemyPressureLevel);
    DistanceActionInterval = PhysicsPressureLevel > 0 ?
        FMax(0.05, 0.20 / float(PhysicsPressureLevel)) : 0.40;
    if (PhysicsPressureLevel > 0)
    {
        DistanceActionInterval = FMax(
            0.05, DistanceActionInterval / float(AttackScale));
    }
    if (WorldInfo.RealTimeSeconds - AdaptiveLastDistancePhysicsRealTime >=
            DistanceActionInterval &&
        SleepOneDistantMonsterCorpse(
            GoreManager, PhysicsPressureLevel,
            VisibleLivingZeds, VisibleCorpses))
    {
        AdaptiveLastDistancePhysicsRealTime = WorldInfo.RealTimeSeconds;
        return;
    }

    // Render LOD is distance-first and remains useful for sleeping corpses.
    // Every mesh uses as many progressive LOD stages as it actually exposes.
    LodActionInterval = PhysicsPressureLevel > 0 ?
        FMax(0.05, 0.20 / float(PhysicsPressureLevel)) : 0.40;
    LodActionInterval = FMax(
        0.05, LodActionInterval / float(AttackScale));
    if (WorldInfo.RealTimeSeconds - AdaptiveLastCorpseLodRealTime >=
            LodActionInterval &&
        ApplyOneAdaptiveCorpseLod(GoreManager, PhysicsPressureLevel))
    {
        AdaptiveLastCorpseLodRealTime = WorldInfo.RealTimeSeconds;
        return;
    }

    RagdollPressureLevel = Max(
        Max(AdaptiveCorpsePressureLevel, ScenePressureLevel),
        EnemyPressureLevel);
    // Start at low visible density and treat six moving corpses as medium to
    // severe density. Two remain awake for nearby visual response.
    if (VisibleAwake >= 6)
    {
        RagdollPressureLevel = Max(RagdollPressureLevel, 2);
    }
    else if (VisibleAwake >= 3)
    {
        RagdollPressureLevel = Max(RagdollPressureLevel, 1);
    }
    DesiredVisibleAwake = 2;
    if (RagdollPressureLevel <= 0 || VisibleAwake <= DesiredVisibleAwake)
    {
        return;
    }
    ActionInterval = RagdollPressureLevel >= 2 ? 0.15 : 0.30;
    ActionInterval = FMax(
        0.075, ActionInterval / float(AttackScale));
    if (WorldInfo.RealTimeSeconds - AdaptiveLastCorpseSleepRealTime <
        ActionInterval)
    {
        return;
    }
    if (SleepOneVisibleMonsterCorpse(
            GoreManager, RagdollPressureLevel >= 2, VisibleAwake))
    {
        AdaptiveLastCorpseSleepRealTime = WorldInfo.RealTimeSeconds;
    }
}

function int CountBits(int Value)
{
    local int Count;
    while (Value != 0)
    {
        Value = Value & (Value - 1);
        ++Count;
    }
    return Count;
}

function ClassifyParticleComponent(
    ParticleSystemComponent ParticleComponent,
    out int FlexComponentCount,
    out int FlexFluidComponentCount,
    out int FlexNonFluidComponentCount,
    out int FlexMixedComponentCount,
    out int NonFlexComponentCount,
    out int UnclassifiedComponentCount)
{
    local int Index;
    local ParticleEmitter EmitterTemplate;
    local bool HasFlexFluid;
    local bool HasFlexNonFluid;

    if (ParticleComponent == None || ParticleComponent.Template == None)
    {
        ++UnclassifiedComponentCount;
        return;
    }
    for (Index = 0;
         Index < ParticleComponent.Template.Emitters.Length;
         ++Index)
    {
        EmitterTemplate = ParticleComponent.Template.Emitters[Index];
        if (EmitterTemplate == None ||
            EmitterTemplate.FlexContainerTemplate == None)
        {
            continue;
        }
        if (EmitterTemplate.FlexContainerTemplate.bFluid)
        {
            HasFlexFluid = true;
        }
        else
        {
            HasFlexNonFluid = true;
        }
    }
    if (!HasFlexFluid && !HasFlexNonFluid)
    {
        ++NonFlexComponentCount;
        return;
    }
    ++FlexComponentCount;
    if (HasFlexFluid && HasFlexNonFluid)
    {
        ++FlexMixedComponentCount;
    }
    else if (HasFlexFluid)
    {
        ++FlexFluidComponentCount;
    }
    else
    {
        ++FlexNonFluidComponentCount;
    }
}

function CountParticleSpawnEnvelope(
    ParticleSystemComponent ParticleComponent,
    out int ConstantSpawnEmitters,
    out int DynamicSpawnEmitters,
    out int ConstantSpawnRateMilli,
    out int BurstEntries,
    out int PeakParticleCapacity)
{
    local int Index;
    local int LODIndex;
    local int AddedRateMilli;
    local float ConstantRatePerSecond;
    local ParticleEmitter EmitterTemplate;
    local ParticleLODLevel LODTemplate;
    local DistributionFloatConstant ConstantRate;
    local DistributionFloatConstant ConstantScale;

    if (ParticleComponent == None || ParticleComponent.Template == None)
    {
        return;
    }
    for (Index = 0;
         Index < ParticleComponent.Template.Emitters.Length;
         ++Index)
    {
        EmitterTemplate = ParticleComponent.Template.Emitters[Index];
        if (EmitterTemplate == None || EmitterTemplate.LODLevels.Length == 0)
        {
            ++DynamicSpawnEmitters;
            continue;
        }
        LODIndex = Clamp(ParticleComponent.GetLODLevel(), 0,
                         EmitterTemplate.LODLevels.Length - 1);
        LODTemplate = EmitterTemplate.LODLevels[LODIndex];
        if (LODTemplate == None || LODTemplate.SpawnModule == None)
        {
            ++DynamicSpawnEmitters;
            continue;
        }
        BurstEntries += Min(1000000000 - BurstEntries,
            LODTemplate.SpawnModule.BurstList.Length);
        PeakParticleCapacity += Min(1000000000 - PeakParticleCapacity,
            Max(0, LODTemplate.PeakActiveParticles));
        ConstantRate = DistributionFloatConstant(
            LODTemplate.SpawnModule.Rate.Distribution);
        ConstantScale = DistributionFloatConstant(
            LODTemplate.SpawnModule.RateScale.Distribution);
        if (ConstantRate == None || ConstantScale == None ||
            ConstantRate.Constant < 0.0 || ConstantRate.Constant > 1000000.0 ||
            ConstantScale.Constant < 0.0 || ConstantScale.Constant > 1000000.0)
        {
            ++DynamicSpawnEmitters;
            continue;
        }
        ConstantRatePerSecond =
            ConstantRate.Constant * ConstantScale.Constant;
        if (ConstantRatePerSecond > 1000000.0)
        {
            ++DynamicSpawnEmitters;
            continue;
        }
        AddedRateMilli = int(ConstantRatePerSecond * 1000.0);
        ConstantSpawnRateMilli += Min(
            1000000000 - ConstantSpawnRateMilli, AddedRateMilli);
        ++ConstantSpawnEmitters;
    }
}

function CountParticlePool(
    EmitterPool ParticlePool,
    out int ComponentCount,
    out int ParticleCount,
    out int VisibleComponentCount,
    out int LODLevelTotal,
    out int BoundedComponentCount,
    out int FlexComponentCount,
    out int FlexFluidComponentCount,
    out int FlexNonFluidComponentCount,
    out int FlexMixedComponentCount,
    out int NonFlexComponentCount,
    out int UnclassifiedComponentCount,
    out int ConstantSpawnEmitters,
    out int DynamicSpawnEmitters,
    out int ConstantSpawnRateMilli,
    out int BurstEntries,
    out int PeakParticleCapacity)
{
    local int Index;
    local ParticleSystemComponent ParticleComponent;

    if (ParticlePool == None)
    {
        return;
    }
    for (Index = 0; Index < ParticlePool.ActiveComponents.Length; ++Index)
    {
        ParticleComponent = ParticlePool.ActiveComponents[Index];
        if (ParticleComponent == None)
        {
            continue;
        }
        ++ComponentCount;
        ParticleCount += ParticleComponent.NumActiveParticles;
        ClassifyParticleComponent(
            ParticleComponent, FlexComponentCount,
            FlexFluidComponentCount, FlexNonFluidComponentCount,
            FlexMixedComponentCount, NonFlexComponentCount,
            UnclassifiedComponentCount);
        CountParticleSpawnEnvelope(
            ParticleComponent, ConstantSpawnEmitters, DynamicSpawnEmitters,
            ConstantSpawnRateMilli, BurstEntries, PeakParticleCapacity);
        LODLevelTotal += ParticleComponent.GetLODLevel();
        if (ParticleComponent.LastRenderTime > WorldInfo.TimeSeconds - 0.3)
        {
            ++VisibleComponentCount;
        }
        if (ParticleComponent.Bounds.SphereRadius > 0.0)
        {
            ++BoundedComponentCount;
        }
    }
}

event PreBeginPlay()
{
    Super.PreBeginPlay();

    AdaptiveGraphicsQuality = 100;
    AdaptiveGraphicsResource = "recover";

    if (WorldInfo == None || WorldInfo.NetMode != NM_Standalone)
    {
        `log("KF2OPT_TELEMETRY schema=6 state=blocked reason=not_standalone");
        Destroy();
        return;
    }

    `log("KF2OPT_TELEMETRY schema=6 state=started net=standalone");
    SetTimer(1.0, true, nameof(SampleTelemetry), self);
    if (bAdaptiveCorpseStagger)
    {
        SetTimer(0.45, true, nameof(StaggerCorpseCleanup), self);
        SetTimer(0.25, true, nameof(AdaptiveCorpseLoadControl), self);
    }
    SampleTelemetry();
}

function SampleTelemetry()
{
    local KFPawn_Monster Zed;
    local array<class<KFPawn_Monster> > LivingClasses;
    local KFPawn Corpse;
    local KFPawn CollisionProbeCorpse;
    local KFGiblet Gib;
    local KFGoreManager GoreManager;
    local KFGameInfo GameInfo;
    local KFImpactEffectManager ImpactEffectManager;
    local KFSprayActor SprayActor;
    local KFExplosionActor ExplosionActor;
    local KFProj_HansSmokeGrenade SmokeGrenadeProjectile;
    local KFProj_BloatPukeMine PukeMineProjectile;
    local Emitter WorldEmitter;
    local int Index;
    local int HitZoneIndex;
    local int LivingZeds;
    local int LivingBosses;
    local int LivingRecentlyRendered;
    local int LivingOffscreen;
    local int LivingLodTotal;
    local int LivingAnimationLodTotal;
    local int LivingInjuredZones;
    local int LivingRequiredBones;
    local int LivingMaterialSlots;
    local int LivingAttachments;
    local int LivingAnimSkipped;
    local int LivingBoneAtomsSkipped;
    local int LivingBoneInterpolation;
    local int LivingKinematicDistanceSkipped;
    local int LivingTicksOffscreen;
    local int LivingSpecialMoves;
    local int LivingAttackMoves;
    local int LivingGrappleMoves;
    local int LivingStumbles;
    local int LivingKnockdowns;
    local int LivingHitReactions;
    local int LivingOtherSpecialMoves;
    local int CorpseTotal;
    local int CorpseAwake;
    local int CorpseSleeping;
    local int CorpseOther;
    local int CorpseFinalPose;
    local int CorpseRecentlyRendered;
    local int CorpseOffscreen;
    local int CorpseLodTotal;
    local int CorpseInjuredZones;
    local int MaxCorpseAgeMs;
    local int RuntimeCorpseLimit;
    local int RuntimeCorpseOffscreenTimeMs;
    local int RuntimeCorpseOffscreenDistance;
    local int DismemberedCorpses;
    local int DismemberedLimbs;
    local int RagdollWarnedCorpses;
    local int MaximumRagdollWarningLevel;
    local int RuntimeCorpseCollideDead;
    local int RuntimeCorpseCollideLiving;
    local int RuntimeCorpseCollideDeadAfterSleep;
    local int RuntimeCorpseCollideLivingAfterSleep;
    local int VisibleGibs;
    local int ZedTimeActive;
    local int SprayActors;
    local int FireSprayActors;
    local int ToxicSprayActors;
    local int OtherSprayActors;
    local int ExplosionActors;
    local int DamagingExplosionActors;
    local int FireExplosionActors;
    local int ToxicExplosionActors;
    local int OtherDamagingExplosionActors;
    local int UnclassifiedExplosionActors;
    local int LingeringExplosionActors;
    local int SmokeExplosionActors;
    local int BloatKingFartExplosionActors;
    local int SmokeGrenadeProjectiles;
    local int PukeMineProjectiles;
    local int BloatKingPukeMineProjectiles;
    local int BodyWoundDecals;
    local int BloodSplatterDecals;
    local int BloodPoolDecals;
    local int ImpactDecals;
    local int ExplosionDecals;
    local int RuntimeWoundDecalLimit;
    local int RuntimeSplatterDecalLimit;
    local int RuntimePoolDecalLimit;
    local int RuntimeImpactDecalLimit;
    local int RuntimeExplosionDecalLimit;
    local int RuntimeBloodEffectLimit;
    local int RuntimeGoreEffectLimit;
    local int RuntimeWoundLifetimeMs;
    local int RuntimeSplatterLifetimeMs;
    local int RuntimePoolLifetimeMs;
    local int RuntimeGibLifetimeMs;
    local int GoreParticleComponents;
    local int GoreParticles;
    local int GoreParticleVisibleComponents;
    local int GoreParticleLodTotal;
    local int GoreParticleBoundedComponents;
    local int WorldParticleComponents;
    local int WorldParticles;
    local int WorldParticleVisibleComponents;
    local int WorldParticleLodTotal;
    local int WorldParticleBoundedComponents;
    local int GroundFireParticleComponents;
    local int GroundFireParticles;
    local int GroundFireParticleVisibleComponents;
    local int GroundFireParticleLodTotal;
    local int GroundFireParticleBoundedComponents;
    local int ImpactParticleComponents;
    local int ImpactParticles;
    local int ImpactParticleVisibleComponents;
    local int ImpactParticleLodTotal;
    local int ImpactParticleBoundedComponents;
    local int GoreParticlePoolCapacity;
    local int WorldParticlePoolCapacity;
    local int GroundFireParticlePoolCapacity;
    local int ImpactParticlePoolCapacity;
    local int ParticleConstantSpawnEmitters;
    local int ParticleDynamicSpawnEmitters;
    local int ParticleConstantSpawnRateMilli;
    local int ParticleBurstEntries;
    local int ParticlePeakCapacity;
    local bool GoreParticlePoolUnbounded;
    local int FlexSurrogateActive;
    local int FlexSurrogateParticles;
    local int FlexSurrogateVisible;
    local int FlexSurrogateLod;
    local int ParticleFlexComponents;
    local int ParticleFlexFluidComponents;
    local int ParticleFlexNonFluidComponents;
    local int ParticleFlexMixedComponents;
    local int ParticleNonFlexComponents;
    local int ParticleUnclassifiedComponents;

    if (WorldInfo == None || WorldInfo.NetMode != NM_Standalone)
    {
        `log("KF2OPT_TELEMETRY schema=6 state=stopped reason=netmode_changed");
        Destroy();
        return;
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=sample_begin");

    foreach WorldInfo.AllPawns(class'KFPawn_Monster', Zed)
    {
        if (Zed != None && Zed.IsAliveAndWell())
        {
            ++LivingZeds;
            if (LivingClasses.Find(Zed.Class) == INDEX_None)
            {
                LivingClasses.AddItem(Zed.Class);
            }
            if (Zed.IsABoss()) ++LivingBosses;
            if (Zed.SpecialMove != SM_None)
            {
                ++LivingSpecialMoves;
                switch (Zed.SpecialMove)
                {
                    case SM_MeleeAttack:
                    case SM_MeleeAttackDoor:
                    case SM_SonicAttack:
                    case SM_StandAndShootAttack:
                    case SM_HoseWeaponAttack:
                    case SM_Suicide:
                    case SM_PlayerZedMove_LMB:
                    case SM_PlayerZedMove_RMB:
                    case SM_PlayerZedMove_V:
                    case SM_PlayerZedMove_MMB:
                    case SM_PlayerZedMove_Q:
                    case SM_PlayerZedMove_G:
                    case SM_Hans_ThrowGrenade:
                    case SM_Hans_GrenadeHalfBarrage:
                    case SM_Hans_GrenadeBarrage:
                        ++LivingAttackMoves;
                        break;
                    case SM_GrappleAttack:
                        ++LivingGrappleMoves;
                        break;
                    case SM_Stumble:
                        ++LivingStumbles;
                        break;
                    case SM_Knockdown:
                        ++LivingKnockdowns;
                        break;
                    case SM_RecoverFromRagdoll:
                    case SM_DeathAnim:
                    case SM_Stunned:
                    case SM_Frozen:
                    case SM_GorgeZedVictim:
                        ++LivingHitReactions;
                        break;
                    default:
                        ++LivingOtherSpecialMoves;
                        break;
                }
            }
            if (Zed.Mesh != None)
            {
                LivingLodTotal += Zed.Mesh.PredictedLODLevel;
                LivingAnimationLodTotal += Zed.Mesh.AnimationLODFrameRate;
                LivingRequiredBones += Zed.Mesh.RequiredBones.Length;
                LivingMaterialSlots += Zed.Mesh.Materials.Length;
                LivingAttachments += Zed.Mesh.Attachments.Length;
                if (Zed.Mesh.bSkipTickAnimNodes) ++LivingAnimSkipped;
                if (Zed.Mesh.bSkipGetBoneAtoms) ++LivingBoneAtomsSkipped;
                if (Zed.Mesh.bInterpolateBoneAtoms) ++LivingBoneInterpolation;
                if (Zed.Mesh.bNotUpdatingKinematicDueToDistance)
                {
                    ++LivingKinematicDistanceSkipped;
                }
                if (Zed.Mesh.bTickAnimNodesWhenNotRendered)
                {
                    ++LivingTicksOffscreen;
                }
                if (Zed.Mesh.LastRenderTime > WorldInfo.TimeSeconds - 0.3)
                {
                    ++LivingRecentlyRendered;
                }
                else
                {
                    ++LivingOffscreen;
                }
            }
            LivingInjuredZones += CountBits(Zed.InjuredHitZones);
        }
    }

    // Reuse the one-second telemetry scan for scene pressure instead of
    // enumerating every living pawn again in the 250-ms corpse controller.
    AdaptiveVisibleLivingZeds = LivingRecentlyRendered;
    AdaptiveVisibleLivingObservedRealTime = WorldInfo.RealTimeSeconds;

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=living_done");

    GoreManager = KFGoreManager(WorldInfo.MyGoreEffectManager);
    if (GoreManager != None)
    {
        for (Index = 0; Index < GoreManager.CorpsePool.Length; ++Index)
        {
            Corpse = GoreManager.CorpsePool[Index];
            if (Corpse == None)
            {
                continue;
            }
            ++CorpseTotal;
            CollisionProbeCorpse = Corpse;
            if (Corpse.bHasBrokenConstraints)
            {
                ++DismemberedCorpses;
            }
            if (Corpse.Mesh != None && Corpse.Mesh.bNoSkeletonUpdate)
            {
                ++CorpseFinalPose;
            }
            if (Corpse.Mesh != None)
            {
                CorpseLodTotal += Corpse.Mesh.PredictedLODLevel;
                if (Corpse.Mesh.LastRenderTime > WorldInfo.TimeSeconds - 0.3)
                {
                    ++CorpseRecentlyRendered;
                }
                else
                {
                    ++CorpseOffscreen;
                }
            }
            CorpseInjuredZones += CountBits(Corpse.InjuredHitZones);
            for (HitZoneIndex = 0;
                 HitZoneIndex < Corpse.HitZones.Length;
                 ++HitZoneIndex)
            {
                if (Corpse.HitZones[HitZoneIndex].bPlayedInjury)
                {
                    ++DismemberedLimbs;
                }
            }
            if (Corpse.RagdollWarningLevel > 0)
            {
                ++RagdollWarnedCorpses;
                MaximumRagdollWarningLevel = Max(
                    MaximumRagdollWarningLevel,
                    Corpse.RagdollWarningLevel);
            }
            if (Corpse.TimeOfDeath > 0.0)
            {
                MaxCorpseAgeMs = Max(MaxCorpseAgeMs,
                    int((WorldInfo.TimeSeconds - Corpse.TimeOfDeath) * 1000.0));
            }
            if (Corpse.Physics != PHYS_RigidBody || Corpse.Mesh == None)
            {
                ++CorpseOther;
            }
            else if (Corpse.Mesh.RigidBodyIsAwake())
            {
                ++CorpseAwake;
            }
            else
            {
                ++CorpseSleeping;
            }
        }
        if (GoreManager.BodyWoundDecalManager != None)
        {
            BodyWoundDecals =
                GoreManager.BodyWoundDecalManager.ActiveDecals.Length;
        }
        if (GoreManager.BloodSplatterDecalManager != None)
        {
            BloodSplatterDecals =
                GoreManager.BloodSplatterDecalManager.ActiveDecals.Length;
        }
        if (GoreManager.BloodPoolDecalManager != None)
        {
            BloodPoolDecals =
                GoreManager.BloodPoolDecalManager.ActiveDecals.Length;
        }
        RuntimeCorpseLimit = bAdaptiveCorpseStaggerInitialized
            ? AdaptiveCorpseRuntimeLimit : GoreManager.MaxDeadBodies;
        RuntimeCorpseOffscreenTimeMs =
            int(GoreManager.MaxCorpseOffscreenTime * 1000.0);
        RuntimeCorpseOffscreenDistance =
            int(GoreManager.MaxCorpseOffscreenDistance);
        RuntimeWoundDecalLimit = GoreManager.MaxBodyWoundDecals;
        RuntimeSplatterDecalLimit = GoreManager.MaxBloodSplatterDecals;
        RuntimePoolDecalLimit = GoreManager.MaxBloodPoolDecals;
        RuntimeBloodEffectLimit = GoreManager.MaxBloodEffects;
        RuntimeGoreEffectLimit = GoreManager.MaxGoreEffects;
        RuntimeWoundLifetimeMs = int(GoreManager.BodyWoundDecalLifetime * 1000.0);
        RuntimeSplatterLifetimeMs = int(GoreManager.BloodSplatterLifetime * 1000.0);
        RuntimePoolLifetimeMs = int(GoreManager.BloodPoolLifetime * 1000.0);
        RuntimeGibLifetimeMs = int(GoreManager.GibletLifetime * 1000.0);
        if (GoreManager.BloodFXEmitterPool != None)
        {
            if (GoreManager.BloodFXEmitterPool.MaxActiveEffects <= 0)
            {
                GoreParticlePoolUnbounded = true;
            }
            GoreParticlePoolCapacity += Max(
                0, GoreManager.BloodFXEmitterPool.MaxActiveEffects);
        }
        if (GoreManager.MiscGoreFXEmitterPool != None)
        {
            if (GoreManager.MiscGoreFXEmitterPool.MaxActiveEffects <= 0)
            {
                GoreParticlePoolUnbounded = true;
            }
            GoreParticlePoolCapacity += Min(
                1000000000 - GoreParticlePoolCapacity,
                Max(0, GoreManager.MiscGoreFXEmitterPool.MaxActiveEffects));
        }
        CountParticlePool(GoreManager.BloodFXEmitterPool,
                          GoreParticleComponents, GoreParticles,
                          GoreParticleVisibleComponents,
                          GoreParticleLodTotal,
                          GoreParticleBoundedComponents,
                          ParticleFlexComponents,
                          ParticleFlexFluidComponents,
                          ParticleFlexNonFluidComponents,
                          ParticleFlexMixedComponents,
                          ParticleNonFlexComponents,
                          ParticleUnclassifiedComponents,
                          ParticleConstantSpawnEmitters,
                          ParticleDynamicSpawnEmitters,
                          ParticleConstantSpawnRateMilli,
                          ParticleBurstEntries,
                          ParticlePeakCapacity);
        if (GoreParticlePoolUnbounded)
        {
            GoreParticlePoolCapacity = 0;
        }
        CountParticlePool(GoreManager.MiscGoreFXEmitterPool,
                          GoreParticleComponents, GoreParticles,
                          GoreParticleVisibleComponents,
                          GoreParticleLodTotal,
                          GoreParticleBoundedComponents,
                          ParticleFlexComponents,
                          ParticleFlexFluidComponents,
                          ParticleFlexNonFluidComponents,
                          ParticleFlexMixedComponents,
                          ParticleNonFlexComponents,
                          ParticleUnclassifiedComponents,
                          ParticleConstantSpawnEmitters,
                          ParticleDynamicSpawnEmitters,
                          ParticleConstantSpawnRateMilli,
                          ParticleBurstEntries,
                           ParticlePeakCapacity);
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=gore_done");

    // These native queries require a live KFPawn instance. Calling them via
    // the class default object crashes KF2's Win64 client before the first
    // telemetry sample, while the game itself calls them from pawn instances.
    if (CollisionProbeCorpse != None &&
        CollisionProbeCorpse.ShouldCorpseCollideWithDead())
    {
        RuntimeCorpseCollideDead = 1;
    }
    if (CollisionProbeCorpse != None &&
        CollisionProbeCorpse.ShouldCorpseCollideWithLiving())
    {
        RuntimeCorpseCollideLiving = 1;
    }
    if (CollisionProbeCorpse != None &&
        CollisionProbeCorpse.ShouldCorpseCollideWithDeadAfterSleep())
    {
        RuntimeCorpseCollideDeadAfterSleep = 1;
    }
    if (CollisionProbeCorpse != None &&
        CollisionProbeCorpse.ShouldCorpseCollideWithLivingAfterSleep())
    {
        RuntimeCorpseCollideLivingAfterSleep = 1;
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=collision_done");

    if (WorldInfo.MyEmitterPool != None)
    {
        WorldParticlePoolCapacity = Max(
            0, WorldInfo.MyEmitterPool.MaxActiveEffects);
    }
    if (WorldInfo.GroundFireEmitterPool != None)
    {
        GroundFireParticlePoolCapacity = Max(
            0, WorldInfo.GroundFireEmitterPool.MaxActiveEffects);
    }
    if (WorldInfo.ImpactFXEmitterPool != None)
    {
        ImpactParticlePoolCapacity = Max(
            0, WorldInfo.ImpactFXEmitterPool.MaxActiveEffects);
    }
    CountParticlePool(WorldInfo.MyEmitterPool,
                      WorldParticleComponents, WorldParticles,
                      WorldParticleVisibleComponents,
                      WorldParticleLodTotal,
                      WorldParticleBoundedComponents,
                      ParticleFlexComponents,
                      ParticleFlexFluidComponents,
                      ParticleFlexNonFluidComponents,
                      ParticleFlexMixedComponents,
                      ParticleNonFlexComponents,
                      ParticleUnclassifiedComponents,
                      ParticleConstantSpawnEmitters,
                      ParticleDynamicSpawnEmitters,
                      ParticleConstantSpawnRateMilli,
                      ParticleBurstEntries,
                      ParticlePeakCapacity);
    CountParticlePool(WorldInfo.GroundFireEmitterPool,
                      GroundFireParticleComponents, GroundFireParticles,
                      GroundFireParticleVisibleComponents,
                      GroundFireParticleLodTotal,
                      GroundFireParticleBoundedComponents,
                      ParticleFlexComponents,
                      ParticleFlexFluidComponents,
                      ParticleFlexNonFluidComponents,
                      ParticleFlexMixedComponents,
                      ParticleNonFlexComponents,
                      ParticleUnclassifiedComponents,
                      ParticleConstantSpawnEmitters,
                      ParticleDynamicSpawnEmitters,
                      ParticleConstantSpawnRateMilli,
                      ParticleBurstEntries,
                      ParticlePeakCapacity);
    WorldParticleComponents += GroundFireParticleComponents;
    WorldParticles += GroundFireParticles;
    WorldParticleVisibleComponents += GroundFireParticleVisibleComponents;
    WorldParticleLodTotal += GroundFireParticleLodTotal;
    WorldParticleBoundedComponents += GroundFireParticleBoundedComponents;
    CountParticlePool(WorldInfo.ImpactFXEmitterPool,
                      ImpactParticleComponents, ImpactParticles,
                      ImpactParticleVisibleComponents,
                      ImpactParticleLodTotal,
                      ImpactParticleBoundedComponents,
                      ParticleFlexComponents,
                      ParticleFlexFluidComponents,
                      ParticleFlexNonFluidComponents,
                      ParticleFlexMixedComponents,
                      ParticleNonFlexComponents,
                      ParticleUnclassifiedComponents,
                      ParticleConstantSpawnEmitters,
                      ParticleDynamicSpawnEmitters,
                      ParticleConstantSpawnRateMilli,
                      ParticleBurstEntries,
                      ParticlePeakCapacity);
    WorldParticleComponents += ImpactParticleComponents;
    WorldParticles += ImpactParticles;
    WorldParticleVisibleComponents += ImpactParticleVisibleComponents;
    WorldParticleLodTotal += ImpactParticleLodTotal;
    WorldParticleBoundedComponents += ImpactParticleBoundedComponents;

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=pools_done");

    ImpactEffectManager = KFImpactEffectManager(WorldInfo.MyImpactEffectManager);
    if (ImpactEffectManager != None &&
        ImpactEffectManager.ImpactEffectDecalManager != None)
    {
        ImpactDecals =
            ImpactEffectManager.ImpactEffectDecalManager.ActiveDecals.Length;
        RuntimeImpactDecalLimit =
            ImpactEffectManager.ImpactEffectDecalManager.MaxActiveDecals;
    }
    if (WorldInfo.ExplosionDecalManager != None)
    {
        ExplosionDecals = WorldInfo.ExplosionDecalManager.ActiveDecals.Length;
        RuntimeExplosionDecalLimit =
            WorldInfo.ExplosionDecalManager.MaxActiveDecals;
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=decals_done");

    foreach WorldInfo.AllActors(class'KFSprayActor', SprayActor)
    {
        if (SprayActor == None || SprayActor.bDeleteMe)
        {
            continue;
        }
        ++SprayActors;
        if (SprayActor.MyDamageType != None &&
            ClassIsChildOf(SprayActor.MyDamageType, class'KFDT_Fire'))
        {
            ++FireSprayActors;
        }
        else if (SprayActor.MyDamageType != None &&
                 ClassIsChildOf(SprayActor.MyDamageType, class'KFDT_Toxic'))
        {
            ++ToxicSprayActors;
        }
        else
        {
            ++OtherSprayActors;
        }
    }
    foreach WorldInfo.AllActors(class'KFExplosionActor', ExplosionActor)
    {
        if (ExplosionActor == None || ExplosionActor.bDeleteMe)
        {
            continue;
        }
        ++ExplosionActors;
        if (KFExplosionActorLingering(ExplosionActor) != None)
        {
            ++LingeringExplosionActors;
        }
        if (ExplosionActor.ExplosionTemplate != None &&
            ExplosionActor.ExplosionTemplate.MyDamageType != None)
        {
            ++DamagingExplosionActors;
            if (ClassIsChildOf(
                    ExplosionActor.ExplosionTemplate.MyDamageType,
                    class'KFDT_Fire'))
            {
                ++FireExplosionActors;
            }
            else if (ClassIsChildOf(
                         ExplosionActor.ExplosionTemplate.MyDamageType,
                         class'KFDT_Toxic'))
            {
                ++ToxicExplosionActors;
            }
            else
            {
                ++OtherDamagingExplosionActors;
            }
        }
        else
        {
            ++UnclassifiedExplosionActors;
        }
        if (KFExplosion_HansSmokeGrenade(ExplosionActor) != None)
        {
            ++SmokeExplosionActors;
        }
        if (KFExplosion_BloatKingFart(ExplosionActor) != None)
        {
            ++BloatKingFartExplosionActors;
        }
    }
    foreach WorldInfo.AllActors(
        class'KFProj_HansSmokeGrenade', SmokeGrenadeProjectile)
    {
        if (SmokeGrenadeProjectile != None &&
            !SmokeGrenadeProjectile.bDeleteMe)
        {
            ++SmokeGrenadeProjectiles;
        }
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=effect_actors_done");
    foreach WorldInfo.AllActors(
        class'KFProj_BloatPukeMine', PukeMineProjectile)
    {
        if (PukeMineProjectile == None || PukeMineProjectile.bDeleteMe)
        {
            continue;
        }
        ++PukeMineProjectiles;
        if (KFProj_BloatKingPukeMine(PukeMineProjectile) != None)
        {
            ++BloatKingPukeMineProjectiles;
        }
    }

    if (WorldInfo.MyEmitterPool != None &&
        WorldInfo.MyEmitterPool.FlexSurrogateComponent != None)
    {
        FlexSurrogateParticles =
            WorldInfo.MyEmitterPool.FlexSurrogateComponent.NumActiveParticles;
        FlexSurrogateLod =
            WorldInfo.MyEmitterPool.FlexSurrogateComponent.GetLODLevel();
        if (WorldInfo.MyEmitterPool.FlexSurrogateComponent.bIsActive)
        {
            FlexSurrogateActive = 1;
        }
        if (WorldInfo.MyEmitterPool.FlexSurrogateComponent.LastRenderTime >
            WorldInfo.TimeSeconds - 0.3)
        {
            FlexSurrogateVisible = 1;
        }
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=surrogate_done");

    foreach WorldInfo.AllActors(class'KFGiblet', Gib)
    {
        if (Gib != None && !Gib.bDeleteMe)
        {
            ++VisibleGibs;
        }
    }
    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=gibs_done");
    foreach WorldInfo.AllActors(class'Emitter', WorldEmitter)
    {
        if (WorldEmitter == None || WorldEmitter.bDeleteMe ||
            WorldEmitter.ParticleSystemComponent == None ||
            !WorldEmitter.ParticleSystemComponent.bIsActive)
        {
            continue;
        }
        ++WorldParticleComponents;
        WorldParticles +=
            WorldEmitter.ParticleSystemComponent.NumActiveParticles;
        ClassifyParticleComponent(
            WorldEmitter.ParticleSystemComponent,
            ParticleFlexComponents, ParticleFlexFluidComponents,
            ParticleFlexNonFluidComponents, ParticleFlexMixedComponents,
            ParticleNonFlexComponents, ParticleUnclassifiedComponents);
        CountParticleSpawnEnvelope(
            WorldEmitter.ParticleSystemComponent,
            ParticleConstantSpawnEmitters, ParticleDynamicSpawnEmitters,
            ParticleConstantSpawnRateMilli, ParticleBurstEntries,
            ParticlePeakCapacity);
        WorldParticleLodTotal +=
            WorldEmitter.ParticleSystemComponent.GetLODLevel();
        if (WorldEmitter.ParticleSystemComponent.LastRenderTime >
            WorldInfo.TimeSeconds - 0.3)
        {
            ++WorldParticleVisibleComponents;
        }
        if (WorldEmitter.ParticleSystemComponent.Bounds.SphereRadius > 0.0)
        {
            ++WorldParticleBoundedComponents;
        }
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=emitters_done");

    GameInfo = KFGameInfo(WorldInfo.Game);
    if (GameInfo != None && GameInfo.IsZedTimeActive())
    {
        ZedTimeActive = 1;
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=sample_ready");

    ++SampleSequence;
    `log("KF2OPT_TELEMETRY schema=6 sample="$SampleSequence$
         " living="$LivingZeds$
         " living_classes="$LivingClasses.Length$
         " living_bosses="$LivingBosses$
         " living_visible="$LivingRecentlyRendered$
         " living_offscreen="$LivingOffscreen$
         " living_lod_total="$LivingLodTotal$
         " living_anim_rate_total="$LivingAnimationLodTotal$
         " living_injured_zones="$LivingInjuredZones$
         " living_required_bones="$LivingRequiredBones$
         " living_material_slots="$LivingMaterialSlots$
         " living_attachments="$LivingAttachments$
         " living_anim_skipped="$LivingAnimSkipped$
         " living_bone_atoms_skipped="$LivingBoneAtomsSkipped$
         " living_bone_interpolation="$LivingBoneInterpolation$
         " living_kinematic_distance_skipped="$LivingKinematicDistanceSkipped$
         " living_ticks_offscreen="$LivingTicksOffscreen$
         " living_special_moves="$LivingSpecialMoves$
         " living_attack_moves="$LivingAttackMoves$
         " living_grapple_moves="$LivingGrappleMoves$
         " living_stumbles="$LivingStumbles$
         " living_knockdowns="$LivingKnockdowns$
         " living_hit_reactions="$LivingHitReactions$
         " living_other_special_moves="$LivingOtherSpecialMoves$
         " corpse_total="$CorpseTotal$
         " corpse_awake="$CorpseAwake$
         " corpse_sleeping="$CorpseSleeping$
         " corpse_other="$CorpseOther$
         " corpse_final="$CorpseFinalPose$
         " corpse_visible="$CorpseRecentlyRendered$
         " corpse_offscreen="$CorpseOffscreen$
         " corpse_lod_total="$CorpseLodTotal$
         " corpse_injured_zones="$CorpseInjuredZones$
         " corpse_max_age_ms="$MaxCorpseAgeMs$
         " corpse_limit="$RuntimeCorpseLimit$
         " corpse_offscreen_time_ms="$RuntimeCorpseOffscreenTimeMs$
         " corpse_offscreen_distance="$RuntimeCorpseOffscreenDistance$
         " dismembered="$DismemberedCorpses$
         " dismembered_limbs="$DismemberedLimbs$
         " ragdoll_warned="$RagdollWarnedCorpses$
         " ragdoll_warning_max="$MaximumRagdollWarningLevel$
         " corpse_collide_dead="$RuntimeCorpseCollideDead$
         " corpse_collide_living="$RuntimeCorpseCollideLiving$
         " corpse_collide_dead_after_sleep="$RuntimeCorpseCollideDeadAfterSleep$
         " corpse_collide_living_after_sleep="$RuntimeCorpseCollideLivingAfterSleep$
         " gibs="$VisibleGibs$
         " zed_time="$ZedTimeActive$
         " spray_actors="$SprayActors$
         " fire_spray_actors="$FireSprayActors$
         " toxic_spray_actors="$ToxicSprayActors$
         " other_spray_actors="$OtherSprayActors$
         " explosion_actors="$ExplosionActors$
         " damaging_explosion_actors="$DamagingExplosionActors$
         " fire_explosion_actors="$FireExplosionActors$
         " toxic_explosion_actors="$ToxicExplosionActors$
         " other_damaging_explosion_actors="$OtherDamagingExplosionActors$
         " unclassified_explosion_actors="$UnclassifiedExplosionActors$
         " lingering_explosion_actors="$LingeringExplosionActors$
         " smoke_explosion_actors="$SmokeExplosionActors$
         " bloat_king_fart_explosion_actors="$BloatKingFartExplosionActors$
         " smoke_grenade_projectiles="$SmokeGrenadeProjectiles$
         " puke_mine_projectiles="$PukeMineProjectiles$
         " bloat_king_puke_mine_projectiles="$BloatKingPukeMineProjectiles$
         " wound_decals="$BodyWoundDecals$
         " splatter_decals="$BloodSplatterDecals$
         " pool_decals="$BloodPoolDecals$
         " impact_decals="$ImpactDecals$
         " explosion_decals="$ExplosionDecals$
         " wound_decal_limit="$RuntimeWoundDecalLimit$
         " splatter_decal_limit="$RuntimeSplatterDecalLimit$
         " pool_decal_limit="$RuntimePoolDecalLimit$
         " impact_decal_limit="$RuntimeImpactDecalLimit$
         " explosion_decal_limit="$RuntimeExplosionDecalLimit$
         " blood_effect_limit="$RuntimeBloodEffectLimit$
         " gore_effect_limit="$RuntimeGoreEffectLimit$
         " wound_lifetime_ms="$RuntimeWoundLifetimeMs$
         " splatter_lifetime_ms="$RuntimeSplatterLifetimeMs$
         " pool_lifetime_ms="$RuntimePoolLifetimeMs$
         " gib_lifetime_ms="$RuntimeGibLifetimeMs$
         " gore_particle_components="$GoreParticleComponents$
         " gore_particles="$GoreParticles$
         " gore_particle_visible_components="$GoreParticleVisibleComponents$
         " gore_particle_lod_total="$GoreParticleLodTotal$
         " gore_particle_bounded_components="$GoreParticleBoundedComponents$
         " world_particle_components="$WorldParticleComponents$
         " world_particles="$WorldParticles$
         " world_particle_visible_components="$WorldParticleVisibleComponents$
         " world_particle_lod_total="$WorldParticleLodTotal$
         " world_particle_bounded_components="$WorldParticleBoundedComponents$
         " ground_fire_particle_components="$GroundFireParticleComponents$
         " ground_fire_particles="$GroundFireParticles$
         " impact_particle_components="$ImpactParticleComponents$
         " impact_particles="$ImpactParticles$
         " gore_particle_pool_capacity="$GoreParticlePoolCapacity$
         " world_particle_pool_capacity="$WorldParticlePoolCapacity$
         " ground_fire_particle_pool_capacity="$GroundFireParticlePoolCapacity$
         " impact_particle_pool_capacity="$ImpactParticlePoolCapacity$
         " particle_constant_spawn_emitters="$ParticleConstantSpawnEmitters$
         " particle_dynamic_spawn_emitters="$ParticleDynamicSpawnEmitters$
         " particle_constant_spawn_rate_milli="$ParticleConstantSpawnRateMilli$
         " particle_burst_entries="$ParticleBurstEntries$
         " particle_peak_capacity="$ParticlePeakCapacity$
         " particle_flex_components="$ParticleFlexComponents$
         " particle_flex_fluid_components="$ParticleFlexFluidComponents$
         " particle_flex_nonfluid_components="$ParticleFlexNonFluidComponents$
         " particle_flex_mixed_components="$ParticleFlexMixedComponents$
         " particle_nonflex_components="$ParticleNonFlexComponents$
         " particle_unclassified_components="$ParticleUnclassifiedComponents$
         " flex_surrogate_active="$FlexSurrogateActive$
         " flex_surrogate_particles="$FlexSurrogateParticles$
         " flex_surrogate_visible="$FlexSurrogateVisible$
         " flex_surrogate_lod="$FlexSurrogateLod);
}

event Destroyed()
{
    RestoreAdaptiveGraphics();
    ClearTimer(nameof(SampleTelemetry), self);
    ClearTimer(nameof(StaggerCorpseCleanup), self);
    ClearTimer(nameof(AdaptiveCorpseLoadControl), self);
    RestoreAllAdaptiveCorpseLods();
    RestoreAllAdaptiveLivingVisuals();
    AdaptiveDistanceSleptCorpses.Length = 0;
    AdaptiveCorpseDebugMarkers.Length = 0;
    AdaptiveCorpseRagdollSleepIds.Length = 0;
    AdaptiveCorpseRagdollSleepIdCount = 0;
    if (bAdaptiveCorpseStaggerInitialized &&
        AdaptiveCorpseManager != None &&
        AdaptiveCorpseManager.MaxDeadBodies == AdaptiveCorpseTarget)
    {
        AdaptiveCorpseManager.MaxDeadBodies = AdaptiveCorpseOriginalLimit;
    }
    if (AdaptiveCorpsesRemoved > 0)
    {
        `log("KF2OPT_CORPSE_STAGGER state=stopped removed="$
             AdaptiveCorpsesRemoved);
    }
    if (AdaptiveCorpsesSlept > 0 || AdaptiveSkeletonReductions > 0)
    {
        `log("KF2OPT_CORPSE_LOAD state=stopped slept="$
             AdaptiveCorpsesSlept$" skeleton_reductions="$
             AdaptiveSkeletonReductions);
    }
    if (AdaptiveBaselinePhysicsSleeps > 0)
    {
        `log("KF2OPT_CORPSE_BASELINE state=stopped slept="$
             AdaptiveBaselinePhysicsSleeps);
    }
    if (AdaptiveCorpseLodReductions > 0 || AdaptiveCorpseLodRestores > 0)
    {
        `log("KF2OPT_CORPSE_LOD state=stopped reduced="$
             AdaptiveCorpseLodReductions$" restored="$
             AdaptiveCorpseLodRestores);
    }
    if (AdaptiveDistancePhysicsSleeps > 0 ||
        AdaptiveDistancePhysicsWakes > 0)
    {
        `log("KF2OPT_CORPSE_DISTANCE state=stopped slept="$
             AdaptiveDistancePhysicsSleeps$" woken="$
             AdaptiveDistancePhysicsWakes);
    }
    if (AdaptiveVisibleRagdollSleeps > 0)
    {
        `log("KF2OPT_CORPSE_RAGDOLL state=stopped slept="$
             AdaptiveVisibleRagdollSleeps);
    }
    if (AdaptiveLivingVisualReductions > 0 ||
        AdaptiveLivingVisualRestores > 0)
    {
        `log("KF2OPT_LIVING_VISUAL state=stopped reduced="$
             AdaptiveLivingVisualReductions$" restored="$
             AdaptiveLivingVisualRestores);
    }
    Super.Destroyed();
}

defaultproperties
{
    bAlwaysRelevant=false
    bHidden=true
    RemoteRole=ROLE_None
}
