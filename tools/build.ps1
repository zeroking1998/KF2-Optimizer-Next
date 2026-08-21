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

Push-Location -LiteralPath $projectRoot
try {
    & cmake --preset $preset "-DKF2_VERSION=0.0.2-alpha" `
        "-DKF2_BUILD_COMMIT=$commit" "-DKF2_BUILD_CHANNEL=$channel"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & cmake --build --preset $preset
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
