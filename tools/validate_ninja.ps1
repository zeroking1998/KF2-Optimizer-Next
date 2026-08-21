[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string] $Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$buildRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out\build\ninja-contract'))
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

& cmake -S $projectRoot -B $buildRoot -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_BUILD_TYPE=$Configuration" `
    '-DBUILD_TESTING=ON' `
    "-DKF2_BUILD_COMMIT=$commit" `
    "-DKF2_BUILD_CHANNEL=$channel"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $buildRoot --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& ctest --test-dir $buildRoot --output-on-failure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "PASS: Ninja $Configuration build and all tests passed"
