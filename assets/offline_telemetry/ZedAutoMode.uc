class ZedAutoMode extends Object;

struct Binding
{
    var KF2OptimizerDamageGuard Target, Donor;
    var array<byte> Original;
};

var private array<Binding> Active;
var private int Requested;

private static final function bool Matches(KF2OptimizerDamageGuard A, KF2OptimizerDamageGuard B)
{
    local int I;
    if (A.script.Length != B.script.Length) return false;
    for (I = 0; I < A.script.Length; ++I)
        if (A.script[I] != B.script[I]) return false;
    return true;
}

private final function bool Add(class<Object> Base, class<Object> Source, name Fn, int Expected, optional name Other)
{
    local Binding B;
    B.Target = class'KF2OptimizerDamageGuard'.static.Resolve(PathName(Base) $ "." $ string(Fn));
    B.Donor = class'KF2OptimizerDamageGuard'.static.Resolve(PathName(Source) $ "." $ string(Other == '' ? Fn : Other));
    if (B.Target != none && B.Donor != none && B.Target.script.Length == Expected && B.Donor.script.Length > 0)
    {
        B.Original = B.Target.script;
        B.Target.script = B.Donor.script;
        if (Matches(B.Target, B.Donor))
        {
            Active.AddItem(B);
            return true;
        }
        B.Target.script = B.Original;
    }
    `log("ZedSystemAuto: incompatible function" @ Base @ Fn);
    return false;
}

private final function Trim(int Count)
{
    local int I;
    for (I = Active.Length - 1; I >= Count; --I)
    {
        if (Matches(Active[I].Target, Active[I].Donor))
            Active[I].Target.script = Active[I].Original;
        else
            `log("ZedSystemAuto: ownership changed; preserving current body" @ Active[I].Target);
        Active.Remove(I, 1);
    }
}

// The shared pawn fix stays installed across a direct solo/client transition.
final function UpdateMode(WorldInfo.ENetMode NetMode, bool bGameplay)
{
    local int Count;
    if (bGameplay)
    {
        if (NetMode == NM_Standalone) Count = 8;
        else if (NetMode == NM_Client || NetMode == NM_ListenServer) Count = 1;
    }
    if (Count == Requested) return;
    Requested = Count;
    if (Count < Active.Length) Trim(Count);
    if (Count > 0 && Active.Length == 0 && !Add(class'KFPawn_Monster',class'CheckKFPawn_Monster','Tick',615))
        return;
    if (Count == 8 && Active.Length == 1)
    {
        if (!Add(class'KFAIController',class'CheckKFAIController','FindNewEnemy',684)
            || !Add(class'KFAIController',class'CheckKFAIController','OtherZedsTargetingMyEnemy',335)
            || !Add(class'KFAIController',class'CheckKFAIController','SpecialBumpHandling',1818)
            || !Add(class'KFAIController',class'CheckKFAIController','GetHasAcceptableEnemyCount',746)
            || !Add(class'KFAIController_Monster',class'CheckKFAIController_Monster','CanGrabAttack',1510)
            || !Add(class'KFAIController_ZedFleshpoundKing',class'CheckKFAIController_ZedFleshpoundKing','CheckForBeamAttack',1054)
            || !Add(class'KFAIDirector',class'CheckKFAIDirector','RegisterAIMember',607,'CheckRegisterAIMember'))
            Trim(1);
    }
    `log("ZedSystemAuto: netmode" @ NetMode @ "active fixes" @ Active.Length);
}
