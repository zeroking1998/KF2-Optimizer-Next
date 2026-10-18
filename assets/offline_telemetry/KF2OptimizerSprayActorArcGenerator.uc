class KF2OptimizerSprayActorArcGenerator extends KFSprayActor_ArcGenerator;

const RotationAudioUpdatesPerSecond=30;

var transient int RotationAudioUpdateBucket;
var transient float LastRotationAudioValue;

simulated function ResetRotationAudio()
{
    if (RotationAudioUpdateBucket >= 0 && WorldInfo != None &&
        OwningKFPawn != None && WorldInfo.NetMode != NM_DedicatedServer)
    {
        OwningKFPawn.SetWeaponComponentRTPCValue("FlamethrowerRotation", 0.0);
    }
    RotationAudioUpdateBucket = -1;
    LastRotationAudioValue = 0.0;
}

simulated event Tick(float DeltaTime)
{
    local int CurrentBucket;

    super(KFSprayActor).Tick(DeltaTime);
    if (bDetached || (RotationAudioUpdateBucket >= 0 &&
        RotationSpeed == LastRotationAudioValue))
    {
        return;
    }
    if (WorldInfo == None || OwningKFPawn == None ||
        WorldInfo.NetMode == NM_DedicatedServer)
    {
        return;
    }
    CurrentBucket = int(
        WorldInfo.RealTimeSeconds * RotationAudioUpdatesPerSecond);
    if (CurrentBucket == RotationAudioUpdateBucket)
    {
        return;
    }
    OwningKFPawn.SetWeaponComponentRTPCValue("FlamethrowerRotation",
        RotationSpeed);
    RotationAudioUpdateBucket = CurrentBucket;
    LastRotationAudioValue = RotationSpeed;
}

simulated function BeginSpray()
{
    super.BeginSpray();
    RotationAudioUpdateBucket = -1;
    LastRotationAudioValue = 0.0;
    if (!bDeleteMe && !bDetached && Base != None && !Base.bHidden &&
        WorldInfo != None && OwningKFPawn != None &&
        WorldInfo.NetMode != NM_DedicatedServer)
    {
        OwningKFPawn.SetWeaponComponentRTPCValue(
            "FlamethrowerRotation", 0.0);
    }
}

simulated function DetachAndFinish()
{
    ResetRotationAudio();
    super.DetachAndFinish();
}

simulated function CleanupEndFire()
{
    ResetRotationAudio();
    super.CleanupEndFire();
}

simulated function Destroyed()
{
    ResetRotationAudio();
    super.Destroyed();
}

defaultproperties
{
    RotationAudioUpdateBucket=-1
}
