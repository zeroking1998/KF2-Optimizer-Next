// Authenticated session listener. The app connects through loopback and the
// probe verifies a fresh random token before accepting an Adaptive action.
class KF2OptimizerAdaptiveControlListener extends TcpLink;

var KF2OptimizerOnlineCorpseController OnlineCorpseController;

function bool EnsureOnlineCorpseController()
{
    local KF2OptimizerOnlineCorpseController CurrentController;

    if (OnlineCorpseController != None &&
        !OnlineCorpseController.bDeleteMe)
    {
        return true;
    }
    OnlineCorpseController = None;
    if (WorldInfo == None)
    {
        return false;
    }
    foreach WorldInfo.DynamicActors(
        class'KF2OptimizerOnlineCorpseController', CurrentController)
    {
        if (CurrentController != None && !CurrentController.bDeleteMe)
        {
            OnlineCorpseController = CurrentController;
            return true;
        }
    }
    OnlineCorpseController = Spawn(
        class'KF2OptimizerOnlineCorpseController');
    return OnlineCorpseController != None &&
        !OnlineCorpseController.bDeleteMe;
}

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
    // Publish the address once at bind. Menu logs can otherwise remain
    // buffered indefinitely, leaving an available local control undiscoverable.
    ConsoleCommand("flushlog", false);
    if (WorldInfo.NetMode == NM_Client ||
        WorldInfo.NetMode == NM_ListenServer)
    {
        if (!EnsureOnlineCorpseController())
        {
            `log("KF2OPT_ONLINE_CORPSE state=unavailable"$
                 " reason=controller_spawn_failed local_only=true");
        }
    }
}

event Destroyed()
{
    // The controller owns the exact restore ledger and is scoped to the
    // current World, not this recoverable TCP listener. World teardown
    // destroys it safely; a replacement listener adopts it in the same map.
    OnlineCorpseController = None;
    Super.Destroyed();
}

defaultproperties
{
    bAlwaysTick=true
    bHidden=true
    RemoteRole=ROLE_None
}
