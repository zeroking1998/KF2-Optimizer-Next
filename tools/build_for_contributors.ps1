[CmdletBinding()]
param(
    [Parameter()]
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release',

    [Parameter()]
    [switch] $Package,

    [Parameter()]
    [string] $TelemetrySeedModule = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))

$requirementArguments = @{}
if ($Package) {
    $requirementArguments.RequireKf2Sdk = $true
}
& (Join-Path $PSScriptRoot 'check_requirements.ps1') @requirementArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if ($Package) {
    $telemetryArguments = @{}
    $telemetryModule = Join-Path $projectRoot 'assets\offline_telemetry\KF2OptimizerTelemetry.u'
    if (-not [string]::IsNullOrWhiteSpace($TelemetrySeedModule)) {
        $telemetryArguments.SeedModule = $TelemetrySeedModule
    } elseif (-not (Test-Path -LiteralPath $telemetryModule -PathType Leaf)) {
        $telemetryArguments.SeedModule = (
            & (Join-Path $PSScriptRoot 'download_telemetry_seed.ps1') |
                Select-Object -Last 1)
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
    & (Join-Path $PSScriptRoot 'build_kf2_telemetry.ps1') @telemetryArguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & (Join-Path $PSScriptRoot 'test.ps1') -Configuration Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & cmake --build (Join-Path $projectRoot 'out\build\windows-x64-release') `
        --config Release --target KF2InventoryExport
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & (Join-Path $PSScriptRoot 'package.ps1') -SkipBuild
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & (Join-Path $PSScriptRoot 'validate_release.ps1')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    Write-Host ''
    Write-Host 'Complete portable package:' -ForegroundColor Green
    Write-Host (Join-Path $projectRoot 'out\package\KF2OptimizerNext')
    exit 0
}

& (Join-Path $PSScriptRoot 'test.ps1') -Configuration $Configuration
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ''
Write-Host 'Tested application build:' -ForegroundColor Green
$configurationPath = $Configuration.ToLowerInvariant()
Write-Host (Join-Path $projectRoot (
    "out\build\windows-x64-$configurationPath\$Configuration\KF2Optimizer.exe"))
