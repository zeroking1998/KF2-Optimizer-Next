// Preserves KF2's fire behavior while removing its unconditional FIRE warning.
class KF2OptimizerFireAffliction extends KFAffliction_Fire;

function bool AdoptExisting(KFAffliction_Fire Existing)
{
    local float RemainingActiveTime;

    if (Existing == None || Existing.PawnOwner == None ||
        Existing.Class != class'KFAffliction_Fire')
    {
        return false;
    }

    PawnOwner = Existing.PawnOwner;
    MonsterOwner = Existing.MonsterOwner;
    CurrentStrength = Existing.CurrentStrength;
    DissipationRate = Existing.DissipationRate;
    Cooldown = Existing.Cooldown;
    LastActivationTime = Existing.LastActivationTime;
    bNeedsTick = Existing.bNeedsTick;
    LastDissipationTime = Existing.LastDissipationTime;
    bDebug = Existing.bDebug;
    AfflictionType = Existing.AfflictionType;
    Duration = Existing.Duration;
    bIsActive = Existing.bIsActive;
    EffectSocketName = Existing.EffectSocketName;
    FireBurnedAmount = Existing.FireBurnedAmount;
    FireFullyCharredDuration = Existing.FireFullyCharredDuration;
    FireCharPercentThreshhold = Existing.FireCharPercentThreshhold;
    BurningTemplate = Existing.BurningTemplate;
    BurningEffect = Existing.BurningEffect;
    MicrowaveParamValue = Existing.MicrowaveParamValue;
    OnFireSound = Existing.OnFireSound;
    OnFireEndSound = Existing.OnFireEndSound;
    Instigator = Existing.Instigator;

    if (bIsActive &&
        PawnOwner.IsTimerActive(nameof(DeActivate), Existing))
    {
        RemainingActiveTime = PawnOwner.GetTimerRate(
            nameof(DeActivate), Existing) - PawnOwner.GetTimerCount(
            nameof(DeActivate), Existing);
        PawnOwner.ClearTimer(nameof(DeActivate), Existing);
        if (RemainingActiveTime > 0.0)
        {
            PawnOwner.SetTimer(
                RemainingActiveTime, false, nameof(DeActivate), self);
        }
    }
    Existing.BurningEffect = None;
    Existing.bIsActive = false;
    return true;
}

function ToggleEffects(bool bEnabled, optional bool bDummy)
{
    // Do not create client effects on a dedicated server.
    if (PawnOwner.WorldInfo.NetMode == NM_DedicatedServer)
    {
        return;
    }

    if (bEnabled)
    {
        if (BurningEffect == None)
        {
            BurningEffect = new(self) class'ParticleSystemComponent';
            BurningEffect.SetTemplate(BurningTemplate);
            PawnOwner.Mesh.AttachComponentToSocket(
                BurningEffect, EffectSocketName);
            BurningEffect.ActivateSystem();
        }
        else
        {
            BurningEffect.SetStopSpawning(-1, false);
        }

        PawnOwner.PlaySoundBase(OnFireSound, true, true, true);
        if (PawnOwner.Role < ROLE_Authority)
        {
            LastActivationTime = PawnOwner.WorldInfo.TimeSeconds;
        }
    }
    else
    {
        if (BurningEffect != None)
        {
            BurningEffect.SetStopSpawning(-1, true);
        }
        PawnOwner.PlaySoundBase(OnFireEndSound, true, true);
    }
}

