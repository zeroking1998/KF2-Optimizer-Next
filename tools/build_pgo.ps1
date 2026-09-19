[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [ValidateSet('Instrument', 'Optimize')]
    [string] $Phase,

    [Parameter()]
    [switch] $ResetProfile
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$outputRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out\pgo'))
$buildRoot = Join-Path $outputRoot 'build'
$executablePath = Join-Path $buildRoot 'Release\KF2Optimizer.exe'
$trainingOutputRoot = Split-Path -Parent $executablePath
$pgdPath = Join-Path $trainingOutputRoot 'KF2Optimizer.pgd'

function Get-MsvcToolDirectory {
    $compilerState = Get-ChildItem -LiteralPath `
        (Join-Path $buildRoot 'CMakeFiles') -Recurse -File `
        -Filter 'CMakeCXXCompiler.cmake' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $compilerMatch = if ($null -ne $compilerState) {
        Select-String -LiteralPath $compilerState.FullName `
            -Pattern '^set\(CMAKE_CXX_COMPILER "([^"]+)"\)$' |
            Select-Object -First 1
    }
    if ($null -eq $compilerMatch) {
        throw 'CMake did not record the MSVC compiler path'
    }
    return Split-Path -Parent $compilerMatch.Matches[0].Groups[1].Value
}

if ($ResetProfile) {
    if (-not $trainingOutputRoot.StartsWith($outputRoot.TrimEnd('\') + '\',
            [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to clear a PGO profile outside the project output directory'
    }
    if (Test-Path -LiteralPath $trainingOutputRoot -PathType Container) {
        Get-ChildItem -LiteralPath $trainingOutputRoot -File |
            Where-Object {
                $_.Name -like 'KF2Optimizer*.pgc' -or
                $_.Name -eq 'KF2Optimizer.pgd'
            } | Remove-Item -Force
    }
}

if ($Phase -eq 'Optimize') {
    $profileFiles = @(Get-ChildItem -LiteralPath $trainingOutputRoot -File `
        -Filter 'KF2Optimizer*.pgc' -ErrorAction SilentlyContinue)
    if ($profileFiles.Count -eq 0 -or
        -not (Test-Path -LiteralPath $pgdPath -PathType Leaf)) {
        throw ('No PGO training data exists. Build the Instrument phase, run ' +
            'the instrumented app through representative workflows, close it ' +
            'normally, and then run the Optimize phase.')
    }
}

$commit = (& git -C $projectRoot rev-parse --short=12 HEAD 2>$null)
if ([string]::IsNullOrWhiteSpace($commit)) { $commit = 'local' }
$dirty = (& git -C $projectRoot status --porcelain --untracked-files=normal 2>$null)
if ($dirty) { $commit = "$commit.dirty" }
$telemetryModule = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerTelemetry.u'
$telemetryHash = if (Test-Path -LiteralPath $telemetryModule -PathType Leaf) {
    (Get-FileHash -LiteralPath $telemetryModule -Algorithm SHA256).Hash.ToLowerInvariant()
} else {
    '589aa708392e2c26abc753ce272c6e146f274623181015e8f6bdc201ccb8e2f0'
}
$mode = if ($Phase -eq 'Instrument') { 'INSTRUMENT' } else { 'OPTIMIZE' }

Push-Location -LiteralPath $projectRoot
try {
    & cmake -S . -B $buildRoot -G 'Visual Studio 17 2022' -A x64 `
        '-DBUILD_TESTING=OFF' '-DKF2_VERSION=0.0.4-alpha' `
        "-DKF2_BUILD_COMMIT=$commit" '-DKF2_BUILD_CHANNEL=release' `
        "-DKF2_OFFLINE_TELEMETRY_SHA256=$telemetryHash" `
        "-DKF2_PGO_MODE=$mode" "-DKF2_PGO_PGD=$pgdPath"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    & cmake --build $buildRoot --config Release --target KF2Optimizer --parallel 4
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Pop-Location
}

if ($Phase -eq 'Instrument') {
    $pgoRuntime = Join-Path (Get-MsvcToolDirectory) 'pgort140.dll'
    if (-not (Test-Path -LiteralPath $pgoRuntime -PathType Leaf)) {
        throw "MSVC PGO training runtime is missing: $pgoRuntime"
    }
    Copy-Item -LiteralPath $pgoRuntime -Destination `
        (Join-Path (Split-Path -Parent $executablePath) 'pgort140.dll') -Force

    Write-Host ''
    Write-Host 'Instrumented PGO build ready:' -ForegroundColor Green
    Write-Host $executablePath
    Write-Host ('Run representative Home, graphics, overlay, diagnostics, ' +
        'update-check and KF2-session workflows, then close the app normally.')
    Write-Host 'After training, run:'
    Write-Host '  ./tools/build_pgo.ps1 -Phase Optimize'
} else {
    $trainingRuntime = Join-Path `
        (Split-Path -Parent $executablePath) 'pgort140.dll'
    if (Test-Path -LiteralPath $trainingRuntime -PathType Leaf) {
        Remove-Item -LiteralPath $trainingRuntime -Force
    }
    Write-Host ''
    Write-Host 'Profile-optimized build ready:' -ForegroundColor Green
    Write-Host $executablePath
    Write-Host "Training profiles consumed: $($profileFiles.Count)"
}
