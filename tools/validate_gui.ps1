param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$buildName = "windows-x64-$($Configuration.ToLowerInvariant())"
$buildRoot = Join-Path $projectRoot "out\build\$buildName"
$outputRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $projectRoot 'out\gui-validation'))
$allowedRoot = [System.IO.Path]::GetFullPath((Join-Path $projectRoot 'out'))
if (-not $outputRoot.StartsWith($allowedRoot + [System.IO.Path]::DirectorySeparatorChar,
                                [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to clean GUI output outside $allowedRoot"
}

cmake -S $projectRoot -B $buildRoot -A x64
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed' }
cmake --build $buildRoot --config $Configuration --target kf2_direct2d_renderer_test
if ($LASTEXITCODE -ne 0) { throw 'Renderer test build failed' }

$testExe = Join-Path $buildRoot "tests\$Configuration\kf2_direct2d_renderer_test.exe"
if (Test-Path -LiteralPath $outputRoot) {
    Remove-Item -LiteralPath $outputRoot -Recurse -Force
}
New-Item -ItemType Directory -Path $outputRoot | Out-Null

& $testExe $outputRoot
if ($LASTEXITCODE -ne 0) { throw 'First GUI capture failed' }
$first = Get-ChildItem -LiteralPath $outputRoot -Filter '*.png' |
    Sort-Object Name |
    ForEach-Object { "{0} {1}" -f $_.Name, (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash }
if ($first.Count -ne 4) { throw "Expected 4 GUI captures, found $($first.Count)" }

& $testExe $outputRoot
if ($LASTEXITCODE -ne 0) { throw 'Second GUI capture failed' }
$second = Get-ChildItem -LiteralPath $outputRoot -Filter '*.png' |
    Sort-Object Name |
    ForEach-Object { "{0} {1}" -f $_.Name, (Get-FileHash -Algorithm SHA256 -LiteralPath $_.FullName).Hash }
if ((Compare-Object $first $second).Count -ne 0) {
    throw 'GUI captures are not deterministic'
}

Write-Host "GUI validation passed ($Configuration):"
$second | ForEach-Object { Write-Host "  $_" }

$releaseExe = Join-Path $projectRoot 'out\package\KF2OptimizerNext\KF2Optimizer.exe'
if ($Configuration -eq 'Release' -and (Test-Path -LiteralPath $releaseExe)) {
    & (Join-Path $PSScriptRoot 'validate_release_gui.ps1') -Executable $releaseExe
}
