[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $SdkRoot,
    [Parameter(Mandatory)][string] $Destination
)

$ErrorActionPreference = 'Stop'
# Construct only the selected v1150 functions from the builder's installed SDK.
# Pin the finished source so SDK revisions or an incorrect slice fail before compilation.
function Write-Donor([string] $Source, [string] $Expected, [scriptblock] $Body) {
    $lines = [IO.File]::ReadAllLines((Join-Path $SdkRoot "Development/Src/$Source"))
    $name = 'Check' + [IO.Path]::GetFileName($Source)
    $text = ((& $Body) -join "`n").Replace("`r`n", "`n")
    $utf8 = [Text.UTF8Encoding]::new($false)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($sha.ComputeHash($utf8.GetBytes($text))).Replace('-', '') }
    finally { $sha.Dispose() }
    if ($hash -ne $Expected) { throw "Incompatible SDK source for $name" }
    [IO.File]::WriteAllText((Join-Path $Destination $name), $text.Replace("`n", "`r`n"), $utf8)
}

function Copy-Lines([int] $First, [int] $Last) {
    $lines[($First - 1)..($Last - 1)]
}

Write-Donor 'KFGame/Classes/KFAIController.uc' '0c97d77719604999a18771c72dc27197ba252279a53558ba7010ec593ce8c186' {
    @'
class CheckKFAIController extends KFAIController;

'@
    # FindNewEnemy
    Copy-Lines 1325 1365
    @'
				if( BestEnemyZedCount == 0 )
				{
					continue;
				}
'@
    Copy-Lines 1366 1395
    ''
    # GetHasAcceptableEnemyCount
    Copy-Lines 1398 1406
    Copy-Lines 1408 1412
    Copy-Lines 1414 1419
    Copy-Lines 1421 1424
    '            NumZedsTargetingCurrentEnemy = NumberOfZedsTargetingPawn( Enemy,false,1000 );'
    Copy-Lines 1425 1429
    Copy-Lines 1431 1436
    ''
    # SpecialBumpHandling
    Copy-Lines 4438 4442
    @'
	if( !bBumpedThisFrame && CurBumpVal <= 0.01 && !MyKFPawn.bReducedZedOnZedPinchPointCollisionStateActive
		&& !bShowCollisionRadiusForReducedZedOnZedPinchPointCollisionState )
	{
		return;
	}

'@
    Copy-Lines 4443 4446
    Copy-Lines 4448 4460
    Copy-Lines 4462 4481
    Copy-Lines 4483 4519
    ''
    # OtherZedsTargetingMyEnemy
    Copy-Lines 2258 2269
    '		if( KFAIC != self && KFAIC.Enemy == Enemy && KFAIC.Pawn != none && KFAIC.Pawn.IsAliveAndWell() )'
    Copy-Lines 2271 2277
    ''
}

Write-Donor 'KFGame/Classes/KFAIController_Monster.uc' '8e835f085a4747a003954cb440730acd211ec21a6c6be12d3f009ab4c81c4440' {
    @'
class CheckKFAIController_Monster extends KFAIController_Monster;

'@
    # CanGrabAttack
    Copy-Lines 261 271
    @'
	{
		return false;
	}

	// Reject targets outside grab range before checking perks and special moves.
	if( Abs(Enemy.Location.Z - Pawn.Location.Z) > class'KFSM_GrappleCombined'.default.MaxVictimZOffset )
	{
		return false;
	}

	DistSq = VSizeSq(Enemy.Location - Pawn.Location);
	if( DistSq > MinDistanceToPerformGrabAttack * MinDistanceToPerformGrabAttack )
'@
    Copy-Lines 272 300
    '		if( MyKFPawn.IsPawnMovingAwayFromMe(Enemy, 300.f) )'
    Copy-Lines 309 333
    Copy-Lines 335 336
    ''
}

Write-Donor 'KFGameContent/Classes/KFAIController_ZedFleshpoundKing.uc' '28e6def818bc0433cdf98e0de8cd25366cd04f86cb507e9cf15eac7d718de010' {
    @'
class CheckKFAIController_ZedFleshpoundKing extends KFAIController_ZedFleshpoundKing;

'@
    # CheckForBeamAttack
    Copy-Lines 91 145
    '		if( DotAngle > MinChestBeamTargetAngle && Abs(Projection.Z) < MaxChestBeamHeightOffset )'
    Copy-Lines 147 149
    '			if( (BestRating == -1.f || Rating < BestRating) && MyKFPawn.FastTrace(P.Location, MyKFPawn.Location) )'
    Copy-Lines 151 170
    ''
}

Write-Donor 'KFGame/Classes/KFAIDirector.uc' '59591cc1085d3cd2113998522636d9cef217adda9a566326c330b5e89dfeb191' {
    @'
class CheckKFAIDirector extends KFAIDirector;

'@
    # RegisterAIMember
    'final function CheckRegisterAIMember( Controller NewMember )'
    Copy-Lines 184 187
    '	if (NewMember == none || ActiveZeds.Find(''Member'', NewMember) != INDEX_NONE)'
    Copy-Lines 189 189
    @'
		return;
	}
'@
    Copy-Lines 197 197
    @'
	Idx = ActiveZeds.Length;
	ActiveZeds.Length = Idx + 1;
	ActiveZeds[Idx].Member = NewMember;
	KFAIC = KFAIController(NewMember);
	// if it's an AI
	if( KFAIC != None )
	{
		AIList.AddItem(KFAIC);
	}
'@
    Copy-Lines 208 208
    @'
	if (NewMember.Pawn != none && KFPawn_Monster(NewMember.Pawn) != none && KFPawn_Monster(NewMember.Pawn).IsABoss())
	{
		ActiveBossList.AddItem(KFPawn_Monster(NewMember.Pawn));
'@
    Copy-Lines 214 215
    ''
}

Write-Donor 'KFGame/Classes/KFPawn_Monster.uc' '1c64ec339c1d30539e8fe2d6fb041a37856c3ac9d1da22bbb60680b1f8a4ef28' {
    @'
class CheckKFPawn_Monster extends KFPawn_Monster;

'@
    # Tick
    Copy-Lines 1101 1102
    '	super(KFPawn).Tick(DeltaTime);'
    Copy-Lines 1104 1104
    @'
	if (WorldInfo.NetMode != NM_DedicatedServer
		&& (bPlayingSprintLoop || (bIsSprinting && (SprintLoopingSound != none || StartSprintingSound != none)))
		&& IsAliveAndWell())
'@
    Copy-Lines 1106 1137
    ''
}
