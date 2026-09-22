// Preserves KF2's fire behavior while removing its unconditional FIRE warning.
class KF2OptimizerFireAffliction extends KFAffliction_Fire;

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

