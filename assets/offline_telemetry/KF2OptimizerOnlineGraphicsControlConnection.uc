// Authenticated loopback connection for process-local controls in a joined or
// listen-server session. It deliberately has no reference to the offline
// telemetry probe and cannot issue replicated writes.
class KF2OptimizerOnlineGraphicsControlConnection extends TcpLink;

var bool bCleanupStarted;

const ConnectionDeadlineSeconds=5.0;

function RequestClose()
{
    if (bCleanupStarted || bDeleteMe)
    {
        return;
    }
    bCleanupStarted = true;
    Close();
}

function ConnectionTimedOut()
{
    local int WorkStarted;
    if (bDeleteMe)
    {
        return;
    }
    WorkStarted = class'KF2OptimizerTelemetryProbe'.static.BeginOwnWork();
    bCleanupStarted = true;
    ClearTimer(nameof(ConnectionTimedOut), self);
    Close();
    if (!bDeleteMe)
    {
        Destroy();
    }
    class'KF2OptimizerTelemetryProbe'.static.EndOwnWork(WorkStarted);
}

event Closed()
{
    local int WorkStarted;
    WorkStarted = class'KF2OptimizerTelemetryProbe'.static.BeginOwnWork();
    bCleanupStarted = true;
    ClearTimer(nameof(ConnectionTimedOut), self);
    if (!bDeleteMe)
    {
        Destroy();
    }
    class'KF2OptimizerTelemetryProbe'.static.EndOwnWork(WorkStarted);
}

function string TakeToken(out string Line)
{
    local int Space;
    local string Result;

    Space = InStr(Line, " ");
    if (Space < 0)
    {
        Result = Line;
        Line = "";
        return Result;
    }
    Result = Left(Line, Space);
    Line = Mid(Line, Space + 1);
    return Result;
}

event Accepted()
{
    local int WorkStarted;
    WorkStarted = class'KF2OptimizerTelemetryProbe'.static.BeginOwnWork();
    LinkMode = MODE_Line;
    ReceiveMode = RMODE_Event;
    SetTimer(ConnectionDeadlineSeconds, false,
        nameof(ConnectionTimedOut), self);
    if (Left(IpAddrToString(RemoteAddr), 10) != "127.0.0.1:")
    {
        `log("KF2OPT_ONLINE_GRAPHICS_BRIDGE state=rejected reason=non_loopback");
        RequestClose();
    }
    class'KF2OptimizerTelemetryProbe'.static.EndOwnWork(WorkStarted);
}

event ReceivedLine(string Line)
{
    local int WorkStarted;
    WorkStarted = class'KF2OptimizerTelemetryProbe'.static.BeginOwnWork();
    HandleMeasuredLine(Line);
    class'KF2OptimizerTelemetryProbe'.static.EndOwnWork(WorkStarted);
}

function HandleMeasuredLine(string Line)
{
    local string Prefix;
    local string Token;
    local string SequenceText;
    local string Resource;
    local string QualityText;
    local int Sequence;
    local int Quality;
    local Engine CurrentEngine;
    local GameViewportClient CurrentViewport;
    local KF2OptimizerOnlineContextInteraction CurrentInteraction;
    local string InteractionPath;
    local bool Applied;
    local string OverheadReceipt;

    if (bCleanupStarted)
    {
        return;
    }
    if (Left(IpAddrToString(RemoteAddr), 10) != "127.0.0.1:" ||
        Len(Line) > 128)
    {
        SendText("KF2OPT_ACK 0 failed rejected");
        RequestClose();
        return;
    }

    Prefix = TakeToken(Line);
    Token = TakeToken(Line);
    SequenceText = TakeToken(Line);
    Resource = TakeToken(Line);
    QualityText = TakeToken(Line);
    Sequence = int(SequenceText);
    Quality = int(QualityText);
    if (Prefix != "KF2OPT" || Len(Line) != 0)
    {
        SendText("KF2OPT_ACK "$SequenceText$" failed malformed");
        RequestClose();
        return;
    }

    if (Resource == "overhead" && Quality == 100)
    {
        OverheadReceipt = class'KF2OptimizerTelemetryProbe'.static.
            ReadOwnWork(Token, Sequence);
        if (Len(OverheadReceipt) > 0) SendText(OverheadReceipt);
        RequestClose();
        return;
    }

    CurrentEngine = class'Engine'.static.GetEngine();
    if (CurrentEngine != None && CurrentEngine.GameViewport != None)
    {
        CurrentViewport = CurrentEngine.GameViewport;
        InteractionPath = PathName(CurrentViewport)$
            ".KF2OptimizerOnlineContextInteraction";
        CurrentInteraction = KF2OptimizerOnlineContextInteraction(
            FindObject(InteractionPath,
                class'KF2OptimizerOnlineContextInteraction'));
    }
    if (CurrentInteraction != None)
    {
        Applied = CurrentInteraction.ApplyOnlineGraphicsControl(
            Token, Sequence, Resource, Quality);
    }
    if (Applied)
    {
        SendText("KF2OPT_ACK "$Sequence$" applied "$Resource$" "$Quality);
    }
    else if (CurrentInteraction != None &&
             CurrentInteraction.WasOnlineGraphicsCapabilityRejected())
    {
        SendText("KF2OPT_ACK "$Sequence$" unsupported "$Resource$" "$Quality);
    }
    else
    {
        SendText("KF2OPT_ACK "$Sequence$" failed rejected");
    }
    RequestClose();
}

defaultproperties
{
    bAlwaysTick=true
    bHidden=true
    RemoteRole=ROLE_None
}
