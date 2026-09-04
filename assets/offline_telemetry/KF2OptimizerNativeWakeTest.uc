// Opt-in standalone diagnostic. Never changes normal sleep/aging policy.
// Injected wakes are test stimuli, not evidence of spontaneous engine wakes.
class KF2OptimizerNativeWakeTest extends Info;

var KF2OptimizerTelemetryProbe Probe;
var KFPawn Target;
var string TargetId;
var float StartedAt;
var float InjectedAt;
var float NextWakeAt;
var float ObservedAt;
var float SleepObservedAt;
var string SleepSource;
var int Cycle;
var bool bWaitingForResleep;
var bool bObserved;
var bool bInjectedWakePending;

// Called only by a committed optimizer sleep action, after its readback.
// A rigid body becoming asleep by itself must not count as an optimizer action.
function ObserveSleep(KFPawn Candidate, string Source)
{
    if (!bDeleteMe && Probe != None && Candidate != None && Candidate == Target &&
        bWaitingForResleep && SleepSource == "" &&
        Probe.GetAdaptiveCorpseActionId(Candidate) == TargetId &&
        Candidate.Mesh != None && !Candidate.Mesh.RigidBodyIsAwake())
    {
        SleepObservedAt = WorldInfo.RealTimeSeconds;
        SleepSource = Source;
    }
}

// Consume the attribution once, only when the real detector observes our wake.
function bool ConsumeInjectedWake(string CorpseId)
{
    if (!bDeleteMe && bInjectedWakePending && CorpseId == TargetId)
    {
        bInjectedWakePending = false;
        return true;
    }
    return false;
}

function Finish(string Result, string Reason)
{
    `log("KF2OPT_WAKE_TEST state="$Result$" reason="$Reason$
         " corpse_id="$TargetId$" cycles="$Cycle$" origin=injected_test");
    Target = None;
    Probe = None;
    Destroy();
}

function Start(KF2OptimizerTelemetryProbe InProbe)
{
    Probe = InProbe;
    StartedAt = WorldInfo.RealTimeSeconds;
    `log("KF2OPT_WAKE_TEST state=started origin=injected_test max_cycles=6 timeout_s=240");
    SetTimer(0.05, true, nameof(Check), self);
}

function float ExpectedSeconds(int Count)
{
    switch (Count)
    {
        case 1: return 2.0;
        case 2: return 5.0;
        case 3: return 10.0;
        case 4: return 20.0;
    }
    return 30.0;
}

function Check()
{
    local int Index;
    local int TrackedIndex;
    local int TransitionIndex;
    local float Now;
    local float Elapsed;
    local KFPawn Candidate;

    if (WorldInfo == None || WorldInfo.NetMode != NM_Standalone ||
        Probe == None || Probe.bDeleteMe || !Probe.bAdaptiveRuntimeEnabled)
    {
        Finish("incomplete", "session_ended_or_adaptive_disabled");
        return;
    }
    Now = WorldInfo.RealTimeSeconds;
    if (Now - StartedAt > 240.0)
    {
        Finish("incomplete", "timeout");
        return;
    }
    if (Target == None)
    {
        if (TargetId != "")
        {
            Finish("incomplete", "target_disappeared");
            return;
        }
        // Select only a corpse that the real optimizer already put to sleep.
        for (Index = 0; Index < Probe.AdaptiveDistanceSleptCorpses.Length; ++Index)
        {
            Candidate = Probe.AdaptiveDistanceSleptCorpses[Index].Corpse;
            if (Candidate != None && !Candidate.bDeleteMe &&
                Candidate.Health <= 0 && Candidate.Mesh != None &&
                Candidate.Physics == PHYS_RigidBody &&
                !Candidate.Mesh.RigidBodyIsAwake() &&
                Probe.GetAdaptiveCorpseDistanceUnits(Candidate) >= 1300)
            {
                TransitionIndex = Probe.FindAdaptiveDistanceSleepTransition(
                    Probe.GetAdaptiveCorpseActionId(Candidate));
                if (TransitionIndex < 0 ||
                    Probe.AdaptiveDistanceSleepTransitions[TransitionIndex].NativeWakeCount != 0)
                {
                    continue;
                }
                Target = Candidate;
                TargetId = Probe.GetAdaptiveCorpseActionId(Target);
                `log("KF2OPT_WAKE_TEST state=selected corpse_id="$TargetId);
                break;
            }
        }
        if (Target == None) return;
    }
    if (Target.bDeleteMe || Target.Health > 0 || Target.Mesh == None ||
        Probe.GetAdaptiveCorpseActionId(Target) != TargetId ||
        !Probe.IsAdaptiveCorpseInPool(Target))
    {
        Finish("incomplete", "target_lost_or_reused");
        return;
    }
    if (Target.Physics != PHYS_RigidBody)
    {
        Finish("incomplete", "physics_changed_by_other_system");
        return;
    }
    if (Probe.GetAdaptiveCorpseDistanceUnits(Target) < 1300)
    {
        Finish("incomplete", "player_too_close");
        return;
    }
    TrackedIndex = Probe.FindAdaptiveDistanceSleptCorpse(Target);
    if (!bWaitingForResleep)
    {
        if (Now < NextWakeAt) return;
        if (TrackedIndex < 0 || Target.Mesh.RigidBodyIsAwake())
        {
            Finish("incomplete", "sleep_state_changed_before_injection");
            return;
        }
        Target.Mesh.WakeRigidBody();
        if (!Target.Mesh.RigidBodyIsAwake())
        {
            Finish("incomplete", "wake_not_effective");
            return;
        }
        ++Cycle;
        InjectedAt = Now;
        bInjectedWakePending = true;
        SleepSource = "";
        SleepObservedAt = 0.0;
        bObserved = false;
        bWaitingForResleep = true;
        `log("KF2OPT_WAKE_TEST state=injected corpse_id="$TargetId$
             " cycle="$Cycle$" effective_awake=1 origin=injected_test");
        return;
    }
    Index = Probe.FindAdaptiveDistanceSleepTransition(TargetId);
    if (!bObserved)
    {
        if (Index >= 0 && Probe.AdaptiveDistanceSleepTransitions[Index].
            RemovalReason == "native_wake")
        {
            ObservedAt = Probe.AdaptiveDistanceSleepTransitions[Index].
                NativeWakeObservedRealTime;
            if (Probe.AdaptiveDistanceSleepTransitions[Index].NativeWakeCount !=
                Min(Cycle, 5) || Abs(Probe.AdaptiveDistanceSleepTransitions[Index].
                NativeWakeCooldownUntilRealTime - ObservedAt -
                ExpectedSeconds(Cycle)) > 0.05)
            {
                Finish("failed", "incorrect_backoff");
                return;
            }
            bObserved = true;
            `log("KF2OPT_WAKE_TEST state=backoff_verified corpse_id="$TargetId$
                 " cycle="$Cycle$" expected_ms="$int(ExpectedSeconds(Cycle)*1000.0));
        }
        else if (Now - InjectedAt > 2.0)
        {
            Finish("incomplete", "native_wake_not_observed");
        }
        return;
    }
    if (SleepSource != "")
    {
        Elapsed = SleepObservedAt - ObservedAt;
        if (Elapsed + 0.05 < ExpectedSeconds(Cycle))
        {
            Finish("failed", "resleep_before_deadline");
            return;
        }
        `log("KF2OPT_WAKE_TEST state=resleep_verified corpse_id="$TargetId$
             " cycle="$Cycle$" elapsed_ms="$int(Elapsed*1000.0)$
             " sleep_source="$SleepSource$" origin=injected_test");
        if (SleepSource == "aging")
        {
            // Aging does not enter the distance tracker. Waking again here
            // would not exercise the next distance-backoff cycle faithfully.
            Finish("partial", "aging_sleep_after_backoff");
            return;
        }
        if (SleepSource != "distance" || TrackedIndex < 0)
        {
            Finish("incomplete", "sleep_owner_changed");
            return;
        }
        if (Cycle >= 6)
        {
            Finish("passed", "all_six_cycles_verified");
            return;
        }
        bWaitingForResleep = false;
        NextWakeAt = Now + 0.5;
    }
    else if (Now - ObservedAt > ExpectedSeconds(Cycle) + 10.0)
    {
        Finish("incomplete", "no_eligible_optimizer_resleep");
    }
}

event Destroyed()
{
    ClearTimer(nameof(Check), self);
    Target = None;
    Probe = None;
    Super.Destroyed();
}

defaultproperties
{
    bHidden=true
    RemoteRole=ROLE_None
}
