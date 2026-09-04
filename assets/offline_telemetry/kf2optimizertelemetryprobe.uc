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

struct AdaptiveZedDebugMarkerEntry
{
    // Value-only snapshots deliberately avoid retaining living pawn actors.
    var vector Location;
    var vector Velocity;
    var string ZedId;
    var int DistanceUnits;
};

struct AdaptiveDebugMarkerScreenEntry
{
    var float X;
    var float Y;
    var float Width;
    var float Height;
};

struct AdaptiveDistanceSleepEntry
{
    var KFPawn Corpse;
    var string CorpseId;
};

struct AdaptiveDistanceSleepTransitionEntry
{
    var string CorpseId;
    var string RemovalReason;
    var float NativeWakeObservedRealTime;
    var int NativeWakeCount;
    var float NativeWakeCooldownUntilRealTime;
    var float ExpiresRealTime;
    var bool bReusable;
};

var int SampleSequence;
var int ProfileWindowSamples;
var int ProfileTotalMilliseconds;
var int ProfileLivingMilliseconds;
var int ProfileCorpseGoreMilliseconds;
var int ProfileParticlePoolMilliseconds;
var int ProfileEffectActorMilliseconds;
var int ProfileWorldEmitterMilliseconds;
var int ProfileClockAnomalies;
var globalconfig bool bAdaptiveCorpseStagger;
var globalconfig bool bAdaptiveRuntimeEnabled;
var globalconfig bool bAdaptiveCorpseDebugMarkers;
var globalconfig bool bAdaptiveZedDebugMarkers;
// Manual, one-world diagnostic; never enabled by normal Adaptive settings.
var globalconfig bool bDebugNativeWakeTest;
var bool bDebugNativeWakeTestStarted;
var KF2OptimizerNativeWakeTest DebugNativeWakeTest;
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
var int AdaptiveAgingPhysicsSleeps;
var int AdaptiveSkeletonReductions;
var int AdaptiveCorpseAgingCursor;
var int AdaptiveCorpseAgingReductions;
var float AdaptiveLastCorpseSleepRealTime;
var float AdaptiveLastCorpseCapacityRealTime;
var array<KFPawn> AdaptiveCorpseLodCorpses;
var array<int> AdaptiveCorpseLodOriginalMinModels;
var array<int> AdaptiveCorpseLodAppliedMinModels;
var array<bool> AdaptiveCorpseLodPersistentAging;
var array<bool> AdaptiveCorpseLodPhysicsFrozen;
var int AdaptiveCorpseLodReductions;
var int AdaptiveCorpseLodRestores;
var float AdaptiveLastCorpseLodRealTime;
var array<AdaptiveDistanceSleepEntry> AdaptiveDistanceSleptCorpses;
var array<AdaptiveDistanceSleepTransitionEntry>
    AdaptiveDistanceSleepTransitions;
var int AdaptiveDistanceSleepTransitionCount;
var int AdaptiveDistanceSleepTransitionPruneCursor;
var bool bAdaptiveDistanceSleepTransitionFullLogged;
var int AdaptiveDistancePhysicsSleeps;
var int AdaptiveDistancePhysicsWakes;
var int AdaptiveVisibleRagdollSleeps;
var float AdaptiveLastNearRagdollRejectRealTime;
var array<string> AdaptiveCorpsePhysicsActionIds;
var int AdaptiveCorpsePhysicsActionIdCount;
var float AdaptiveLastDistancePhysicsRealTime;
var int AdaptiveVisibleLivingZeds;
var float AdaptiveVisibleLivingObservedRealTime;
var int AdaptiveCorpseScenePressureLevel;
var array<AdaptiveCorpseDebugMarkerEntry> AdaptiveCorpseDebugMarkers;
var array<AdaptiveZedDebugMarkerEntry> AdaptiveZedDebugMarkers;
var array<AdaptiveDebugMarkerScreenEntry> AdaptiveDebugMarkerScreenEntries;
var float AdaptiveZedDebugRefreshRealTime;
var transient HUD AdaptiveDebugMarkerHUD;
var bool bAdaptiveDebugMarkerRenderConfirmed;
var array<KFPawn_Monster> AdaptiveLivingVisualZeds;
var array<int> AdaptiveLivingOriginalMinLods;
var array<int> AdaptiveLivingAppliedMinLods;
var array<float> AdaptiveLivingOriginalAnimDistances;
var array<float> AdaptiveLivingAppliedAnimDistances;
var array<int> AdaptiveLivingOriginalAnimRates;
var array<int> AdaptiveLivingAppliedAnimRates;
var int AdaptiveLivingVisualReductions;
var int AdaptiveLivingVisualRestores;
var int AdaptiveLivingVisualPressureLevel;
var int AdaptiveLivingVisualPendingPressureLevel;
var float AdaptiveLivingVisualPendingSinceRealTime;
var float AdaptiveLivingVisualLastChangeRealTime;
var bool bAdaptiveRuntimeQuiesced;

function bool ValidAdaptiveControlToken(string Candidate)
{
    return Len(AdaptiveControlToken) == 32 && Candidate == AdaptiveControlToken;
}

function bool SetAdaptiveRuntimeEnabled(bool bEnabled)
{
    if (WorldInfo == None || WorldInfo.NetMode != NM_Standalone)
    {
        return false;
    }
    if (bEnabled)
    {
        ClearTimer(nameof(RestoreOneAdaptiveCorpseLod), self);
        bAdaptiveRuntimeEnabled = true;
        if (bAdaptiveCorpseStagger)
        {
            SetTimer(0.45, true, nameof(StaggerCorpseCleanup), self);
            SetTimer(0.25, true, nameof(AdaptiveCorpseLoadControl), self);
            SetTimer(0.05, true, nameof(AdaptiveCorpseAgingControl), self);
        }
        `log("KF2OPT_ADAPTIVE_MODE state=enabled readback=verified");
        return true;
    }

    if (AdaptiveGraphicsState != None &&
        !class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(
            AdaptiveGraphicsState, "recover", 100))
    {
        return false;
    }
    if (!ApplyAdaptiveEffectRuntimeReadback("recover", 100))
    {
        return false;
    }
    BeginAdaptiveCorpseLodRelease();
    RestoreAllAdaptiveLivingVisuals();
    BeginAdaptiveDistanceSleepRelease();
    if (AdaptiveCorpseManager != None)
    {
        AdaptiveCorpseRuntimeLimit = AdaptiveCorpseTarget;
        AdaptiveCorpseManager.MaxDeadBodies = AdaptiveCorpseTarget;
    }
    ClearTimer(nameof(StaggerCorpseCleanup), self);
    ClearTimer(nameof(AdaptiveCorpseLoadControl), self);
    ClearTimer(nameof(AdaptiveCorpseAgingControl), self);
    AdaptiveCorpsePressureSamples = 0;
    AdaptiveCorpseRecoverySamples = 0;
    AdaptiveCorpsePressureLevel = 0;
    AdaptiveCorpseCurrentFramePressureLevel = 0;
    AdaptiveCorpseScenePressureLevel = 0;
    AdaptiveLivingVisualPressureLevel = 0;
    AdaptiveLivingVisualPendingPressureLevel = 0;
    AdaptiveFramePressureObservedRealTime = 0.0;
    bAdaptiveRuntimeEnabled = false;
    `log("KF2OPT_ADAPTIVE_MODE state=disabled readback=verified"$
         " corpse_limit="$AdaptiveCorpseRuntimeLimit$
         " telemetry=active");
    return true;
}

function bool ApplyAdaptiveResourceControl(
    string Token, int Sequence, string Resource, int Quality)
{
    local int QualityStage;
    local int PreviousGpuQuality;
    local int PreviousCpuQuality;
    local int PreviousVramQuality;
    local int PreviousRamQuality;
    local int PreviousOverdrawQuality;
    local int PreviousEffectsQuality;

    if (!ValidAdaptiveControlToken(Token) || Sequence <= 0 ||
        Sequence <= AdaptiveLastControlSequence ||
        Quality < 10 || Quality > 100 ||
        !((Resource ~= "gpu") || (Resource ~= "vram") ||
          (Resource ~= "cpu") || (Resource ~= "ram") ||
          (Resource ~= "overdraw") ||
          (Resource ~= "effects") ||
          (Resource ~= "mixed") || (Resource ~= "recover") ||
          (Resource ~= "enable") || (Resource ~= "disable")))
    {
        return false;
    }
    if ((Resource ~= "enable") || (Resource ~= "disable"))
    {
        if (!SetAdaptiveRuntimeEnabled(Resource ~= "enable"))
        {
            return false;
        }
        AdaptiveGraphicsQuality = Resource ~= "disable" ? 100 : Quality;
        AdaptiveGraphicsResource = Resource;
        AdaptiveLastControlSequence = Sequence;
        return true;
    }
    QualityStage = Clamp((Quality + 9) / 10, 1, 10);

    PreviousGpuQuality = AdaptiveGraphicsState == None
        ? 100 : AdaptiveGraphicsState.GpuQuality;
    PreviousCpuQuality = AdaptiveGraphicsState == None
        ? 100 : AdaptiveGraphicsState.CpuQuality;
    PreviousVramQuality = AdaptiveGraphicsState == None
        ? 100 : AdaptiveGraphicsState.VramQuality;
    PreviousRamQuality = AdaptiveGraphicsState == None
        ? 100 : AdaptiveGraphicsState.RamQuality;
    PreviousOverdrawQuality = AdaptiveGraphicsState == None
        ? 100 : AdaptiveGraphicsState.OverdrawQuality;
    PreviousEffectsQuality = AdaptiveGraphicsState == None
        ? 100 : AdaptiveGraphicsState.EffectsQuality;
    if (AdaptiveGraphicsState == None ||
        !class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(
            AdaptiveGraphicsState, Resource, Quality))
    {
        `log("KF2OPT_ADAPTIVE_QUALITY state=failed seq="$Sequence$
             " resource="$Resource$" quality="$Quality$
             " reason=readback_mismatch");
        return false;
    }
    if (!ApplyAdaptiveEffectRuntimeReadback(Resource, Quality))
    {
        if (!RollbackAdaptiveResourceControl(
                PreviousGpuQuality, PreviousCpuQuality,
                PreviousVramQuality, PreviousRamQuality,
                PreviousOverdrawQuality, PreviousEffectsQuality) ||
            !ApplyAdaptiveEffectRuntimeReadback(
                "rollback", PreviousEffectsQuality))
        {
            `log("KF2OPT_EFFECT_RUNTIME state=rollback_failed resource="$
                 Resource$" quality="$Quality);
        }
        `log("KF2OPT_ADAPTIVE_QUALITY state=failed seq="$Sequence$
             " resource="$Resource$" quality="$Quality$
             " reason=runtime_readback_mismatch");
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

function bool RollbackAdaptiveResourceControl(
    int GpuQuality, int CpuQuality, int VramQuality, int RamQuality,
    int OverdrawQuality, int EffectsQuality)
{
    local bool bRestored;

    bRestored = true;
    if (!class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(
            AdaptiveGraphicsState, "gpu", GpuQuality))
    {
        bRestored = false;
    }
    if (!class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(
            AdaptiveGraphicsState, "cpu", CpuQuality))
    {
        bRestored = false;
    }
    if (!class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(
            AdaptiveGraphicsState, "vram", VramQuality))
    {
        bRestored = false;
    }
    if (!class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(
            AdaptiveGraphicsState, "ram", RamQuality))
    {
        bRestored = false;
    }
    if (!class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(
            AdaptiveGraphicsState, "overdraw", OverdrawQuality))
    {
        bRestored = false;
    }
    if (!class'KF2OptimizerAdaptiveGraphics'.static.ApplyResource(
            AdaptiveGraphicsState, "effects", EffectsQuality))
    {
        bRestored = false;
    }
    return bRestored;
}

// KF2's graphics menu updates class defaults, but managers and emitter pools
// created earlier in the current world keep their old limits. Synchronize the
// already-live instances and include them in the authenticated APPLIED result.
function bool ApplyAdaptiveEffectRuntimeReadback(
    string Resource, int Quality)
{
    local KFGoreManager GoreManager;
    local KFImpactEffectManager ImpactEffectManager;
    local int DesiredImpactEffects;
    local bool bReadbackMatches;

    if (WorldInfo == None || WorldInfo.NetMode != NM_Standalone)
    {
        return false;
    }
    GoreManager = KFGoreManager(WorldInfo.MyGoreEffectManager);
    if (GoreManager == None)
    {
        `log("KF2OPT_EFFECT_RUNTIME state=unavailable resource="$Resource$
             " quality="$Quality$" reason=no_gore_manager");
        return false;
    }

    GoreManager.GoreFXLifetimeMultiplier =
        class'KFGoreManager'.default.GoreFXLifetimeMultiplier;
    GoreManager.MaxBodyWoundDecals =
        class'KFGoreManager'.default.MaxBodyWoundDecals;
    GoreManager.MaxBloodEffects =
        class'KFGoreManager'.default.MaxBloodEffects;
    GoreManager.MaxGoreEffects =
        class'KFGoreManager'.default.MaxGoreEffects;
    GoreManager.MaxPersistentSplatsPerFrame =
        class'KFGoreManager'.default.MaxPersistentSplatsPerFrame;
    GoreManager.bAllowBloodSplatterDecals =
        class'KFGoreManager'.default.bAllowBloodSplatterDecals;
    if (GoreManager.BodyWoundDecalManager != None)
    {
        GoreManager.BodyWoundDecalManager.MaxActiveDecals =
            GoreManager.MaxBodyWoundDecals;
    }
    if (GoreManager.BloodFXEmitterPool != None)
    {
        GoreManager.BloodFXEmitterPool.MaxActiveEffects =
            GoreManager.MaxBloodEffects;
    }
    if (GoreManager.MiscGoreFXEmitterPool != None)
    {
        GoreManager.MiscGoreFXEmitterPool.MaxActiveEffects =
            GoreManager.MaxGoreEffects;
    }

    ImpactEffectManager =
        KFImpactEffectManager(WorldInfo.MyImpactEffectManager);
    if (ImpactEffectManager != None)
    {
        ImpactEffectManager.MaxImpactEffectDecals =
            class'KFImpactEffectManager'.default.MaxImpactEffectDecals;
        if (ImpactEffectManager.ImpactEffectDecalManager != None)
        {
            ImpactEffectManager.ImpactEffectDecalManager.MaxActiveDecals =
                ImpactEffectManager.MaxImpactEffectDecals;
        }
    }
    DesiredImpactEffects = Max(1, int(
        float(class'KFImpactFXEmitterPool'.default.MaxActiveEffects) *
        class'WorldInfo'.default.EmitterPoolScale));
    if (WorldInfo.ImpactFXEmitterPool != None)
    {
        WorldInfo.ImpactFXEmitterPool.MaxActiveEffects =
            DesiredImpactEffects;
    }
    WorldInfo.MaxExplosionDecals = class'WorldInfo'.default.MaxExplosionDecals;
    if (WorldInfo.ExplosionDecalManager != None)
    {
        WorldInfo.ExplosionDecalManager.MaxActiveDecals =
            WorldInfo.MaxExplosionDecals;
    }

    bReadbackMatches =
        Abs(GoreManager.GoreFXLifetimeMultiplier -
            class'KFGoreManager'.default.GoreFXLifetimeMultiplier) < 0.001 &&
        GoreManager.MaxBodyWoundDecals ==
            class'KFGoreManager'.default.MaxBodyWoundDecals &&
        GoreManager.MaxBloodEffects ==
            class'KFGoreManager'.default.MaxBloodEffects &&
        GoreManager.MaxGoreEffects ==
            class'KFGoreManager'.default.MaxGoreEffects &&
        GoreManager.MaxPersistentSplatsPerFrame ==
            class'KFGoreManager'.default.MaxPersistentSplatsPerFrame &&
        GoreManager.bAllowBloodSplatterDecals ==
            class'KFGoreManager'.default.bAllowBloodSplatterDecals &&
        (GoreManager.BodyWoundDecalManager == None ||
         GoreManager.BodyWoundDecalManager.MaxActiveDecals ==
            GoreManager.MaxBodyWoundDecals) &&
        (GoreManager.BloodFXEmitterPool == None ||
         GoreManager.BloodFXEmitterPool.MaxActiveEffects ==
            GoreManager.MaxBloodEffects) &&
        (GoreManager.MiscGoreFXEmitterPool == None ||
         GoreManager.MiscGoreFXEmitterPool.MaxActiveEffects ==
            GoreManager.MaxGoreEffects) &&
        (ImpactEffectManager == None ||
         (ImpactEffectManager.MaxImpactEffectDecals ==
            class'KFImpactEffectManager'.default.MaxImpactEffectDecals &&
          (ImpactEffectManager.ImpactEffectDecalManager == None ||
           ImpactEffectManager.ImpactEffectDecalManager.MaxActiveDecals ==
            ImpactEffectManager.MaxImpactEffectDecals))) &&
        (WorldInfo.ImpactFXEmitterPool == None ||
         WorldInfo.ImpactFXEmitterPool.MaxActiveEffects ==
            DesiredImpactEffects) &&
        WorldInfo.MaxExplosionDecals ==
            class'WorldInfo'.default.MaxExplosionDecals &&
        (WorldInfo.ExplosionDecalManager == None ||
         WorldInfo.ExplosionDecalManager.MaxActiveDecals ==
            WorldInfo.MaxExplosionDecals);
    if (!bReadbackMatches)
    {
        `log("KF2OPT_EFFECT_RUNTIME state=failed resource="$Resource$
             " quality="$Quality$" reason=readback_mismatch");
        return false;
    }
    `log("KF2OPT_EFFECT_RUNTIME state=applied resource="$Resource$
         " quality="$Quality$" blood_limit="$GoreManager.MaxBloodEffects$
         " gore_limit="$GoreManager.MaxGoreEffects$
         " blood_pool="$
         (GoreManager.BloodFXEmitterPool == None ? -1 :
          GoreManager.BloodFXEmitterPool.MaxActiveEffects)$
         " gore_pool="$
         (GoreManager.MiscGoreFXEmitterPool == None ? -1 :
          GoreManager.MiscGoreFXEmitterPool.MaxActiveEffects)$
         " wound_decals="$GoreManager.MaxBodyWoundDecals$
         " impact_decals="$
         (ImpactEffectManager == None ? -1 :
          ImpactEffectManager.MaxImpactEffectDecals)$
         " impact_pool="$
         (WorldInfo.ImpactFXEmitterPool == None ? -1 :
          WorldInfo.ImpactFXEmitterPool.MaxActiveEffects)$
         " explosion_decals="$WorldInfo.MaxExplosionDecals$
         " readback=verified");
    return true;
}

function RestoreAdaptiveGraphics()
{
    if (!class'KF2OptimizerAdaptiveGraphics'.static.RestoreOriginal(
            AdaptiveGraphicsState) ||
        !ApplyAdaptiveEffectRuntimeReadback("restore", 100))
    {
        `log("KF2OPT_ADAPTIVE_QUALITY state=restore_failed reason=readback_mismatch");
        return;
    }
    AdaptiveGraphicsQuality = 100;
    AdaptiveGraphicsResource = "recover";
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
    AdaptiveLastNearRagdollRejectRealTime =
        WorldInfo.RealTimeSeconds - 2.0;
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

    if (!bAdaptiveCorpseStagger || !bAdaptiveRuntimeEnabled ||
        WorldInfo == None ||
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

function int ResolveAdaptiveLivingEnemyPressureLevel(float PressureScale)
{
    local int RequestedLevel;
    local float HoldSeconds;
    local float RequiredScale;

    RequestedLevel = GetAdaptiveLivingEnemyPressureLevel(PressureScale);
    if (RequestedLevel == AdaptiveLivingVisualPressureLevel)
    {
        AdaptiveLivingVisualPendingPressureLevel = 0;
        AdaptiveLivingVisualPendingSinceRealTime = 0.0;
        return AdaptiveLivingVisualPressureLevel;
    }
    // A three-percent dead band keeps adjacent pressure samples from bouncing
    // across a tier boundary. Rising pressure reacts sooner than recovery.
    if (RequestedLevel > AdaptiveLivingVisualPressureLevel)
    {
        RequiredScale = AdaptiveLivingVisualPressureLevel <= 0 ?
            0.02 : float(AdaptiveLivingVisualPressureLevel) / 5.0 + 0.03;
        HoldSeconds = 0.75;
        if (PressureScale < RequiredScale)
        {
            AdaptiveLivingVisualPendingPressureLevel = 0;
            AdaptiveLivingVisualPendingSinceRealTime = 0.0;
            return AdaptiveLivingVisualPressureLevel;
        }
    }
    else
    {
        RequiredScale = FMax(0.0,
            float(AdaptiveLivingVisualPressureLevel - 1) / 5.0 - 0.03);
        HoldSeconds = 1.25;
        if (PressureScale > RequiredScale)
        {
            AdaptiveLivingVisualPendingPressureLevel = 0;
            AdaptiveLivingVisualPendingSinceRealTime = 0.0;
            return AdaptiveLivingVisualPressureLevel;
        }
    }
    if (AdaptiveLivingVisualLastChangeRealTime > 0.0 &&
        WorldInfo.RealTimeSeconds - AdaptiveLivingVisualLastChangeRealTime < 1.5)
    {
        return AdaptiveLivingVisualPressureLevel;
    }
    if (AdaptiveLivingVisualPendingPressureLevel != RequestedLevel)
    {
        AdaptiveLivingVisualPendingPressureLevel = RequestedLevel;
        AdaptiveLivingVisualPendingSinceRealTime = WorldInfo.RealTimeSeconds;
        return AdaptiveLivingVisualPressureLevel;
    }
    if (WorldInfo.RealTimeSeconds - AdaptiveLivingVisualPendingSinceRealTime <
        HoldSeconds)
    {
        return AdaptiveLivingVisualPressureLevel;
    }
    AdaptiveLivingVisualPressureLevel = RequestedLevel;
    AdaptiveLivingVisualPendingPressureLevel = 0;
    AdaptiveLivingVisualPendingSinceRealTime = 0.0;
    AdaptiveLivingVisualLastChangeRealTime = WorldInfo.RealTimeSeconds;
    return AdaptiveLivingVisualPressureLevel;
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
    local float TierScale;
    local KFPawn_Monster Candidate;
    local PlayerController LocalPC;

    if (EnemyPressureLevel <= 0)
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
    // Targets use the stable effective tier, never the continuously changing
    // observation. This prevents redundant writes while preserving pressure.
    TierScale = float(EnemyPressureLevel) / 5.0;
    MinimumDistance = 600.0 - TierScale * 300.0;
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
        TargetMinLod = 1 + int(TierScale * float(MaximumMinLod));
        if (DistanceSquared >= 1440000.0)
        {
            ++TargetMinLod;
        }
        TargetMinLod = Min(TargetMinLod, MaximumMinLod);
        TargetAnimDistance = FMin(0.55, 0.15 + TierScale * 0.40);
        TargetAnimRate = Clamp(1 + EnemyPressureLevel, 2, 6);
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
             GetAdaptiveCorpseDistanceUnits(Candidate)$" distance_m="$
             FormatAdaptiveCorpseDistanceMeters(
                 GetAdaptiveCorpseDistanceUnits(Candidate), false)$
             " readback=verified");
    }
}

function bool ApplyOneAdaptiveCorpseAging(KFGoreManager GoreManager)
{
    local int EntryIndex;
    local int DistanceUnits;
    local int MaximumMinLod;
    local int TargetMinLod;
    local int Tier;
    local float CorpseAge;
    local float MaximumSpeedSquared;
    local float MinimumTierOneAge;
    local float MinimumTierTwoAge;
    local float MinimumTierThreeAge;
    local bool bFinalTierInteractionSafe;
    local bool bFinalTierLodReady;
    local bool bRecentlyRendered;
    local string PhysicsAction;
    local KFPawn Candidate;

    if (GoreManager == None || GoreManager.CorpsePool.Length == 0)
    {
        AdaptiveCorpseAgingCursor = 0;
        return false;
    }
    if (AdaptiveCorpseAgingCursor < 0 ||
        AdaptiveCorpseAgingCursor >= GoreManager.CorpsePool.Length)
    {
        AdaptiveCorpseAgingCursor = 0;
    }
    Candidate = GoreManager.CorpsePool[AdaptiveCorpseAgingCursor];
    ++AdaptiveCorpseAgingCursor;

    if (Candidate == None || Candidate.bDeleteMe ||
        KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
        Candidate.TimeOfDeath <= 0.0 ||
        Candidate.Physics != PHYS_RigidBody ||
        Candidate.SpecialMove == SM_DeathAnim)
    {
        return false;
    }

    MinimumTierOneAge = 3.0;
    MinimumTierTwoAge = 8.0;
    MinimumTierThreeAge = 15.0;
    CorpseAge = WorldInfo.TimeSeconds - Candidate.TimeOfDeath;
    if (CorpseAge < MinimumTierOneAge)
    {
        return false;
    }
    Tier = CorpseAge >= MinimumTierThreeAge ? 3 :
        CorpseAge >= MinimumTierTwoAge ? 2 : 1;
    DistanceUnits = GetAdaptiveCorpseDistanceUnits(Candidate);
    bRecentlyRendered = Candidate.Mesh.LastRenderTime >
        WorldInfo.TimeSeconds - 0.3;
    // A final PHYS_None pose cannot react to a nearby player, hit or
    // explosion. Require both the existing 800-unit interaction boundary and
    // native render recency to be clear. The persistent cursor revisits a
    // deferred corpse later, so moving away or losing visibility can still
    // advance it without a one-frame batch.
    bFinalTierInteractionSafe = DistanceUnits >= 800 &&
        !bRecentlyRendered;
    EntryIndex = FindAdaptiveCorpseLodEntry(Candidate);
    bFinalTierLodReady = false;
    if (Tier >= 3 && bFinalTierInteractionSafe && EntryIndex >= 0 &&
        EntryIndex < AdaptiveCorpseLodAppliedMinModels.Length &&
        EntryIndex < AdaptiveCorpseLodPersistentAging.Length &&
        EntryIndex < AdaptiveCorpseLodPhysicsFrozen.Length &&
        Candidate.Mesh.SkeletalMesh != None &&
        Candidate.Mesh.SkeletalMesh.LODInfo.Length >= 2 &&
        Candidate.Mesh.ForcedLodModel == 0)
    {
        MaximumMinLod = Candidate.Mesh.SkeletalMesh.LODInfo.Length - 1;
        bFinalTierLodReady =
            AdaptiveCorpseLodAppliedMinModels[EntryIndex] == MaximumMinLod &&
            Candidate.Mesh.MinLodModel == MaximumMinLod;
        if (bFinalTierLodReady)
        {
            AdaptiveCorpseLodPersistentAging[EntryIndex] = true;
        }
    }

    // Only one corpse and one meaningful state transition are handled per
    // 50-ms invocation. Visible old corpses still receive staged LOD and
    // sleeping-skeleton reductions. The final tier waits for its final real
    // LOD plus the interaction guard; the earlier tiers retain distance guards
    // so a fresh nearby visible ragdoll still has a native reaction window.
    MaximumSpeedSquared = Tier >= 3 ? 160000.0 : 62500.0;
    PhysicsAction = Tier >= 3 ? "aging_freeze" : "aging";
    if ((Candidate.Mesh.RigidBodyIsAwake() || Tier >= 3) &&
        VSizeSq(Candidate.Velocity) <= MaximumSpeedSquared &&
        (Tier < 3 ||
         (bFinalTierInteractionSafe && bFinalTierLodReady)) &&
        (Tier >= 3 ||
         (Tier == 2 && DistanceUnits >= 1000) ||
         (Tier == 1 && DistanceUnits >= 1200)) &&
        (Tier < 3 || FindAdaptiveCorpseLodEntry(Candidate) >= 0) &&
        (Tier >= 3 || FindAdaptiveDistanceSleptCorpse(Candidate) == -1) &&
        (Tier >= 3 ||
         !DeferAdaptiveDistanceResleepAfterNativeWake(Candidate)) &&
        FindAdaptiveCorpsePhysicsActionId(
            PhysicsAction, GetAdaptiveCorpseActionId(Candidate)) == -1)
    {
        Candidate.Mesh.PutRigidBodyToSleep();
        if (Candidate.Mesh.RigidBodyIsAwake())
        {
            return false;
        }
        Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true;
        Candidate.Mesh.bNoSkeletonUpdate = true;
        if (Tier >= 3)
        {
            EntryIndex = FindAdaptiveCorpseLodEntry(Candidate);
            Candidate.SetPhysics(PHYS_None);
            if (Candidate.Physics != PHYS_None)
            {
                Candidate.Mesh.WakeRigidBody();
                return false;
            }
            AdaptiveCorpseLodPhysicsFrozen[EntryIndex] = true;
        }
        if (!RegisterAdaptiveCorpsePhysicsAction(Candidate, PhysicsAction))
        {
            if (Tier >= 3 && Candidate.Physics == PHYS_None)
            {
                Candidate.SetPhysics(PHYS_RigidBody);
                if (Candidate.Physics != PHYS_RigidBody)
                {
                    `log("KF2OPT_CORPSE_AGING state=rollback_failed"$
                         " action=freeze corpse_id="$
                         GetAdaptiveCorpseActionId(Candidate)$
                         " physics=none readback=failed");
                    return false;
                }
                AdaptiveCorpseLodPhysicsFrozen[EntryIndex] = false;
            }
            Candidate.Mesh.WakeRigidBody();
            `log("KF2OPT_CORPSE_AGING state=tracking_full capacity=8192"$
                 " action=cancelled effective_awake="$
                 (Candidate.Mesh.RigidBodyIsAwake() ? 1 : 0));
            return false;
        }
        ++AdaptiveAgingPhysicsSleeps;
        ++AdaptiveCorpsesSlept;
        ++AdaptiveCorpseAgingReductions;
        RegisterAdaptiveCorpseDebugMarker(Candidate, "AGE_SLEEP_"$Tier);
        `log("KF2OPT_CORPSE_AGING state=applied action="$
             (Tier >= 3 ? "freeze" : "sleep")$" tier="$Tier$
             " reductions="$AdaptiveCorpseAgingReductions$" age_ms="$
             int(CorpseAge * 1000.0)$" corpse_id="$
             GetAdaptiveCorpseActionId(Candidate)$" distance_units="$
             DistanceUnits$" distance_m="$
             FormatAdaptiveCorpseDistanceMeters(DistanceUnits, false)$
             " recently_rendered="$(bRecentlyRendered ? 1 : 0)$
             " effective_awake=0 physics="$
             (Candidate.Physics == PHYS_None ? "none" : "rigid_body")$
             " readback=verified");
        if (DebugNativeWakeTest != None && !DebugNativeWakeTest.bDeleteMe)
        {
            DebugNativeWakeTest.ObserveSleep(
                Candidate, Tier >= 3 ? "aging_freeze" : "aging");
        }
        return true;
    }

    if (!Candidate.Mesh.RigidBodyIsAwake() &&
        (!Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep ||
         !Candidate.Mesh.bNoSkeletonUpdate))
    {
        Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true;
        Candidate.Mesh.bNoSkeletonUpdate = true;
        ++AdaptiveSkeletonReductions;
        ++AdaptiveCorpseAgingReductions;
        `log("KF2OPT_CORPSE_AGING state=applied action=skeleton tier="$Tier$
             " reductions="$AdaptiveCorpseAgingReductions$" age_ms="$
             int(CorpseAge * 1000.0)$" corpse_id="$
             GetAdaptiveCorpseActionId(Candidate)$" distance_units="$
             DistanceUnits$" distance_m="$
             FormatAdaptiveCorpseDistanceMeters(DistanceUnits, false)$
             " recently_rendered="$(bRecentlyRendered ? 1 : 0)$
             " readback=verified");
        return true;
    }

    if (Candidate.Mesh.SkeletalMesh == None ||
        Candidate.Mesh.SkeletalMesh.LODInfo.Length < 2 ||
        Candidate.Mesh.ForcedLodModel != 0 ||
        (Tier < 3 && DistanceUnits < 300) ||
        (Tier >= 3 && !bFinalTierInteractionSafe))
    {
        return false;
    }
    MaximumMinLod = Candidate.Mesh.SkeletalMesh.LODInfo.Length - 1;
    TargetMinLod = Tier >= 3 ? MaximumMinLod :
        Tier == 2 ? Min(3, MaximumMinLod) : Min(1, MaximumMinLod);
    if (Candidate.Mesh.MinLodModel >= TargetMinLod)
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
        AdaptiveCorpseLodPersistentAging.AddItem(true);
        AdaptiveCorpseLodPhysicsFrozen.AddItem(false);
    }
    else if (Candidate.Mesh.MinLodModel !=
             AdaptiveCorpseLodAppliedMinModels[EntryIndex])
    {
        RemoveAdaptiveCorpseLodEntry(EntryIndex, false);
        return false;
    }
    else
    {
        AdaptiveCorpseLodAppliedMinModels[EntryIndex] = TargetMinLod;
        AdaptiveCorpseLodPersistentAging[EntryIndex] = true;
    }
    Candidate.Mesh.MinLodModel = TargetMinLod;
    if (Candidate.Mesh.MinLodModel != TargetMinLod)
    {
        RemoveAdaptiveCorpseLodEntry(EntryIndex, false);
        return false;
    }
    ++AdaptiveCorpseLodReductions;
    ++AdaptiveCorpseAgingReductions;
    RegisterAdaptiveCorpseDebugMarker(Candidate, "AGE_LOD_"$Tier);
    `log("KF2OPT_CORPSE_AGING state=applied action=lod tier="$Tier$
         " target_lod="$TargetMinLod$" reductions="$
         AdaptiveCorpseAgingReductions$" age_ms="$
         int(CorpseAge * 1000.0)$" corpse_id="$
         GetAdaptiveCorpseActionId(Candidate)$" distance_units="$
         DistanceUnits$" distance_m="$
         FormatAdaptiveCorpseDistanceMeters(DistanceUnits, false)$
         " recently_rendered="$(bRecentlyRendered ? 1 : 0)$
         " readback=verified");
    return true;
}

function AdaptiveCorpseAgingControl()
{
    local KFGoreManager GoreManager;
    local KFGameInfo GameInfo;

    if (!bAdaptiveCorpseStagger || !bAdaptiveRuntimeEnabled ||
        WorldInfo == None || WorldInfo.NetMode != NM_Standalone)
    {
        return;
    }
    GameInfo = KFGameInfo(WorldInfo.Game);
    if (GameInfo != None && GameInfo.IsZedTimeActive())
    {
        return;
    }
    GoreManager = KFGoreManager(WorldInfo.MyGoreEffectManager);
    if (GoreManager == None || !bAdaptiveCorpseStaggerInitialized ||
        AdaptiveCorpseManager != GoreManager)
    {
        return;
    }
    ApplyOneAdaptiveCorpseAging(GoreManager);
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

function bool RemoveAdaptiveCorpseLodEntry(int Index, bool bRestore)
{
    local KFPawn Candidate;

    if (Index < 0 || Index >= AdaptiveCorpseLodCorpses.Length ||
        Index >= AdaptiveCorpseLodOriginalMinModels.Length ||
        Index >= AdaptiveCorpseLodAppliedMinModels.Length ||
        Index >= AdaptiveCorpseLodPersistentAging.Length ||
        Index >= AdaptiveCorpseLodPhysicsFrozen.Length)
    {
        return false;
    }
    Candidate = AdaptiveCorpseLodCorpses[Index];
    if (bRestore && AdaptiveCorpseLodPhysicsFrozen[Index] &&
        Candidate != None && !Candidate.bDeleteMe)
    {
        if (Candidate.Physics == PHYS_None)
        {
            Candidate.SetPhysics(PHYS_RigidBody);
        }
        if (Candidate.Physics != PHYS_RigidBody)
        {
            return false;
        }
        if (!UnregisterAdaptiveCorpsePhysicsAction(
                Candidate, "aging_freeze"))
        {
            return false;
        }
        if (Candidate.Mesh != None)
        {
            Candidate.Mesh.bNoSkeletonUpdate = false;
        }
    }
    if (bRestore && Candidate != None && !Candidate.bDeleteMe &&
        Candidate.Mesh != None &&
        Candidate.Mesh.MinLodModel == AdaptiveCorpseLodAppliedMinModels[Index])
    {
        Candidate.Mesh.MinLodModel =
            AdaptiveCorpseLodOriginalMinModels[Index];
        if (Candidate.Mesh.MinLodModel !=
            AdaptiveCorpseLodOriginalMinModels[Index])
        {
            return false;
        }
        ++AdaptiveCorpseLodRestores;
    }
    AdaptiveCorpseLodCorpses.Remove(Index, 1);
    AdaptiveCorpseLodOriginalMinModels.Remove(Index, 1);
    AdaptiveCorpseLodAppliedMinModels.Remove(Index, 1);
    AdaptiveCorpseLodPersistentAging.Remove(Index, 1);
    AdaptiveCorpseLodPhysicsFrozen.Remove(Index, 1);
    return true;
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
        else if (AdaptiveCorpseLodPhysicsFrozen[Index] &&
                 FindAdaptiveCorpsePhysicsActionId(
                     "aging_freeze",
                     GetAdaptiveCorpseActionId(Candidate)) == -1)
        {
            // A failed action registration may already have reached
            // PHYS_None. Retain ownership until the verified restore succeeds.
            RemoveAdaptiveCorpseLodEntry(Index, true);
        }
        else if (Candidate.Mesh.MinLodModel !=
                 AdaptiveCorpseLodAppliedMinModels[Index])
        {
            // A different system changed the value. Stop owning it instead of
            // overwriting an external decision during restore. A final frozen
            // actor must still pass the verified physics-release path.
            RemoveAdaptiveCorpseLodEntry(
                Index, AdaptiveCorpseLodPhysicsFrozen[Index]);
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
        if (!AdaptiveCorpseLodPersistentAging[Index] &&
            DistanceSquared < 62500.0)
        {
            RemoveAdaptiveCorpseLodEntry(Index, true);
        }
    }
}

function RestoreOneAdaptiveCorpseLod()
{
    local int Index;

    if (bAdaptiveRuntimeEnabled)
    {
        ClearTimer(nameof(RestoreOneAdaptiveCorpseLod), self);
        return;
    }
    Index = AdaptiveCorpseLodCorpses.Length - 1;
    if (Index < 0)
    {
        ClearTimer(nameof(RestoreOneAdaptiveCorpseLod), self);
        `log("KF2OPT_CORPSE_AGING state=release_complete");
        return;
    }
    RemoveAdaptiveCorpseLodEntry(Index, true);
}

function BeginAdaptiveCorpseLodRelease()
{
    ClearTimer(nameof(RestoreOneAdaptiveCorpseLod), self);
    if (AdaptiveCorpseLodCorpses.Length == 0)
    {
        return;
    }
    `log("KF2OPT_CORPSE_AGING state=release_started tracked="$
         AdaptiveCorpseLodCorpses.Length$" interval_ms=50");
    SetTimer(0.05, true, nameof(RestoreOneAdaptiveCorpseLod), self);
}

function int FindAdaptiveDistanceSleptCorpse(KFPawn Candidate)
{
    local int Index;

    for (Index = 0; Index < AdaptiveDistanceSleptCorpses.Length; ++Index)
    {
        if (AdaptiveDistanceSleptCorpses[Index].Corpse == Candidate)
        {
            return Index;
        }
    }
    return -1;
}

function bool EnsureAdaptiveDistanceSleepTransitions()
{
    if (AdaptiveDistanceSleepTransitions.Length == 0)
    {
        AdaptiveDistanceSleepTransitions.Length = 8192;
    }
    return AdaptiveDistanceSleepTransitions.Length == 8192;
}

function int FindAdaptiveDistanceSleepTransition(string CorpseId)
{
    local int Probe;
    local int Slot;

    if (CorpseId == "" || CorpseId == "none" ||
        AdaptiveDistanceSleepTransitions.Length != 8192)
    {
        return -1;
    }
    Slot = GetAdaptiveCorpsePhysicsActionHash("distance:"$CorpseId);
    for (Probe = 0; Probe < 8192; ++Probe)
    {
        if (AdaptiveDistanceSleepTransitions[Slot].CorpseId == CorpseId)
        {
            return Slot;
        }
        if (AdaptiveDistanceSleepTransitions[Slot].CorpseId == "" &&
            !AdaptiveDistanceSleepTransitions[Slot].bReusable)
        {
            return -1;
        }
        Slot = (Slot + 1) & 8191;
    }
    return -1;
}

function int FindOrAddAdaptiveDistanceSleepTransition(string CorpseId)
{
    local int FirstReusableSlot;
    local int Probe;
    local int Slot;

    if (CorpseId == "" || CorpseId == "none" ||
        !EnsureAdaptiveDistanceSleepTransitions())
    {
        return -1;
    }
    FirstReusableSlot = -1;
    Slot = GetAdaptiveCorpsePhysicsActionHash("distance:"$CorpseId);
    for (Probe = 0; Probe < 8192; ++Probe)
    {
        if (AdaptiveDistanceSleepTransitions[Slot].CorpseId == CorpseId)
        {
            return Slot;
        }
        if (AdaptiveDistanceSleepTransitions[Slot].CorpseId == "")
        {
            if (AdaptiveDistanceSleepTransitions[Slot].bReusable)
            {
                if (FirstReusableSlot < 0)
                {
                    FirstReusableSlot = Slot;
                }
                Slot = (Slot + 1) & 8191;
                continue;
            }
            if (FirstReusableSlot >= 0)
            {
                Slot = FirstReusableSlot;
            }
            AdaptiveDistanceSleepTransitions[Slot].CorpseId = CorpseId;
            AdaptiveDistanceSleepTransitions[Slot].bReusable = false;
            ++AdaptiveDistanceSleepTransitionCount;
            return Slot;
        }
        Slot = (Slot + 1) & 8191;
    }
    if (FirstReusableSlot >= 0)
    {
        AdaptiveDistanceSleepTransitions[FirstReusableSlot].CorpseId = CorpseId;
        AdaptiveDistanceSleepTransitions[FirstReusableSlot].bReusable = false;
        ++AdaptiveDistanceSleepTransitionCount;
        return FirstReusableSlot;
    }
    return -1;
}

function ClearAdaptiveDistanceSleepTransition(int Index)
{
    if (Index < 0 || Index >= AdaptiveDistanceSleepTransitions.Length ||
        AdaptiveDistanceSleepTransitions[Index].CorpseId == "")
    {
        return;
    }
    AdaptiveDistanceSleepTransitions[Index].CorpseId = "";
    AdaptiveDistanceSleepTransitions[Index].RemovalReason = "";
    AdaptiveDistanceSleepTransitions[Index].NativeWakeObservedRealTime = 0.0;
    AdaptiveDistanceSleepTransitions[Index].NativeWakeCount = 0;
    AdaptiveDistanceSleepTransitions[Index].NativeWakeCooldownUntilRealTime = 0.0;
    AdaptiveDistanceSleepTransitions[Index].ExpiresRealTime = 0.0;
    AdaptiveDistanceSleepTransitions[Index].bReusable = true;
    AdaptiveDistanceSleepTransitionCount = Max(
        0, AdaptiveDistanceSleepTransitionCount - 1);
    bAdaptiveDistanceSleepTransitionFullLogged = false;
}

function PruneAdaptiveDistanceSleepTransitions()
{
    local int Index;
    local int Slot;

    if (AdaptiveDistanceSleepTransitions.Length != 8192)
    {
        return;
    }
    // Reclaim at most 64 expired records per control pass. Tombstones keep
    // open-addressing lookup correct while the bounded sweep avoids a spike.
    for (Index = 0; Index < 64; ++Index)
    {
        Slot = AdaptiveDistanceSleepTransitionPruneCursor;
        AdaptiveDistanceSleepTransitionPruneCursor =
            (AdaptiveDistanceSleepTransitionPruneCursor + 1) & 8191;
        if (AdaptiveDistanceSleepTransitions[Slot].CorpseId != "" &&
            AdaptiveDistanceSleepTransitions[Slot].RemovalReason != "tracked" &&
            AdaptiveDistanceSleepTransitions[Slot].ExpiresRealTime > 0.0 &&
            AdaptiveDistanceSleepTransitions[Slot].ExpiresRealTime <=
                WorldInfo.RealTimeSeconds)
        {
            ClearAdaptiveDistanceSleepTransition(Slot);
        }
    }
}

function float GetAdaptiveNativeWakeBackoffSeconds(int NativeWakeCount)
{
    if (NativeWakeCount <= 1)
    {
        return 2.0;
    }
    if (NativeWakeCount == 2)
    {
        return 5.0;
    }
    if (NativeWakeCount == 3)
    {
        return 10.0;
    }
    if (NativeWakeCount == 4)
    {
        return 20.0;
    }
    return 30.0;
}

function string GetAdaptiveDistanceSleepTransitionReason(string CorpseId)
{
    local int Index;

    Index = FindAdaptiveDistanceSleepTransition(CorpseId);
    if (Index < 0)
    {
        return "";
    }
    return AdaptiveDistanceSleepTransitions[Index].RemovalReason;
}

function bool RememberAdaptiveDistanceSleepTransition(
    string CorpseId, string RemovalReason, int DistanceUnits)
{
    local float BackoffSeconds;
    local int Index;

    Index = FindOrAddAdaptiveDistanceSleepTransition(CorpseId);
    if (Index < 0)
    {
        return false;
    }
    AdaptiveDistanceSleepTransitions[Index].RemovalReason = RemovalReason;
    if (RemovalReason == "native_wake")
    {
        if (AdaptiveDistanceSleepTransitions[Index].ExpiresRealTime <=
            WorldInfo.RealTimeSeconds)
        {
            AdaptiveDistanceSleepTransitions[Index].NativeWakeCount = 0;
        }
        AdaptiveDistanceSleepTransitions[Index].NativeWakeCount = Min(
            5, AdaptiveDistanceSleepTransitions[Index].NativeWakeCount + 1);
        BackoffSeconds = GetAdaptiveNativeWakeBackoffSeconds(
            AdaptiveDistanceSleepTransitions[Index].NativeWakeCount);
        AdaptiveDistanceSleepTransitions[Index].NativeWakeObservedRealTime =
            WorldInfo.RealTimeSeconds;
        AdaptiveDistanceSleepTransitions[Index].NativeWakeCooldownUntilRealTime =
            WorldInfo.RealTimeSeconds + BackoffSeconds;
        AdaptiveDistanceSleepTransitions[Index].ExpiresRealTime =
            WorldInfo.RealTimeSeconds + 60.0;
    }
    else
    {
        AdaptiveDistanceSleepTransitions[Index].NativeWakeCount = 0;
        AdaptiveDistanceSleepTransitions[Index].NativeWakeObservedRealTime = 0.0;
        AdaptiveDistanceSleepTransitions[Index].NativeWakeCooldownUntilRealTime =
            0.0;
        AdaptiveDistanceSleepTransitions[Index].ExpiresRealTime =
            WorldInfo.RealTimeSeconds + 30.0;
        if (RemovalReason == "deleted" || RemovalReason == "reused" ||
            RemovalReason == "invalidated" ||
            RemovalReason == "removed_from_pool")
        {
            ClearAdaptiveDistanceSleepTransition(Index);
        }
    }
    return true;
}

function bool DeferAdaptiveDistanceResleepAfterNativeWake(KFPawn Candidate)
{
    local int Index;

    Index = FindAdaptiveDistanceSleepTransition(
        GetAdaptiveCorpseActionId(Candidate));
    if (Index < 0 ||
        AdaptiveDistanceSleepTransitions[Index].RemovalReason !=
            "native_wake")
    {
        return false;
    }
    // A native wake is an ownership signal. Back off only this full actor ID;
    // all other eligible corpses remain available to Distance-Sleep. The
    // absolute deadline cannot be extended by missing distance telemetry.
    return WorldInfo.RealTimeSeconds <
        AdaptiveDistanceSleepTransitions[Index].NativeWakeCooldownUntilRealTime;
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

function int GetAdaptiveCorpsePhysicsActionHash(string ActionId)
{
    local int Index;
    local int HashValue;

    HashValue = 5381;
    for (Index = 0; Index < Len(ActionId); ++Index)
    {
        HashValue = ((HashValue << 5) + HashValue) ^
            Asc(Mid(ActionId, Index, 1));
    }
    return HashValue & 8191;
}

function bool EnsureAdaptiveCorpsePhysicsActionIds()
{
    if (AdaptiveCorpsePhysicsActionIds.Length == 0)
    {
        // Baseline and pressure-driven Ragdoll ownership share one table with
        // distinct action prefixes. Stable strings avoid retaining deleted
        // Pawn objects while the power-of-two table keeps lookup bounded.
        AdaptiveCorpsePhysicsActionIds.Length = 8192;
    }
    return AdaptiveCorpsePhysicsActionIds.Length == 8192;
}

function int FindAdaptiveCorpsePhysicsActionId(
    string Action, string CorpseId)
{
    local int Probe;
    local int Slot;
    local string ActionId;

    if (Action == "" || CorpseId == "" ||
        AdaptiveCorpsePhysicsActionIds.Length != 8192)
    {
        return -1;
    }
    ActionId = Action$":"$CorpseId;
    Slot = GetAdaptiveCorpsePhysicsActionHash(ActionId);
    for (Probe = 0; Probe < 8192; ++Probe)
    {
        if (AdaptiveCorpsePhysicsActionIds[Slot] == ActionId)
        {
            return Slot;
        }
        if (AdaptiveCorpsePhysicsActionIds[Slot] == "")
        {
            return -1;
        }
        Slot = (Slot + 1) & 8191;
    }
    return -1;
}

function bool RegisterAdaptiveCorpsePhysicsAction(
    KFPawn Candidate, string Action)
{
    local int DeletedSlot;
    local int Probe;
    local int Slot;
    local string CorpseId;
    local string ActionId;

    if (Candidate == None || Action == "" ||
        !EnsureAdaptiveCorpsePhysicsActionIds())
    {
        return false;
    }
    CorpseId = GetAdaptiveCorpseActionId(Candidate);
    ActionId = Action$":"$CorpseId;
    Slot = GetAdaptiveCorpsePhysicsActionHash(ActionId);
    DeletedSlot = -1;
    for (Probe = 0; Probe < 8192; ++Probe)
    {
        if (AdaptiveCorpsePhysicsActionIds[Slot] == ActionId)
        {
            return true;
        }
        if (AdaptiveCorpsePhysicsActionIds[Slot] == "__deleted__" &&
            DeletedSlot < 0)
        {
            DeletedSlot = Slot;
        }
        else if (AdaptiveCorpsePhysicsActionIds[Slot] == "")
        {
            if (DeletedSlot >= 0)
            {
                Slot = DeletedSlot;
            }
            AdaptiveCorpsePhysicsActionIds[Slot] = ActionId;
            ++AdaptiveCorpsePhysicsActionIdCount;
            return true;
        }
        Slot = (Slot + 1) & 8191;
    }
    if (DeletedSlot >= 0)
    {
        AdaptiveCorpsePhysicsActionIds[DeletedSlot] = ActionId;
        ++AdaptiveCorpsePhysicsActionIdCount;
        return true;
    }
    // Never evict an owned ID: eviction would make an old corpse eligible for
    // repeated work. A saturated table disables only further aging and
    // Ragdoll ownership; distance, LOD and capacity continue independently.
    return false;
}

function bool UnregisterAdaptiveCorpsePhysicsAction(
    KFPawn Candidate, string Action)
{
    local int Slot;

    if (Candidate == None || Action == "")
    {
        return false;
    }
    Slot = FindAdaptiveCorpsePhysicsActionId(
        Action, GetAdaptiveCorpseActionId(Candidate));
    if (Slot < 0)
    {
        // Idempotent release: an already-cleared action must not prevent the
        // actor and its LOD ownership from completing restore.
        return true;
    }
    // Open-addressing lookup must continue through removed slots. Registration
    // reuses the first tombstone it encounters, so repeated Adaptive
    // off/on cycles do not consume the fixed table.
    AdaptiveCorpsePhysicsActionIds[Slot] = "__deleted__";
    AdaptiveCorpsePhysicsActionIdCount =
        Max(0, AdaptiveCorpsePhysicsActionIdCount - 1);
    return true;
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

function string FormatAdaptiveCorpseDistanceMeters(
    int DistanceUnits, optional bool IncludeUnit)
{
    local int DistanceDecimeters;
    local string Result;

    if (DistanceUnits < 0)
    {
        return IncludeUnit ? "unknown" : "-1";
    }
    // KF2's own SDK converts world distance to metres by dividing Unreal
    // units by 100. Keep one decimal place while avoiding locale-dependent
    // float formatting in logs and debug markers.
    DistanceDecimeters = (DistanceUnits + 5) / 10;
    Result = string(DistanceDecimeters / 10)$"."$
        string(DistanceDecimeters % 10);
    if (IncludeUnit)
    {
        Result $= " m";
    }
    return Result;
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

function string FormatAdaptiveDebugMarkerId(string FullId)
{
    // Keep the searchable actor/time suffix on screen. The complete ID remains
    // in every APPLIED receipt and telemetry log entry.
    if (FullId == "none")
    {
        return "#?";
    }
    if (Len(FullId) > 12)
    {
        return "#"$Right(FullId, 12);
    }
    return "#"$FullId;
}

function string FormatAdaptiveDebugMarkerAction(string Action)
{
    if (Left(Action, 8) == "AGE_LOD_")
    {
        return "LOD"$Mid(Action, 8);
    }
    if (Left(Action, 4) == "LOD_")
    {
        return "LOD"$Mid(Action, 4);
    }
    if (Left(Action, 10) == "AGE_SLEEP_")
    {
        return "SLEEP"$Mid(Action, 10);
    }
    if (Action == "DIST_SLEEP")
    {
        return "DIST SLEEP";
    }
    if (Action == "RAGDOLL_SLEEP")
    {
        return "RAGDOLL";
    }
    return Action;
}

function float GetAdaptiveDebugMarkerTextScale(
    Canvas MarkerCanvas, int DistanceUnits)
{
    local float DistanceScale;
    local float ResolutionScale;

    if (MarkerCanvas == None)
    {
        return 0.85;
    }
    // Canvas text uses pixels. Compensate for high-resolution displays so a
    // marker keeps roughly the same physical size at 1080p, 1440p and 4K.
    ResolutionScale = MarkerCanvas.ClipY / 1080.0;
    if (ResolutionScale < 1.0)
    {
        ResolutionScale = 1.0;
    }
    else if (ResolutionScale > 2.0)
    {
        ResolutionScale = 2.0;
    }

    DistanceScale = 0.85;
    if (DistanceUnits >= 4000)
    {
        DistanceScale = 1.05;
    }
    else if (DistanceUnits >= 2000)
    {
        DistanceScale = 0.95;
    }
    return DistanceScale * ResolutionScale;
}

function bool ReserveAdaptiveDebugMarkerScreenPosition(
    Canvas MarkerCanvas, string Label, float TextScale,
    vector AnchorPosition, out vector ScreenPosition)
{
    local bool bOverlaps;
    local float LabelWidth;
    local float LabelHeight;
    local float VerticalOffset;
    local int Attempt;
    local int Index;
    local int NewIndex;

    if (MarkerCanvas == None)
    {
        return false;
    }
    if (AdaptiveDebugMarkerScreenEntries.Length >= 10)
    {
        return false;
    }
    MarkerCanvas.Font = class'Engine'.Static.GetSmallFont();
    MarkerCanvas.TextSize(
        Label, LabelWidth, LabelHeight, TextScale, TextScale);
    LabelWidth += 8.0;
    LabelHeight += 4.0;

    // Keep a marker close to its actor, but alternate below and above its
    // anchor when another marker already occupies that screen rectangle.
    // Debug markers that cannot find a nearby free slot are skipped instead
    // of turning dense combat into an unreadable wall of text.
    for (Attempt = 0; Attempt < 9; ++Attempt)
    {
        if (Attempt == 0)
        {
            VerticalOffset = 0.0;
        }
        else if ((Attempt % 2) == 1)
        {
            VerticalOffset = float((Attempt + 1) / 2) * LabelHeight;
        }
        else
        {
            VerticalOffset = -float(Attempt / 2) * LabelHeight;
        }
        ScreenPosition.X = AnchorPosition.X - LabelWidth * 0.5;
        ScreenPosition.Y = AnchorPosition.Y - LabelHeight + VerticalOffset;
        if (ScreenPosition.X < 4.0)
        {
            ScreenPosition.X = 4.0;
        }
        else if (ScreenPosition.X + LabelWidth > MarkerCanvas.ClipX - 4.0)
        {
            ScreenPosition.X = MarkerCanvas.ClipX - LabelWidth - 4.0;
        }
        if (ScreenPosition.Y < 4.0 ||
            ScreenPosition.Y + LabelHeight > MarkerCanvas.ClipY - 4.0)
        {
            continue;
        }

        bOverlaps = false;
        for (Index = 0;
             Index < AdaptiveDebugMarkerScreenEntries.Length; ++Index)
        {
            if (ScreenPosition.X <
                    AdaptiveDebugMarkerScreenEntries[Index].X +
                    AdaptiveDebugMarkerScreenEntries[Index].Width &&
                ScreenPosition.X + LabelWidth >
                    AdaptiveDebugMarkerScreenEntries[Index].X &&
                ScreenPosition.Y <
                    AdaptiveDebugMarkerScreenEntries[Index].Y +
                    AdaptiveDebugMarkerScreenEntries[Index].Height &&
                ScreenPosition.Y + LabelHeight >
                    AdaptiveDebugMarkerScreenEntries[Index].Y)
            {
                bOverlaps = true;
                break;
            }
        }
        if (!bOverlaps)
        {
            NewIndex = AdaptiveDebugMarkerScreenEntries.Length;
            AdaptiveDebugMarkerScreenEntries.Length = NewIndex + 1;
            AdaptiveDebugMarkerScreenEntries[NewIndex].X = ScreenPosition.X;
            AdaptiveDebugMarkerScreenEntries[NewIndex].Y = ScreenPosition.Y;
            AdaptiveDebugMarkerScreenEntries[NewIndex].Width = LabelWidth;
            AdaptiveDebugMarkerScreenEntries[NewIndex].Height = LabelHeight;
            return true;
        }
    }
    return false;
}

function DrawAdaptiveDebugMarkerLabel(
    Canvas MarkerCanvas, string Label, vector AnchorPosition,
    int Red, int Green, int Blue, float TextScale)
{
    local int EntryIndex;
    local FontRenderInfo MarkerFontRenderInfo;
    local float OutlineOffset;
    local vector ScreenPosition;

    if (!ReserveAdaptiveDebugMarkerScreenPosition(
            MarkerCanvas, Label, TextScale, AnchorPosition, ScreenPosition))
    {
        return;
    }
    EntryIndex = AdaptiveDebugMarkerScreenEntries.Length - 1;
    MarkerFontRenderInfo =
        MarkerCanvas.CreateFontRenderInfo(false, true);
    MarkerCanvas.SetDrawColor(0, 0, 0, 190);
    MarkerCanvas.SetPos(ScreenPosition.X - 4.0, ScreenPosition.Y - 2.0);
    MarkerCanvas.DrawRect(
        AdaptiveDebugMarkerScreenEntries[EntryIndex].Width,
        AdaptiveDebugMarkerScreenEntries[EntryIndex].Height);
    OutlineOffset = 1.25 * TextScale;
    MarkerCanvas.SetDrawColor(0, 0, 0, 235);
    MarkerCanvas.SetPos(
        ScreenPosition.X - OutlineOffset, ScreenPosition.Y);
    MarkerCanvas.DrawText(Label, false, TextScale, TextScale);
    MarkerCanvas.SetPos(
        ScreenPosition.X + OutlineOffset, ScreenPosition.Y);
    MarkerCanvas.DrawText(Label, false, TextScale, TextScale);
    MarkerCanvas.SetPos(
        ScreenPosition.X, ScreenPosition.Y - OutlineOffset);
    MarkerCanvas.DrawText(Label, false, TextScale, TextScale);
    MarkerCanvas.SetPos(
        ScreenPosition.X, ScreenPosition.Y + OutlineOffset);
    MarkerCanvas.DrawText(Label, false, TextScale, TextScale);
    MarkerCanvas.SetDrawColor(Red, Green, Blue, 255);
    MarkerCanvas.SetPos(ScreenPosition.X, ScreenPosition.Y);
    MarkerCanvas.DrawText(
        Label, false, TextScale, TextScale, MarkerFontRenderInfo);
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
    local string MarkerLabel;
    local int MarkerRed;
    local int MarkerGreen;
    local int MarkerBlue;
    local int DistanceUnits;
    local float MarkerTextScale;

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
    for (Index = AdaptiveCorpseDebugMarkers.Length - 1; Index >= 0; --Index)
    {
        if (AdaptiveDebugMarkerScreenEntries.Length >= 5)
        {
            break;
        }
        Candidate = AdaptiveCorpseDebugMarkers[Index].Corpse;
        if (Candidate == None || Candidate.Mesh == None)
        {
            continue;
        }
        MarkerLocation = Candidate.Location;
        MarkerLocation.Z += 72.0;
        if (Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.2 ||
            !LocalPC.FastTrace(MarkerLocation, ViewLocation))
        {
            continue;
        }
        ScreenPosition = MarkerCanvas.Project(MarkerLocation);
        if (ScreenPosition.X < 8.0 || ScreenPosition.X > MarkerCanvas.ClipX - 8.0 ||
            ScreenPosition.Y < 8.0 || ScreenPosition.Y > MarkerCanvas.ClipY - 8.0)
        {
            continue;
        }
        if (AdaptiveCorpseDebugMarkers[Index].Action == "WAKE")
        {
            MarkerRed = 80;
            MarkerGreen = 255;
            MarkerBlue = 120;
        }
        else if (AdaptiveCorpseDebugMarkers[Index].Action == "DIST_SLEEP")
        {
            MarkerRed = 80;
            MarkerGreen = 200;
            MarkerBlue = 255;
        }
        else
        {
            MarkerRed = 255;
            MarkerGreen = 170;
            MarkerBlue = 60;
        }
        DistanceUnits = GetAdaptiveCorpseDistanceUnits(Candidate);
        MarkerTextScale = GetAdaptiveDebugMarkerTextScale(
            MarkerCanvas, DistanceUnits);
        MarkerLabel =
            FormatAdaptiveDebugMarkerAction(
                AdaptiveCorpseDebugMarkers[Index].Action)$" "$
            FormatAdaptiveCorpseDistanceMeters(
                DistanceUnits, true)$" "$
            FormatAdaptiveDebugMarkerId(
                AdaptiveCorpseDebugMarkers[Index].CorpseId);
        DrawAdaptiveDebugMarkerLabel(
            MarkerCanvas, MarkerLabel, ScreenPosition,
            MarkerRed, MarkerGreen, MarkerBlue, MarkerTextScale);
    }
}

function int GetAdaptiveCorpseEffectiveAwake(KFPawn Candidate)
{
    if (Candidate == None || Candidate.Mesh == None)
    {
        return -1;
    }
    return Candidate.Mesh.RigidBodyIsAwake() ? 1 : 0;
}

function bool IsAdaptiveCorpseInPool(KFPawn Candidate)
{
    if (Candidate == None || AdaptiveCorpseManager == None)
    {
        return false;
    }
    // Dynamic-array Find is native. Avoid a script-level nested scan while
    // pruning a large tracked corpse set every control pass.
    return AdaptiveCorpseManager.CorpsePool.Find(Candidate) >= 0;
}

function RemoveAdaptiveDistanceSleptCorpseEntry(
    int Index, string RemovalReason)
{
    local string BackoffFields;
    local int DistanceUnits;
    local int EffectiveAwake;
    local int TransitionIndex;
    local string CorpseId;
    local KFPawn Candidate;

    if (Index >= 0 && Index < AdaptiveDistanceSleptCorpses.Length)
    {
        Candidate = AdaptiveDistanceSleptCorpses[Index].Corpse;
        CorpseId = AdaptiveDistanceSleptCorpses[Index].CorpseId;
        DistanceUnits = GetAdaptiveCorpseDistanceUnits(Candidate);
        EffectiveAwake = GetAdaptiveCorpseEffectiveAwake(Candidate);
        RememberAdaptiveDistanceSleepTransition(
            CorpseId, RemovalReason, DistanceUnits);
        if (RemovalReason == "native_wake")
        {
            TransitionIndex = FindAdaptiveDistanceSleepTransition(CorpseId);
            if (TransitionIndex >= 0)
            {
                BackoffFields = " native_wake_count="$
                    AdaptiveDistanceSleepTransitions[TransitionIndex].NativeWakeCount$
                    " resleep_after_ms="$int(FMax(0.0,
                        AdaptiveDistanceSleepTransitions[TransitionIndex].
                            NativeWakeCooldownUntilRealTime -
                            WorldInfo.RealTimeSeconds) * 1000.0);
            }
            // Detection alone cannot identify who woke the body. Preserve
            // the existing reason for compatibility, but label test stimuli.
            if (DebugNativeWakeTest != None && !DebugNativeWakeTest.bDeleteMe &&
                DebugNativeWakeTest.ConsumeInjectedWake(CorpseId))
            {
                BackoffFields = BackoffFields$" wake_origin=injected_test";
            }
            else
            {
                BackoffFields = BackoffFields$" wake_origin=unattributed";
            }
        }
        AdaptiveDistanceSleptCorpses.Remove(Index, 1);
        `log("KF2OPT_CORPSE_DISTANCE state=removed previous_state=sleep"$
             " removal_reason="$RemovalReason$" corpse_id="$CorpseId$
             " distance_units="$DistanceUnits$
             " distance_m="$FormatAdaptiveCorpseDistanceMeters(
                 DistanceUnits, false)$
             " effective_awake="$EffectiveAwake$BackoffFields);
    }
}

function PruneAdaptiveDistanceSleptCorpses()
{
    local int Index;
    local KFPawn Candidate;

    PruneAdaptiveDistanceSleepTransitions();
    for (Index = AdaptiveDistanceSleptCorpses.Length - 1;
         Index >= 0; --Index)
    {
        Candidate = AdaptiveDistanceSleptCorpses[Index].Corpse;
        if (Candidate == None || Candidate.bDeleteMe)
        {
            RemoveAdaptiveDistanceSleptCorpseEntry(Index, "deleted");
        }
        else if (GetAdaptiveCorpseActionId(Candidate) !=
                 AdaptiveDistanceSleptCorpses[Index].CorpseId)
        {
            RemoveAdaptiveDistanceSleptCorpseEntry(Index, "reused");
        }
        else if (KFPawn_Monster(Candidate) == None || Candidate.Mesh == None ||
                 Candidate.TimeOfDeath <= 0.0)
        {
            RemoveAdaptiveDistanceSleptCorpseEntry(Index, "invalidated");
        }
        else if (!IsAdaptiveCorpseInPool(Candidate))
        {
            RemoveAdaptiveDistanceSleptCorpseEntry(
                Index, "removed_from_pool");
        }
        else if (Candidate.Physics != PHYS_RigidBody)
        {
            RemoveAdaptiveDistanceSleptCorpseEntry(
                Index, "physics_changed");
        }
        else if (Candidate.Mesh.RigidBodyIsAwake())
        {
            RemoveAdaptiveDistanceSleptCorpseEntry(Index, "native_wake");
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
        Candidate = AdaptiveDistanceSleptCorpses[Index].Corpse;
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
        RemoveAdaptiveDistanceSleptCorpseEntry(Index, "optimizer_wake");
        if (bWasSleeping)
        {
            `log("KF2OPT_CORPSE_DISTANCE state=wake woken="$
                 AdaptiveDistancePhysicsWakes$" tracked="$
                 AdaptiveDistanceSleptCorpses.Length$" corpse_id="$
                 GetAdaptiveCorpseActionId(Candidate)$" distance_units="$
                 GetAdaptiveCorpseDistanceUnits(Candidate)$
                 " distance_m="$FormatAdaptiveCorpseDistanceMeters(
                     GetAdaptiveCorpseDistanceUnits(Candidate), false)$
                 " effective_awake=1");
        }
    }
    return WakeCount;
}

function int WakeAdaptiveDistanceSleptCorpseBatch()
{
    local int Index;
    local int WakeCount;
    local KFPawn Candidate;

    PruneAdaptiveDistanceSleptCorpses();
    for (Index = AdaptiveDistanceSleptCorpses.Length - 1;
         Index >= 0 && WakeCount < 8; --Index)
    {
        Candidate = AdaptiveDistanceSleptCorpses[Index].Corpse;
        if (Candidate != None && Candidate.Mesh != None &&
            !Candidate.Mesh.RigidBodyIsAwake())
        {
            Candidate.Mesh.WakeRigidBody();
            if (Candidate.Mesh.RigidBodyIsAwake())
            {
                Candidate.Mesh.bNoSkeletonUpdate = false;
                ++AdaptiveDistancePhysicsWakes;
                ++WakeCount;
            }
        }
        RemoveAdaptiveDistanceSleptCorpseEntry(
            Index, "adaptive_disabled");
    }
    if (AdaptiveDistanceSleptCorpses.Length == 0)
    {
        ClearTimer(nameof(WakeAdaptiveDistanceSleptCorpseBatch), self);
        `log("KF2OPT_CORPSE_DISTANCE state=release_complete reason=adaptive_disabled");
    }
    return WakeCount;
}

function BeginAdaptiveDistanceSleepRelease()
{
    if (WakeAdaptiveDistanceSleptCorpseBatch() > 0 &&
        AdaptiveDistanceSleptCorpses.Length > 0)
    {
        SetTimer(0.05, true,
            nameof(WakeAdaptiveDistanceSleptCorpseBatch), self);
    }
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
            FindAdaptiveDistanceSleptCorpse(Candidate) != -1 ||
            DeferAdaptiveDistanceResleepAfterNativeWake(Candidate))
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
    local int EntryIndex;
    local int TransitionIndex;
    local string CorpseId;
    local string PreviousReason;
    local KFPawn Candidate;

    Candidate = SelectDistantAwakeMonsterCorpseForSleep(
        GoreManager, PhysicsPressureLevel);
    if (Candidate == None || Candidate.Mesh == None)
    {
        return false;
    }
    CorpseId = GetAdaptiveCorpseActionId(Candidate);
    TransitionIndex = FindOrAddAdaptiveDistanceSleepTransition(CorpseId);
    if (TransitionIndex < 0)
    {
        if (!bAdaptiveDistanceSleepTransitionFullLogged)
        {
            bAdaptiveDistanceSleepTransitionFullLogged = true;
            `log("KF2OPT_CORPSE_DISTANCE state=tracking_full capacity=8192"$
                 " used="$AdaptiveDistanceSleepTransitionCount$
                 " action=disabled_to_preserve_traceability");
        }
        return false;
    }
    PreviousReason = GetAdaptiveDistanceSleepTransitionReason(CorpseId);
    if (PreviousReason == "tracked")
    {
        PreviousReason = "tracking_lost";
        AdaptiveDistanceSleepTransitions[TransitionIndex].RemovalReason =
            PreviousReason;
        `log("KF2OPT_CORPSE_DISTANCE state=removed previous_state=sleep"$
             " removal_reason=tracking_lost corpse_id="$CorpseId$
             " distance_units="$GetAdaptiveCorpseDistanceUnits(Candidate)$
             " distance_m="$FormatAdaptiveCorpseDistanceMeters(
                 GetAdaptiveCorpseDistanceUnits(Candidate), false)$
             " effective_awake="$
             GetAdaptiveCorpseEffectiveAwake(Candidate));
    }
    Candidate.Mesh.PutRigidBodyToSleep();
    if (Candidate.Mesh.RigidBodyIsAwake())
    {
        return false;
    }
    Candidate.Mesh.bSkipAllUpdateWhenPhysicsAsleep = true;
    Candidate.Mesh.bNoSkeletonUpdate = true;
    EntryIndex = AdaptiveDistanceSleptCorpses.Length;
    AdaptiveDistanceSleptCorpses.Length = EntryIndex + 1;
    AdaptiveDistanceSleptCorpses[EntryIndex].Corpse = Candidate;
    AdaptiveDistanceSleptCorpses[EntryIndex].CorpseId = CorpseId;
    AdaptiveDistanceSleepTransitions[TransitionIndex].RemovalReason =
        "tracked";
    AdaptiveDistanceSleepTransitions[TransitionIndex].ExpiresRealTime =
        WorldInfo.RealTimeSeconds + 60.0;
    ++AdaptiveDistancePhysicsSleeps;
    ++AdaptiveCorpsesSlept;
    RegisterAdaptiveCorpseDebugMarker(Candidate, "DIST_SLEEP");
    if (PreviousReason != "")
    {
        `log("KF2OPT_CORPSE_DISTANCE state=resleep previous_reason="$
             PreviousReason$" physics_level="$PhysicsPressureLevel$
             " frame_level="$AdaptiveCorpsePressureLevel$
             " scene_level="$AdaptiveCorpseScenePressureLevel$
             " quality_steps="$GetAdaptiveCorpseAttackScale()$" slept="$
             AdaptiveDistancePhysicsSleeps$" tracked="$
             AdaptiveDistanceSleptCorpses.Length$" visible_living="$
             VisibleLivingZeds$" visible_corpses="$VisibleCorpses$
             " corpse_id="$CorpseId$" distance_units="$
             GetAdaptiveCorpseDistanceUnits(Candidate)$
             " distance_m="$FormatAdaptiveCorpseDistanceMeters(
                 GetAdaptiveCorpseDistanceUnits(Candidate), false)$
             " effective_awake=0");
    }
    else
    {
        `log("KF2OPT_CORPSE_DISTANCE state=sleep physics_level="$
             PhysicsPressureLevel$" frame_level="$AdaptiveCorpsePressureLevel$
             " scene_level="$AdaptiveCorpseScenePressureLevel$
             " quality_steps="$GetAdaptiveCorpseAttackScale()$" slept="$
             AdaptiveDistancePhysicsSleeps$" tracked="$
             AdaptiveDistanceSleptCorpses.Length$" visible_living="$
             VisibleLivingZeds$" visible_corpses="$VisibleCorpses$
             " corpse_id="$CorpseId$" distance_units="$
             GetAdaptiveCorpseDistanceUnits(Candidate)$
             " distance_m="$FormatAdaptiveCorpseDistanceMeters(
                 GetAdaptiveCorpseDistanceUnits(Candidate), false)$
             " effective_awake=0");
    }
    if (DebugNativeWakeTest != None && !DebugNativeWakeTest.bDeleteMe)
    {
        DebugNativeWakeTest.ObserveSleep(Candidate, "distance");
    }
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
        AdaptiveCorpseLodPersistentAging.AddItem(false);
        AdaptiveCorpseLodPhysicsFrozen.AddItem(false);
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
         GetAdaptiveCorpseDistanceUnits(Candidate)$" distance_m="$
         FormatAdaptiveCorpseDistanceMeters(
             GetAdaptiveCorpseDistanceUnits(Candidate), false)$
         " readback=verified");
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
    KFGoreManager GoreManager, bool SeverePressure,
    int ScenePressureLevel, int EnemyPressureLevel, int FramePressureLevel)
{
    local int Index;
    local float DistanceSquared;
    local float MinimumAge;
    local float MaximumSpeedSquared;
    local float SelectedDeathTime;
    local KFPawn Candidate;
    local KFPawn Selected;
    local PlayerController LocalPC;

    LocalPC = GetALocalPlayerController();
    if (LocalPC == None || LocalPC.Pawn == None)
    {
        return None;
    }
    MinimumAge = SeverePressure ? 0.75 : 1.5;
    MaximumSpeedSquared = SeverePressure ? 360000.0 : 160000.0;
    SelectedDeathTime = WorldInfo.TimeSeconds + 1.0;
    if (!EnsureAdaptiveCorpsePhysicsActionIds() ||
        AdaptiveCorpsePhysicsActionIdCount >= 8192)
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
            FindAdaptiveCorpsePhysicsActionId("ragdoll",
                GetAdaptiveCorpseActionId(Candidate)) != -1 ||
            DeferAdaptiveDistanceResleepAfterNativeWake(Candidate) ||
            Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.3 ||
            !Candidate.Mesh.RigidBodyIsAwake() ||
            VSizeSq(Candidate.Velocity) > MaximumSpeedSquared)
        {
            continue;
        }
        DistanceSquared = VSizeSq(
            Candidate.Location - LocalPC.Pawn.Location);
        // Ragdoll pressure may become more aggressive, but it must never
        // freeze a visible corpse inside the 800-unit interaction radius.
        if (DistanceSquared < 640000.0)
        {
            if (WorldInfo.RealTimeSeconds -
                    AdaptiveLastNearRagdollRejectRealTime >= 2.0)
            {
                AdaptiveLastNearRagdollRejectRealTime =
                    WorldInfo.RealTimeSeconds;
                `log("KF2OPT_CORPSE_RAGDOLL state=rejected_near corpse_id="$
                     GetAdaptiveCorpseActionId(Candidate)$" distance_units="$
                     GetAdaptiveCorpseDistanceUnits(Candidate)$
                     " distance_m="$FormatAdaptiveCorpseDistanceMeters(
                         GetAdaptiveCorpseDistanceUnits(Candidate), false)$
                     " minimum_distance_units=800 minimum_distance_m=8.0"$
                     " scene_level="$
                     ScenePressureLevel$" enemy_level="$EnemyPressureLevel$
                     " frame_level="$FramePressureLevel$" visible=1 age_ms="$
                     int((WorldInfo.TimeSeconds - Candidate.TimeOfDeath) *
                         1000.0)$" zed_time=0 eligible=0 reason=near_player");
            }
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
    KFGoreManager GoreManager, bool SeverePressure, int VisibleAwakeBefore,
    int ScenePressureLevel, int EnemyPressureLevel, int FramePressureLevel)
{
    local KFPawn Candidate;

    Candidate = SelectVisibleAwakeMonsterCorpseForSleep(
        GoreManager, SeverePressure, ScenePressureLevel,
        EnemyPressureLevel, FramePressureLevel);
    // Ragdoll pressure must not bypass the per-actor native-wake backoff.
    // Reuse the absolute deadline without resetting or extending it.
    if (Candidate == None || Candidate.Mesh == None ||
        DeferAdaptiveDistanceResleepAfterNativeWake(Candidate))
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
    if (!RegisterAdaptiveCorpsePhysicsAction(Candidate, "ragdoll"))
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
         AdaptiveCorpsePhysicsActionIdCount$" corpse_id="$
         GetAdaptiveCorpseActionId(Candidate)$" distance_units="$
         GetAdaptiveCorpseDistanceUnits(Candidate)$
         " distance_m="$FormatAdaptiveCorpseDistanceMeters(
             GetAdaptiveCorpseDistanceUnits(Candidate), false)$
         " minimum_distance_units=800 minimum_distance_m=8.0"$
         " scene_level="$ScenePressureLevel$
         " enemy_level="$EnemyPressureLevel$" frame_level="$FramePressureLevel$
         " visible=1 age_ms="$
         int((WorldInfo.TimeSeconds - Candidate.TimeOfDeath) * 1000.0)$
         " zed_time=0 eligible=1 effective_awake=0");
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

    if (!bAdaptiveCorpseStagger || !bAdaptiveRuntimeEnabled ||
        WorldInfo == None ||
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

    PruneAdaptiveDistanceSleptCorpses();
    AttackScale = GetAdaptiveCorpseAttackScale();
    // Proximity is gameplay-critical: wake every matching tracked corpse now,
    // independently of quality level or the number of nearby bodies.
    WakeNearAdaptiveDistanceSleptCorpses();
    PruneAdaptiveCorpseLodEntries();
    RestoreNearAdaptiveCorpseLods();
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
    EnemyPressureLevel = ResolveAdaptiveLivingEnemyPressureLevel(
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
            GoreManager, RagdollPressureLevel >= 2, VisibleAwake,
            ScenePressureLevel, EnemyPressureLevel,
            AdaptiveCorpseCurrentFramePressureLevel))
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
    if (bAdaptiveCorpseStagger && bAdaptiveRuntimeEnabled)
    {
        SetTimer(0.45, true, nameof(StaggerCorpseCleanup), self);
        SetTimer(0.25, true, nameof(AdaptiveCorpseLoadControl), self);
        SetTimer(0.05, true, nameof(AdaptiveCorpseAgingControl), self);
    }
    SampleTelemetry();
}

function int GetProfileSystemMilliseconds()
{
    local int Year;
    local int Month;
    local int DayOfWeek;
    local int Day;
    local int Hour;
    local int Minute;
    local int Second;
    local int Millisecond;

    GetSystemTime(
        Year, Month, DayOfWeek, Day,
        Hour, Minute, Second, Millisecond);
    return (((Hour * 60) + Minute) * 60 + Second) * 1000 + Millisecond;
}

function InsertAdaptiveZedDebugMarkerByDistance(
    vector MarkerLocation, vector MarkerVelocity,
    string ZedId, int DistanceUnits)
{
    local int Index;
    local int LastIndex;

    if (AdaptiveZedDebugMarkers.Length >= 24 &&
        DistanceUnits >= AdaptiveZedDebugMarkers[23].DistanceUnits)
    {
        return;
    }
    if (AdaptiveZedDebugMarkers.Length < 24)
    {
        AdaptiveZedDebugMarkers.Length =
            AdaptiveZedDebugMarkers.Length + 1;
    }
    LastIndex = AdaptiveZedDebugMarkers.Length - 1;
    Index = LastIndex;
    while (Index > 0 &&
           AdaptiveZedDebugMarkers[Index - 1].DistanceUnits > DistanceUnits)
    {
        AdaptiveZedDebugMarkers[Index] =
            AdaptiveZedDebugMarkers[Index - 1];
        --Index;
    }
    AdaptiveZedDebugMarkers[Index].Location = MarkerLocation;
    AdaptiveZedDebugMarkers[Index].Velocity = MarkerVelocity;
    AdaptiveZedDebugMarkers[Index].ZedId = ZedId;
    AdaptiveZedDebugMarkers[Index].DistanceUnits = DistanceUnits;
}

function RefreshAdaptiveZedDebugMarkers()
{
    local KFPawn_Monster Candidate;
    local PlayerController LocalPC;
    local vector ViewLocation;
    local vector MarkerLocation;

    if (!bAdaptiveZedDebugMarkers || WorldInfo == None)
    {
        AdaptiveZedDebugMarkers.Length = 0;
        return;
    }
    if (WorldInfo.RealTimeSeconds < AdaptiveZedDebugRefreshRealTime + 0.10)
    {
        return;
    }
    AdaptiveZedDebugRefreshRealTime = WorldInfo.RealTimeSeconds;
    AdaptiveZedDebugMarkers.Length = 0;
    LocalPC = GetALocalPlayerController();
    if (LocalPC == None || LocalPC.ViewTarget == None)
    {
        return;
    }
    ViewLocation = LocalPC.ViewTarget.Location;
    foreach WorldInfo.AllPawns(class'KFPawn_Monster', Candidate)
    {
        if (Candidate == None || Candidate.bDeleteMe ||
            !Candidate.IsAliveAndWell() || Candidate.Mesh == None)
        {
            continue;
        }
        MarkerLocation = Candidate.Location;
        // Trace to the visible upper body instead of the pawn origin. Low
        // props and floor geometry can hide the feet without hiding the Zed.
        MarkerLocation.Z += 120.0;
        if (Candidate.Mesh.LastRenderTime <= WorldInfo.TimeSeconds - 0.30 ||
            !LocalPC.FastTrace(MarkerLocation, ViewLocation))
        {
            continue;
        }
        // KFPawn_Monster does not expose CollisionHeight to UnrealScript in
        // this KF2 SDK. A fixed world-space lift keeps the marker above the
        // largest normal Zed without retaining a pawn reference.
        InsertAdaptiveZedDebugMarkerByDistance(
            MarkerLocation, Candidate.Velocity,
            GetAdaptiveCorpseActionId(Candidate),
            GetAdaptiveCorpseDistanceUnits(Candidate));
    }
}

function DrawAdaptiveZedDebugMarkers(Canvas MarkerCanvas)
{
    local int Index;
    local vector MarkerLocation;
    local vector ScreenPosition;
    local vector ViewLocation;
    local rotator ViewRotation;
    local PlayerController LocalPC;
    local string MarkerLabel;
    local float MarkerTextScale;
    local float PredictionSeconds;

    if (!bAdaptiveZedDebugMarkers || MarkerCanvas == None ||
        WorldInfo == None || WorldInfo.NetMode != NM_Standalone)
    {
        return;
    }
    LocalPC = GetALocalPlayerController();
    if (LocalPC == None)
    {
        return;
    }
    LocalPC.GetPlayerViewPoint(ViewLocation, ViewRotation);
    RefreshAdaptiveZedDebugMarkers();
    for (Index = 0; Index < AdaptiveZedDebugMarkers.Length; ++Index)
    {
        MarkerLocation = AdaptiveZedDebugMarkers[Index].Location;
        PredictionSeconds =
            WorldInfo.RealTimeSeconds - AdaptiveZedDebugRefreshRealTime;
        if (PredictionSeconds < 0.0)
        {
            PredictionSeconds = 0.0;
        }
        else if (PredictionSeconds > 0.10)
        {
            PredictionSeconds = 0.10;
        }
        MarkerLocation += AdaptiveZedDebugMarkers[Index].Velocity *
            PredictionSeconds;
        if (vector(ViewRotation) dot (MarkerLocation - ViewLocation) <= 0.0)
        {
            continue;
        }
        ScreenPosition = MarkerCanvas.Project(MarkerLocation);
        if (ScreenPosition.X < 0.0 || ScreenPosition.Y < 0.0 ||
            ScreenPosition.X >= MarkerCanvas.ClipX ||
            ScreenPosition.Y >= MarkerCanvas.ClipY)
        {
            continue;
        }
        MarkerLabel =
            "Z "$FormatAdaptiveCorpseDistanceMeters(
                AdaptiveZedDebugMarkers[Index].DistanceUnits, true)$" "$
            FormatAdaptiveDebugMarkerId(
                AdaptiveZedDebugMarkers[Index].ZedId);
        MarkerTextScale = GetAdaptiveDebugMarkerTextScale(
            MarkerCanvas, AdaptiveZedDebugMarkers[Index].DistanceUnits);
        DrawAdaptiveDebugMarkerLabel(
            MarkerCanvas, MarkerLabel, ScreenPosition,
            64, 220, 255, MarkerTextScale);
    }
}

function RemoveAdaptiveDebugMarkerPostRender()
{
    if (AdaptiveDebugMarkerHUD != None)
    {
        AdaptiveDebugMarkerHUD.RemovePostRenderedActor(self);
        AdaptiveDebugMarkerHUD = None;
    }
    bPostRenderIfNotVisible = default.bPostRenderIfNotVisible;
}

function EnsureAdaptiveDebugMarkerPostRender()
{
    local PlayerController LocalPC;

    if (!bAdaptiveCorpseDebugMarkers && !bAdaptiveZedDebugMarkers)
    {
        RemoveAdaptiveDebugMarkerPostRender();
        return;
    }
    LocalPC = GetALocalPlayerController();
    if (LocalPC == None || LocalPC.MyHUD == None)
    {
        return;
    }
    if (AdaptiveDebugMarkerHUD != None &&
        AdaptiveDebugMarkerHUD != LocalPC.MyHUD)
    {
        AdaptiveDebugMarkerHUD.RemovePostRenderedActor(self);
        AdaptiveDebugMarkerHUD = None;
    }
    AdaptiveDebugMarkerHUD = LocalPC.MyHUD;
    bPostRenderIfNotVisible = true;
    AdaptiveDebugMarkerHUD.bShowOverlays = true;
    AdaptiveDebugMarkerHUD.AddPostRenderedActor(self);
}

simulated event PostRenderFor(PlayerController PC,
    Canvas MarkerCanvas, vector CameraPosition, vector CameraDir)
{
    if (PC == None || MarkerCanvas == None)
    {
        return;
    }
    AdaptiveDebugMarkerScreenEntries.Length = 0;
    DrawAdaptiveCorpseDebugMarkers(MarkerCanvas);
    DrawAdaptiveZedDebugMarkers(MarkerCanvas);
    if (!bAdaptiveDebugMarkerRenderConfirmed &&
        (AdaptiveCorpseDebugMarkers.Length > 0 ||
         AdaptiveZedDebugMarkers.Length > 0))
    {
        bAdaptiveDebugMarkerRenderConfirmed = true;
        `log("KF2OPT_DEBUG_MARKERS state=rendered corpse_candidates="$
             AdaptiveCorpseDebugMarkers.Length$" zed_candidates="$
             AdaptiveZedDebugMarkers.Length);
    }
}

function int GetProfileElapsedMilliseconds(int StartMilliseconds,
                                           int EndMilliseconds)
{
    local int ElapsedMilliseconds;

    if (EndMilliseconds >= StartMilliseconds)
    {
        ElapsedMilliseconds = EndMilliseconds - StartMilliseconds;
        if (ElapsedMilliseconds <= 10000)
        {
            return ElapsedMilliseconds;
        }
    }
    else if (StartMilliseconds >= 86390000 && EndMilliseconds <= 10000)
    {
        // GetSystemTime is time-of-day based. Preserve a genuine midnight
        // crossing without interpreting an ordinary backward clock correction
        // as nearly 24 hours of telemetry work.
        return (86400000 - StartMilliseconds) + EndMilliseconds;
    }
    ++ProfileClockAnomalies;
    return 0;
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
    local int WorldEmitterComponents;
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
    local int ProfileTotalStartMilliseconds;
    local int ProfileSectionStartMilliseconds;
    local int ProfileTotalNode;
    local int ProfileSectionNode;
    local int ProfileUnclassifiedMilliseconds;
    local bool bSubmitNativeProfileNodes;
    local string ProfileState;

    if (WorldInfo == None || WorldInfo.NetMode != NM_Standalone)
    {
        `log("KF2OPT_TELEMETRY schema=6 state=stopped reason=netmode_changed");
        Destroy();
        return;
    }
    EnsureAdaptiveDebugMarkerPostRender();

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=sample_begin");
    if (bDebugNativeWakeTest && !bDebugNativeWakeTestStarted && bAdaptiveRuntimeEnabled)
    {
        bDebugNativeWakeTestStarted = true;
        DebugNativeWakeTest = Spawn(class'KF2OptimizerNativeWakeTest');
        if (DebugNativeWakeTest != None) DebugNativeWakeTest.Start(self);
    }

    bSubmitNativeProfileNodes = ((SampleSequence + 1) % 10 == 0);
    ProfileTotalStartMilliseconds = GetProfileSystemMilliseconds();
    if (bSubmitNativeProfileNodes)
    {
        ProfileTotalNode = ProfNodeStart("KF2OPT_Telemetry_Total");
        ProfileSectionNode = ProfNodeStart("KF2OPT_Telemetry_Living");
    }
    ProfileSectionStartMilliseconds = GetProfileSystemMilliseconds();
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
    ProfileLivingMilliseconds += GetProfileElapsedMilliseconds(
        ProfileSectionStartMilliseconds, GetProfileSystemMilliseconds());
    if (bSubmitNativeProfileNodes)
    {
        ProfNodeStop(ProfileSectionNode);
    }

    // Reuse the one-second telemetry scan for scene pressure instead of
    // enumerating every living pawn again in the 250-ms corpse controller.
    AdaptiveVisibleLivingZeds = LivingRecentlyRendered;
    AdaptiveVisibleLivingObservedRealTime = WorldInfo.RealTimeSeconds;

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=living_done");

    GoreManager = KFGoreManager(WorldInfo.MyGoreEffectManager);
    if (bSubmitNativeProfileNodes)
    {
        ProfileSectionNode = ProfNodeStart("KF2OPT_Telemetry_CorpseGore");
    }
    ProfileSectionStartMilliseconds = GetProfileSystemMilliseconds();
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
    ProfileCorpseGoreMilliseconds += GetProfileElapsedMilliseconds(
        ProfileSectionStartMilliseconds, GetProfileSystemMilliseconds());
    if (bSubmitNativeProfileNodes)
    {
        ProfNodeStop(ProfileSectionNode);
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

    if (bSubmitNativeProfileNodes)
    {
        ProfileSectionNode = ProfNodeStart("KF2OPT_Telemetry_ParticlePools");
    }
    ProfileSectionStartMilliseconds = GetProfileSystemMilliseconds();
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
    ProfileParticlePoolMilliseconds += GetProfileElapsedMilliseconds(
        ProfileSectionStartMilliseconds, GetProfileSystemMilliseconds());
    if (bSubmitNativeProfileNodes)
    {
        ProfNodeStop(ProfileSectionNode);
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=pools_done");

    if (bSubmitNativeProfileNodes)
    {
        ProfileSectionNode = ProfNodeStart("KF2OPT_Telemetry_EffectActors");
    }
    ProfileSectionStartMilliseconds = GetProfileSystemMilliseconds();
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
    ProfileEffectActorMilliseconds += GetProfileElapsedMilliseconds(
        ProfileSectionStartMilliseconds, GetProfileSystemMilliseconds());
    if (bSubmitNativeProfileNodes)
    {
        ProfNodeStop(ProfileSectionNode);
    }
    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=gibs_done");
    if (bSubmitNativeProfileNodes)
    {
        ProfileSectionNode = ProfNodeStart("KF2OPT_Telemetry_WorldEmitters");
    }
    ProfileSectionStartMilliseconds = GetProfileSystemMilliseconds();
    foreach WorldInfo.AllActors(class'Emitter', WorldEmitter)
    {
        if (WorldEmitter == None || WorldEmitter.bDeleteMe ||
            WorldEmitter.ParticleSystemComponent == None ||
            !WorldEmitter.ParticleSystemComponent.bIsActive)
        {
            continue;
        }
        ++WorldEmitterComponents;
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
    ProfileWorldEmitterMilliseconds += GetProfileElapsedMilliseconds(
        ProfileSectionStartMilliseconds, GetProfileSystemMilliseconds());
    if (bSubmitNativeProfileNodes)
    {
        ProfNodeStop(ProfileSectionNode);
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=emitters_done");

    GameInfo = KFGameInfo(WorldInfo.Game);
    if (GameInfo != None && GameInfo.IsZedTimeActive())
    {
        ZedTimeActive = 1;
    }

    if (SampleSequence == 0) `log("KF2OPT_TRACE stage=sample_ready");

    if (bSubmitNativeProfileNodes)
    {
        ProfNodeStop(ProfileTotalNode);
    }
    ProfileTotalMilliseconds += GetProfileElapsedMilliseconds(
        ProfileTotalStartMilliseconds, GetProfileSystemMilliseconds());
    ++ProfileWindowSamples;
    ++SampleSequence;
    // KF2's shipping client exposes Clock/UnClock but returned zero for every
    // section during the real 2026-09-01 gameplay run. Report an honest,
    // accumulated one-millisecond wall-clock window instead of manufacturing
    // microsecond precision. Native profiler nodes are also submitted on the
    // receipt sample for a future engine-profiler capture.
    if (SampleSequence % 10 == 0)
    {
        ProfileUnclassifiedMilliseconds = Max(0,
            ProfileTotalMilliseconds - ProfileLivingMilliseconds -
            ProfileCorpseGoreMilliseconds -
            ProfileParticlePoolMilliseconds -
            ProfileEffectActorMilliseconds -
            ProfileWorldEmitterMilliseconds);
        ProfileState = ProfileTotalMilliseconds > 0
            ? "measured" : "below_resolution";
        `log("KF2OPT_TELEMETRY_PROFILE schema=2 sample="$SampleSequence$
             " state="$ProfileState$
             " timer=system_clock_ms resolution_us=1000"$
             " window_samples="$ProfileWindowSamples$
             " clock_anomalies="$ProfileClockAnomalies$
             " native_nodes=submitted"$
             " total_ms="$ProfileTotalMilliseconds$
             " living_ms="$ProfileLivingMilliseconds$
             " corpse_gore_ms="$ProfileCorpseGoreMilliseconds$
             " particle_pools_ms="$ProfileParticlePoolMilliseconds$
             " effect_actors_ms="$ProfileEffectActorMilliseconds$
             " world_emitters_ms="$ProfileWorldEmitterMilliseconds$
             " unclassified_ms="$ProfileUnclassifiedMilliseconds$
             " living="$LivingZeds$" corpses="$CorpseTotal$
             " pool_components="$(GoreParticleComponents +
                 WorldParticleComponents - WorldEmitterComponents)$
             " world_emitters="$WorldEmitterComponents$
             " dynamic_emitters="$ParticleDynamicSpawnEmitters);
        ProfileWindowSamples = 0;
        ProfileTotalMilliseconds = 0;
        ProfileLivingMilliseconds = 0;
        ProfileCorpseGoreMilliseconds = 0;
        ProfileParticlePoolMilliseconds = 0;
        ProfileEffectActorMilliseconds = 0;
        ProfileWorldEmitterMilliseconds = 0;
        ProfileClockAnomalies = 0;
    }
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

function QuiesceForWorldTeardown()
{
    if (bAdaptiveRuntimeQuiesced)
    {
        return;
    }
    bAdaptiveRuntimeQuiesced = true;
    if (DebugNativeWakeTest != None && !DebugNativeWakeTest.bDeleteMe)
    {
        DebugNativeWakeTest.Finish("incomplete", "world_teardown");
    }
    DebugNativeWakeTest = None;
    RemoveAdaptiveDebugMarkerPostRender();
    ClearTimer(nameof(SampleTelemetry), self);
    ClearTimer(nameof(StaggerCorpseCleanup), self);
    ClearTimer(nameof(AdaptiveCorpseLoadControl), self);
    ClearTimer(nameof(AdaptiveCorpseAgingControl), self);
    ClearTimer(nameof(RestoreOneAdaptiveCorpseLod), self);
    ClearTimer(nameof(WakeAdaptiveDistanceSleptCorpseBatch), self);
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
    if (AdaptiveAgingPhysicsSleeps > 0 ||
        AdaptiveCorpseAgingReductions > 0)
    {
        `log("KF2OPT_CORPSE_AGING state=stopped slept="$
             AdaptiveAgingPhysicsSleeps$" reductions="$
             AdaptiveCorpseAgingReductions);
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
    // The current world is already being unloaded. Do not write rendering,
    // skeletal-mesh or WorldInfo state from Destroyed(): those objects are on
    // UE3's teardown path and every modified value dies with this world.
    // Drop all strong references and ownership metadata without dereferencing
    // the actors. Live rollback remains handled by the authenticated recover
    // action before teardown.
    AdaptiveGraphicsState = None;
    AdaptiveCorpseManager = None;
    AdaptiveCorpseAgingCursor = 0;
    AdaptiveCorpseLodCorpses.Length = 0;
    AdaptiveCorpseLodOriginalMinModels.Length = 0;
    AdaptiveCorpseLodAppliedMinModels.Length = 0;
    AdaptiveCorpseLodPersistentAging.Length = 0;
    AdaptiveCorpseLodPhysicsFrozen.Length = 0;
    AdaptiveLivingVisualZeds.Length = 0;
    AdaptiveLivingOriginalMinLods.Length = 0;
    AdaptiveLivingAppliedMinLods.Length = 0;
    AdaptiveLivingOriginalAnimDistances.Length = 0;
    AdaptiveLivingAppliedAnimDistances.Length = 0;
    AdaptiveLivingOriginalAnimRates.Length = 0;
    AdaptiveLivingAppliedAnimRates.Length = 0;
    AdaptiveDistanceSleptCorpses.Length = 0;
    AdaptiveDistanceSleepTransitions.Length = 0;
    AdaptiveDistanceSleepTransitionCount = 0;
    bAdaptiveDistanceSleepTransitionFullLogged = false;
    AdaptiveCorpseDebugMarkers.Length = 0;
    AdaptiveZedDebugMarkers.Length = 0;
    AdaptiveZedDebugRefreshRealTime = 0.0;
    bAdaptiveDebugMarkerRenderConfirmed = false;
    AdaptiveCorpsePhysicsActionIds.Length = 0;
    AdaptiveCorpsePhysicsActionIdCount = 0;
    `log("KF2OPT_TELEMETRY schema=6 state=stopped reason=world_teardown");
}

event Destroyed()
{
    QuiesceForWorldTeardown();
    Super.Destroyed();
}

defaultproperties
{
    bAlwaysRelevant=false
    bHidden=true
    RemoteRole=ROLE_None
    bAdaptiveCorpseDebugMarkers=false
    bAdaptiveZedDebugMarkers=false
    bDebugNativeWakeTest=false
    bAdaptiveRuntimeEnabled=true
}
