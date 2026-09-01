class KF2OptimizerAdaptiveControlConnection extends TcpLink;

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
    local string Peer;

    LinkMode = MODE_Line;
    ReceiveMode = RMODE_Event;
    Peer = IpAddrToString(RemoteAddr);
    if (Left(Peer, 10) != "127.0.0.1:")
    {
        `log("KF2OPT_ADAPTIVE_BRIDGE state=rejected reason=non_loopback");
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
    local KF2OptimizerTelemetryProbe Probe;
    local bool Applied;
    local Engine CurrentEngine;
    local GameViewportClient CurrentViewport;
    local KF2OptimizerTelemetryInteraction CurrentInteraction;
    local string InteractionPath;

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

    if ((Resource ~= "enable") || (Resource ~= "disable"))
    {
        CurrentEngine = class'Engine'.static.GetEngine();
        if (CurrentEngine != None && CurrentEngine.GameViewport != None)
        {
            CurrentViewport = CurrentEngine.GameViewport;
            InteractionPath = PathName(CurrentViewport)$
                ".KF2OptimizerTelemetryInteraction";
            CurrentInteraction = KF2OptimizerTelemetryInteraction(
                FindObject(InteractionPath,
                    class'KF2OptimizerTelemetryInteraction'));
        }
        if (CurrentInteraction == None)
        {
            SendText("KF2OPT_ACK "$Sequence$" failed rejected");
            Close();
            return;
        }
    }

    foreach WorldInfo.DynamicActors(class'KF2OptimizerTelemetryProbe', Probe)
    {
        if (Probe != None && !Probe.bDeleteMe)
        {
            Applied = Probe.ApplyAdaptiveResourceControl(
                Token, Sequence, Resource, Quality);
            break;
        }
    }
    if (Applied)
    {
        if ((Resource ~= "enable") || (Resource ~= "disable"))
        {
            CurrentInteraction.SetProcessAdaptiveRuntimeEnabled(
                Resource ~= "enable");
        }
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
