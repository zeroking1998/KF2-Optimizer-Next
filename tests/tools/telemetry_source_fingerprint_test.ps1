[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $FingerprintScript,

    [Parameter(Mandatory)]
    [string] $SourceRoot
)

$ErrorActionPreference = 'Stop'
$sourceNames = @(
    'kf2optimizertelemetryprobe.uc'
    'KF2OptimizerTelemetryMutator.uc'
    'KF2OptimizerTelemetryInteraction.uc'
    'KF2OptimizerAdaptiveControlListener.uc'
    'KF2OptimizerAdaptiveControlConnection.uc'
    'KF2OptimizerAdaptiveGraphics.uc'
    'KF2OptimizerAdaptiveGraphicsState.uc'
)
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('KF2TelemetryFingerprintTest-' + [Guid]::NewGuid().ToString('N'))

try {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    foreach ($sourceName in $sourceNames) {
        Copy-Item -LiteralPath (Join-Path $SourceRoot $sourceName) `
            -Destination (Join-Path $temporaryRoot $sourceName)
    }

    $expected = (& $FingerprintScript -SourceRoot $SourceRoot).Trim()
    $copied = (& $FingerprintScript -SourceRoot $temporaryRoot).Trim()
    if ($expected -notmatch '^[0-9a-f]{64}$' -or $copied -ne $expected) {
        throw 'Equivalent telemetry sources did not produce the same fingerprint.'
    }

    Add-Content -LiteralPath (Join-Path $temporaryRoot $sourceNames[0]) `
        -Value "`n// fingerprint regression mutation"
    $changed = (& $FingerprintScript -SourceRoot $temporaryRoot).Trim()
    if ($changed -eq $expected) {
        throw 'A telemetry source change did not invalidate the fingerprint.'
    }

    Remove-Item -LiteralPath (Join-Path $temporaryRoot $sourceNames[-1])
    $missingWasRejected = $false
    try {
        & $FingerprintScript -SourceRoot $temporaryRoot | Out-Null
    }
    catch {
        $missingWasRejected = $true
    }
    if (-not $missingWasRejected) {
        throw 'A missing telemetry source was not rejected.'
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Write-Host 'PASS: telemetry source fingerprint is stable and change-sensitive'
