[CmdletBinding()]
param(
    [Parameter()]
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$preset = "windows-x64-$($Configuration.ToLowerInvariant())"

function Invoke-Native([string] $Command, [string[]] $Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Native command failed ($LASTEXITCODE): $Command $($Arguments -join ' ')"
    }
}

Push-Location -LiteralPath $projectRoot
try {
    Invoke-Native 'cmake' @('--preset', $preset)
    Invoke-Native 'cmake' @('--build', '--preset', $preset, '--parallel')
    $tests = 'kf2_(game_session|system_metrics|present_source|presentmon_session|presentmon_dxgi_present|gpu_metrics|overlay_policy|overlay_window|application_lifecycle)_test'
    Invoke-Native 'ctest' @('--preset', $preset, '-R', $tests, '--output-on-failure')
    Write-Host 'PASS: PresentMon produced non-zero identity-bound application FPS' -ForegroundColor Green
    Write-Host 'PASS: CPU/RAM, GPU/VRAM, process/window identity, overlay policy and overlay soak passed' -ForegroundColor Green
    Write-Host 'PASS: overlay toggle and clean application shutdown passed' -ForegroundColor Green
}
finally {
    Pop-Location
}
