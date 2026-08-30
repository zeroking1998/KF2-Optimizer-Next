// Published-only offline entry point. KF2 resolves this mutator after the
// gameplay URL is opened, when Published packages are available.
class KF2OptimizerTelemetryMutator extends KFMutator;

function InitMutator(string Options, out string ErrorMessage)
{
    local Engine CurrentEngine;
    local GameViewportClient CurrentViewport;
    local KF2OptimizerTelemetryInteraction CurrentInteraction;
    local string InteractionPath;

    Super.InitMutator(Options, ErrorMessage);
    if (WorldInfo == None || WorldInfo.NetMode != NM_Standalone)
    {
        `log("KF2OPT_MUTATOR schema=1 state=blocked_net_mode");
        Destroy();
        return;
    }

    CurrentEngine = class'Engine'.static.GetEngine();
    if (CurrentEngine == None || CurrentEngine.GameViewport == None)
    {
        `log("KF2OPT_MUTATOR schema=1 state=viewport_unavailable");
        return;
    }
    CurrentViewport = CurrentEngine.GameViewport;
    InteractionPath = PathName(CurrentViewport)$
        ".KF2OptimizerTelemetryInteraction";
    CurrentInteraction = KF2OptimizerTelemetryInteraction(
        FindObject(InteractionPath,
            class'KF2OptimizerTelemetryInteraction'));
    if (CurrentInteraction != None)
    {
        CurrentInteraction.PrepareForGameplayWorld();
        `log("KF2OPT_MUTATOR schema=1 state=ready interaction=existing");
        return;
    }

    CurrentInteraction = new(CurrentViewport, "KF2OptimizerTelemetryInteraction")
        class'KF2OptimizerTelemetryInteraction';
    if (CurrentInteraction == None)
    {
        `log("KF2OPT_MUTATOR schema=1 state=interaction_failed");
        return;
    }
    CurrentInteraction.PrepareForGameplayWorld();
    CurrentViewport.InsertInteraction(CurrentInteraction);
    `log("KF2OPT_MUTATOR schema=1 state=ready interaction=inserted");
}

defaultproperties
{
    GroupNames.Add("KF2OptimizerTelemetry")
}
