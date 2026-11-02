param([Parameter(Mandatory)][string] $CompiledModule)

$ErrorActionPreference = 'Stop'
# KFEditor can compile the field layout, but its source language cannot describe
# UStruct.Script. Bind the single return cast in our unpublished ScriptView to
# Core.Function, the same class the SDK already encoded for both named lookups.
$bytes = [IO.File]::ReadAllBytes($CompiledModule)
$binary = [Text.Encoding]::GetEncoding(28591).GetString($bytes)
$target = 'KFGame.KFPawn.AdjustDamage'
$helper = 'KF2OptimizerTelemetry.KF2OptimizerDamageGuard.IsHuman'
$pattern = '(?s)\x04\x2e(?<view>.{4})\x1c.{4}\x45\x2d\x00.{4}' +
    '\x1c\x00\x1f' + [Regex]::Escape($target) + '\x00' +
    [Regex]::Escape([string][char]($helper.Length + 2)) + '\x00\x1f' +
    [Regex]::Escape($helper) + '\x00\x20(?<function>.{4})\x16\x04\x3a.{4}\x53'
$sites = [Regex]::Matches($binary, $pattern)
if ($sites.Count -ne 1) { throw 'Unrecognized SDK output for the damage guard.' }
$source = $sites[0].Groups['function'].Index
$destination = $sites[0].Groups['view'].Index
if ([BitConverter]::ToInt32($bytes, $source) -ge 0 -or
    [BitConverter]::ToInt32($bytes, $destination) -le 0) {
    throw 'Expected a Core.Function import and a local guard class export.'
}
[Array]::Copy($bytes, $source, $bytes, $destination, 4)
# Resolve(string) uses the same FindObject function and the same Core.Function import.
$lookup = [Regex]::Escape($binary.Substring($sites[0].Index + 7, 4))
$guard = [Regex]::Escape($binary.Substring($destination, 4))
$functionImport = [Regex]::Escape($binary.Substring($source, 4))
$resolve = [Regex]::Matches($binary, '(?s)\x04\x2e(?<view>' + $guard +
    ')\x1c' + $lookup + '\x00.{4}\x20' + $functionImport + '\x16\x04\x3a.{4}\x53')
if ($resolve.Count -ne 1) { throw 'Unrecognized SDK output for the Zed function lookup.' }
[Array]::Copy($bytes, $source, $bytes, $resolve[0].Groups['view'].Index, 4)
[IO.File]::WriteAllBytes($CompiledModule, $bytes)
