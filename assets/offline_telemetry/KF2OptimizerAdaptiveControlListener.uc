// Authenticated session listener. The app connects through loopback and the
// probe verifies a fresh random token before accepting an Adaptive action.
class KF2OptimizerAdaptiveControlListener extends TcpLink;

event PreBeginPlay()
{
    local int BoundPort;

    Super.PreBeginPlay();
    if (WorldInfo == None)
    {
        `log("KF2OPT_ADAPTIVE_BRIDGE state=blocked reason=no_world");
        Destroy();
        return;
    }
    LinkMode = MODE_Line;
    ReceiveMode = RMODE_Event;
    if (WorldInfo.NetMode == NM_Standalone)
    {
        AcceptClass = class'KF2OptimizerAdaptiveControlConnection';
    }
    else if (WorldInfo.NetMode == NM_Client ||
             WorldInfo.NetMode == NM_ListenServer)
    {
        // This connection reaches only the process-local viewport interaction.
        // That interaction permits authenticated graphics and bounded local
        // corpse-pool actions, never replicated writes.
        AcceptClass = class'KF2OptimizerOnlineGraphicsControlConnection';
    }
    else
    {
        `log("KF2OPT_ADAPTIVE_BRIDGE state=blocked reason=unsupported_net_mode");
        Destroy();
        return;
    }
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
