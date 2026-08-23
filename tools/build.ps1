[CmdletBinding()]
param(
    [Parameter()]
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$preset = "windows-x64-$($Configuration.ToLowerInvariant())"
$commit = (& git -C $projectRoot rev-parse --short=12 HEAD 2>$null)
if ([string]::IsNullOrWhiteSpace($commit)) { $commit = 'local' }
$dirty = (& git -C $projectRoot status --porcelain --untracked-files=normal 2>$null)
if ($dirty) { $commit = "$commit.dirty" }
$channel = if ($Configuration -eq 'Release') { 'release' } else { 'dev' }
$telemetryModule = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerTelemetry.u'
$telemetryHash = if (Test-Path -LiteralPath $telemetryModule -PathType Leaf) {
    (Get-FileHash -LiteralPath $telemetryModule -Algorithm SHA256).Hash.ToLowerInvariant()
} else {
    '589aa708392e2c26abc753ce272c6e146f274623181015e8f6bdc201ccb8e2f0'
}

Push-Location -LiteralPath $projectRoot
try {
    & cmake --preset $preset "-DKF2_VERSION=0.0.4-alpha" `
        "-DKF2_BUILD_COMMIT=$commit" "-DKF2_BUILD_CHANNEL=$channel" `
        "-DKF2_OFFLINE_TELEMETRY_SHA256=$telemetryHash"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & cmake --build --preset $preset
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
