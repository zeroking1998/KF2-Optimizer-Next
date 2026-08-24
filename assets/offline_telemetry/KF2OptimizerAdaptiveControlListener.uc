// Authenticated session listener. The app connects through loopback and the
// probe verifies a fresh random token before accepting an Adaptive action.
class KF2OptimizerAdaptiveControlListener extends TcpLink;

event PreBeginPlay()
{
    local int BoundPort;

    Super.PreBeginPlay();
    if (WorldInfo == None || WorldInfo.NetMode != NM_Standalone)
    {
        `log("KF2OPT_ADAPTIVE_BRIDGE state=blocked reason=not_standalone");
        Destroy();
        return;
    }
    LinkMode = MODE_Line;
    ReceiveMode = RMODE_Event;
    AcceptClass = class'KF2OptimizerAdaptiveControlConnection';
    BoundPort = BindPort(0, false);
    if (BoundPort <= 0 || !Listen())
    {
        `log("KF2OPT_ADAPTIVE_BRIDGE state=unavailable reason=bind_failed");
        Destroy();
        return;
    }
    `log("KF2OPT_ADAPTIVE_BRIDGE state=ready port="$BoundPort);
}

defaultproperties
{
    bAlwaysTick=true
    bHidden=true
    RemoteRole=ROLE_None
}
