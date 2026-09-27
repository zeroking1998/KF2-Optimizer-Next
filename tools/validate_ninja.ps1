[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug',

    [switch] $PublicCI,

    [switch] $EnableCompilerCache
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$configurationName = $Configuration.ToLowerInvariant()
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot `
    "out\build\ninja-contract-$configurationName"))
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out\build'))
if (-not $buildRoot.StartsWith($allowedRoot.TrimEnd('\') + '\',
        [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing Ninja validation outside $allowedRoot"
}

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw 'vswhere.exe was not found'
}
$installation = (& $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath | Select-Object -First 1)
if ([string]::IsNullOrWhiteSpace($installation)) {
    throw 'A Visual Studio installation with MSVC x64 was not found'
}
$devShellModule = Join-Path $installation `
    'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
if (-not (Test-Path -LiteralPath $devShellModule -PathType Leaf)) {
    throw "Visual Studio developer shell module was not found: $devShellModule"
}
Import-Module $devShellModule
Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

$ninja = Get-ChildItem -LiteralPath (Join-Path $installation `
    'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja') `
    -Filter ninja.exe -File -Recurse | Select-Object -First 1 -ExpandProperty FullName
if ([string]::IsNullOrWhiteSpace($ninja)) {
    throw 'Visual Studio Ninja executable was not found'
}

if (Test-Path -LiteralPath $buildRoot) {
    Remove-Item -LiteralPath $buildRoot -Recurse -Force
}
$commit = (& git -C $projectRoot rev-parse --short=12 HEAD 2>$null)
if ([string]::IsNullOrWhiteSpace($commit)) { $commit = 'local' }
$dirty = (& git -C $projectRoot status --porcelain --untracked-files=normal 2>$null)
if ($dirty) { $commit = "$commit.dirty" }
$channel = if ($Configuration -eq 'Release') { 'release' } else { 'dev' }
$releaseRepository = (& git -C $projectRoot remote get-url origin 2>$null)
if ($releaseRepository -notmatch '^https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(?:\.git)?$') {
    $releaseRepository = (& git -C $projectRoot remote get-url upstream 2>$null)
}
if ($releaseRepository -match '\.git$') {
    $releaseRepository = $releaseRepository.Substring(
        0, $releaseRepository.Length - 4)
}
$telemetryModule = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerTelemetry.u'
$telemetryHash = if (Test-Path -LiteralPath $telemetryModule -PathType Leaf) {
    (Get-FileHash -LiteralPath $telemetryModule -Algorithm SHA256).Hash.ToLowerInvariant()
} else {
    '589aa708392e2c26abc753ce272c6e146f274623181015e8f6bdc201ccb8e2f0'
}

$configureArguments = @(
    '-S', $projectRoot,
    '-B', $buildRoot,
    '-G', 'Ninja',
    "-DCMAKE_MAKE_PROGRAM=$ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    '-DBUILD_TESTING=ON',
    '-DKF2_VERSION=0.0.4-alpha',
    "-DKF2_BUILD_COMMIT=$commit",
    "-DKF2_BUILD_CHANNEL=$channel",
    "-DKF2_OFFLINE_TELEMETRY_SHA256=$telemetryHash"
)
if ($releaseRepository -match '^https://github\.com/[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$') {
    $configureArguments += "-DKF2_RELEASE_REPOSITORY=$releaseRepository"
}
if ($EnableCompilerCache) {
    $sccache = Get-Command sccache -CommandType Application -ErrorAction Stop
    $configureArguments += "-DCMAKE_CXX_COMPILER_LAUNCHER=$($sccache.Source)"
    if ($Configuration -eq 'Debug') {
        $configureArguments += '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded'
    }
}

& cmake @configureArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $buildRoot --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$ctestArguments = @('--test-dir', $buildRoot, '--output-on-failure')
if ($PublicCI) {
    $ctestArguments += @('--label-exclude', 'requires-desktop')
}
& ctest @ctestArguments
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "PASS: Ninja $Configuration build and all tests passed"
