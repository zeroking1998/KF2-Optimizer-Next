[CmdletBinding()]
param(
    [Parameter()]
    [string] $SourceRoot = ''
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SourceRoot)) {
    $projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
    $SourceRoot = Join-Path $projectRoot 'assets\offline_telemetry'
}
$resolvedSourceRoot = [IO.Path]::GetFullPath($SourceRoot)
$sourceNames = @(
    'kf2optimizertelemetryprobe.uc'
    'KF2OptimizerTelemetryMutator.uc'
    'KF2OptimizerTelemetryInteraction.uc'
    'KF2OptimizerAdaptiveControlListener.uc'
    'KF2OptimizerAdaptiveControlConnection.uc'
    'KF2OptimizerAdaptiveGraphics.uc'
    'KF2OptimizerAdaptiveGraphicsState.uc'
)

$manifestLines = foreach ($sourceName in $sourceNames) {
    $sourcePath = Join-Path $resolvedSourceRoot $sourceName
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Required KF2 telemetry source is missing: $sourcePath"
    }
    $sourceHash = (Get-FileHash -LiteralPath $sourcePath `
        -Algorithm SHA256).Hash.ToLowerInvariant()
    "$sourceName|$sourceHash"
}
$manifest = ($manifestLines -join "`n") + "`n"
$manifestBytes = [Text.Encoding]::UTF8.GetBytes($manifest)
$sha256 = [Security.Cryptography.SHA256]::Create()
try {
    $fingerprintBytes = $sha256.ComputeHash($manifestBytes)
}
finally {
    $sha256.Dispose()
}
([BitConverter]::ToString($fingerprintBytes) -replace '-', '').ToLowerInvariant()
