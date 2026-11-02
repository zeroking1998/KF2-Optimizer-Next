// Installs the SDK-equivalent null-safe human-Pawn test on the game thread.
class KF2OptimizerDamageGuard extends Object;

// KF2 Win64 10897 / CL 1532151. UObject ends at 0x60; the VM reads
// UStruct.Script at 0x8c (data) and 0x94 (length), with four-byte packing.
var private byte PaddingToScript[41];
var array<byte> script;

// The SDK build binds the return cast to the same Core.Function type as FindObject.
// Both accessors use a typed Core.Function lookup.
static private final function KF2OptimizerDamageGuard ScriptView(bool Target)
{
    return KF2OptimizerDamageGuard(FindObject(Target ? "KFGame.KFPawn.AdjustDamage" : "KF2OptimizerTelemetry.KF2OptimizerDamageGuard.IsHuman", class'Core.Function'));
}

static final function KF2OptimizerDamageGuard Resolve(string FunctionPath)
{
    return KF2OptimizerDamageGuard(FindObject(FunctionPath, class'Core.Function'));
}

// Supplies the linked UClass and FName for this process. The game never calls
// this helper on a hit. Its two constants let us match the original class name
// and use the SDK's object-to-bool conversion and typed cast in its place.
static private final function bool IsHuman(Pawn Candidate, out name OriginalName)
{
    OriginalName = 'KFPawn_Human';
    return bool(KFPawn_Human(Candidate));
}

static function bool Install()
{
    local KF2OptimizerDamageGuard Target, Helper;
    local array<byte> Code;
    local int I;

    Target = ScriptView(true);
    Helper = ScriptView(false);
    if (Target == None || Helper == None || Target.script.Length != 0x66e)
        return false;
    // The actual template offsets are validated before using either constant.
    if (Helper.script.Length != 51 || Helper.script[0] != 0x0f ||
        Helper.script[10] != 0x21 || Helper.script[19] != 0x04 ||
        Helper.script[20] != 0x38 || Helper.script[21] != 0x47 || Helper.script[22] != 0x2e ||
        Helper.script[31] != 0x00)
        return false;
    Code = Target.script;
    if (Code[0x1f2] != 0x18 || Code[0x1f3] != 0x36 || Code[0x1f4] != 0 ||
        Code[0x1f5] != 0x19 || Code[0x1f6] != 0x19 || Code[0x1f7] != 0 ||
        Code[0x20b] != 0x01 || Code[0x21f] != 0xc5 || Code[0x220] != 0x21 ||
        Code[0x229] != 0x16 || Code[0x22a] != 0x16 || Code[0x22b] != 0x0f)
        return false;
    for (I = 0; I < 8; ++I)
        if (Code[0x221 + I] != Helper.script[11 + I])
            return false;

    // Keep the original InstigatedBy parameter and Controller.Pawn property.
    // Branch destinations and every instruction outside this expression stay
    // in place, including the original conditional jump around the modifier.
    Code[0x1f3] = 0x2a;
    for (I = 0; I < 11; ++I)
        Code[0x1f5 + I] = Helper.script[20 + I];
    for (I = 0; I < 30; ++I)
        Code[0x200 + I] = Target.script[0x1f6 + I];
    Code[0x21e] = 0x16; // EndFunctionParms for the enclosing short-circuit AND.
    Code[0x21f] = 0x06; // A matching human continues at the original body.
    Code[0x220] = 0x2b;
    Code[0x221] = 0x02;
    for (I = 0x222; I < 0x22b; ++I)
        Code[I] = 0x0b; // Valid, unreachable EX_Nothing tokens.
    Target.script = Code;
    return true;
}
