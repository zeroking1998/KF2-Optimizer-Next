[CmdletBinding()]
param(
    [Parameter()]
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [Parameter()]
    [switch] $PublicCI
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$preset = "windows-x64-$($Configuration.ToLowerInvariant())"

& (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Push-Location -LiteralPath $projectRoot
try {
    $ctestArguments = @('--preset', $preset, '--output-on-failure')
    if ($PublicCI) {
        $ctestArguments += @('--label-exclude', 'requires-desktop')
    }
    & ctest @ctestArguments
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
