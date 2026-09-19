// Temporary KF2 viewport subclass used only for a protected Optimizer session.
// All native KFGameViewportClient behavior is inherited. The app restores the
// exact original KFEngine.ini after KF2 exits.
class KF2OptimizerGraphicsViewport extends KFGameViewportClient;

event bool Init(out string OutError)
{
    local KF2OptimizerGraphicsInteraction Monitor;
    local KF2OptimizerOnlineContextInteraction OnlineMonitor;

    if (!Super.Init(OutError))
    {
        return false;
    }
    Monitor = new(self, "KF2OptimizerGraphicsInteraction")
        class'KF2OptimizerGraphicsInteraction';
    if (Monitor == None || InsertInteraction(Monitor) == -1)
    {
        OutError = "KF2 Optimizer graphics monitor could not start";
        return false;
    }
    OnlineMonitor = new(self, "KF2OptimizerOnlineContextInteraction")
        class'KF2OptimizerOnlineContextInteraction';
    if (OnlineMonitor == None || InsertInteraction(OnlineMonitor) == -1)
    {
        OutError = "KF2 Optimizer online context monitor could not start";
        return false;
    }
    `log("KF2OPT_GFX_BOOTSTRAP schema=1 state=ready");
    return true;
}
