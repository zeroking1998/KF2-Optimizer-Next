// Authenticated loopback connection for process-local controls in a joined or
// listen-server session. It deliberately has no reference to the offline
// telemetry probe and cannot issue replicated writes.
class KF2OptimizerOnlineGraphicsControlConnection extends TcpLink;

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
    LinkMode = MODE_Line;
    ReceiveMode = RMODE_Event;
    if (Left(IpAddrToString(RemoteAddr), 10) != "127.0.0.1:")
    {
        `log("KF2OPT_ONLINE_GRAPHICS_BRIDGE state=rejected reason=non_loopback");
        Close();
    }
}

event ReceivedLine(string Line)
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

    if (Left(IpAddrToString(RemoteAddr), 10) != "127.0.0.1:" ||
        Len(Line) > 128)
    {
        SendText("KF2OPT_ACK 0 failed rejected");
        Close();
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
        Close();
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
    else
    {
        SendText("KF2OPT_ACK "$Sequence$" failed rejected");
    }
    Close();
}

defaultproperties
{
    bAlwaysTick=true
    bHidden=true
    RemoteRole=ROLE_None
}
