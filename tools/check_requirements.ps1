[CmdletBinding()]
param(
    [Parameter()]
    [switch] $RequireKf2Sdk,

    [Parameter()]
    [string] $SdkRoot = ''
)

$ErrorActionPreference = 'Stop'
$missing = [Collections.Generic.List[string]]::new()

function Write-Result {
    param(
        [Parameter(Mandatory)]
        [bool] $Passed,

        [Parameter(Mandatory)]
        [string] $Name,

        [Parameter(Mandatory)]
        [string] $Details
    )

    $label = if ($Passed) { 'PASS' } else { 'MISSING' }
    $color = if ($Passed) { 'Green' } else { 'Yellow' }
    Write-Host ("{0,-8} {1}: {2}" -f $label, $Name, $Details) -ForegroundColor $color
}

function Find-Kf2SdkRoot {
    param([string] $RequestedRoot)

    $candidates = [Collections.Generic.List[string]]::new()
    if (-not [string]::IsNullOrWhiteSpace($RequestedRoot)) {
        $candidates.Add($RequestedRoot)
    }
    if (-not [string]::IsNullOrWhiteSpace($env:KF2_SDK_ROOT)) {
        $candidates.Add($env:KF2_SDK_ROOT)
    }
    try {
        $steamPath = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction Stop).SteamPath
        if (-not [string]::IsNullOrWhiteSpace($steamPath)) {
            $candidates.Add((Join-Path $steamPath 'steamapps\common\killingfloor2'))
        }
    }
    catch {}
    $candidates.Add('C:\Program Files (x86)\Steam\steamapps\common\killingfloor2')
    $candidates.Add('D:\Steam\steamapps\common\killingfloor2')
    $candidates.Add('E:\Steam\steamapps\common\killingfloor2')

    foreach ($candidate in $candidates) {
        $editor = Join-Path $candidate 'Binaries\Win64\KFEditor.exe'
        if (Test-Path -LiteralPath $editor -PathType Leaf) {
            return [IO.Path]::GetFullPath($candidate)
        }
    }
    return ''
}

$windowsSupported =
    [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT -and
    [Environment]::OSVersion.Version.Major -ge 10
Write-Result $windowsSupported 'Windows' 'Windows 10 or newer is required.'
if (-not $windowsSupported) { $missing.Add('Windows 10 or newer') }

$powerShellOk = $PSVersionTable.PSVersion.Major -ge 7
Write-Result $powerShellOk 'PowerShell' ("Version $($PSVersionTable.PSVersion); version 7 or newer is required.")
if (-not $powerShellOk) { $missing.Add('PowerShell 7') }

$git = Get-Command git -ErrorAction SilentlyContinue
$gitDetails = if ($git) { (& git --version) } else { 'Install Git for Windows.' }
Write-Result ($null -ne $git) 'Git' $gitDetails
if (-not $git) { $missing.Add('Git for Windows') }

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
$cmakeVersion = $null
if ($cmake) {
    $versionText = (& cmake --version | Select-Object -First 1)
    if ($versionText -match '(\d+\.\d+\.\d+)') {
        $cmakeVersion = [Version]$Matches[1]
    }
}
$cmakeOk = $null -ne $cmakeVersion -and $cmakeVersion -ge [Version]'3.28.0'
$cmakeDetails = if ($cmakeVersion) {
    "Version $cmakeVersion; version 3.28 or newer is required."
} else {
    'Install CMake 3.28 or newer.'
}
Write-Result $cmakeOk 'CMake' $cmakeDetails
if (-not $cmakeOk) { $missing.Add('CMake 3.28 or newer') }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudioPath = ''
if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
    $vswhereArguments = @(
        '-latest',
        '-version',
        '[17.0,18.0)',
        '-products',
        '*',
        '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '-property',
        'installationPath'
    )
    $visualStudioPath = (
        & $vswhere @vswhereArguments | Select-Object -First 1)
}
$visualStudioOk = -not [string]::IsNullOrWhiteSpace($visualStudioPath)
$visualStudioDetails = if ($visualStudioOk) {
    $visualStudioPath
} else {
    'Install Visual Studio 2022 C++ Build Tools using .vsconfig.'
}
Write-Result $visualStudioOk 'MSVC x64' $visualStudioDetails
if (-not $visualStudioOk) { $missing.Add('Visual Studio 2022 C++ Build Tools') }

$kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Include'
$sdkVersion = if (Test-Path -LiteralPath $kitsRoot -PathType Container) {
    Get-ChildItem -LiteralPath $kitsRoot -Directory |
        Where-Object { $_.Name -match '^10\.0\.\d+\.0$' } |
        Sort-Object { [Version]$_.Name } -Descending |
        Select-Object -First 1 -ExpandProperty Name
} else {
    $null
}
$sdkOk = -not [string]::IsNullOrWhiteSpace($sdkVersion)
$sdkDetails = if ($sdkOk) {
    "Version $sdkVersion"
} else {
    'Install a Windows 10 or 11 SDK using .vsconfig.'
}
Write-Result $sdkOk 'Windows SDK' $sdkDetails
if (-not $sdkOk) { $missing.Add('Windows 10 or 11 SDK') }

$resolvedKf2Sdk = Find-Kf2SdkRoot -RequestedRoot $SdkRoot
$kf2SdkOk = -not [string]::IsNullOrWhiteSpace($resolvedKf2Sdk)
$kf2Details = if ($kf2SdkOk) {
    $resolvedKf2Sdk
} elseif ($RequireKf2Sdk) {
    'Install the official Killing Floor 2 SDK through Steam.'
} else {
    'Optional for the app build; required for a complete portable package.'
}
Write-Result $kf2SdkOk 'KF2 SDK' $kf2Details
if ($RequireKf2Sdk -and -not $kf2SdkOk) {
    $missing.Add('official Killing Floor 2 SDK')
}

if ($missing.Count -gt 0) {
    Write-Host ''
    Write-Host 'The development environment is not ready:' -ForegroundColor Red
    $missing | ForEach-Object { Write-Host "  - $_" }
    Write-Host 'See docs/BUILDING.md for installation links and commands.'
    exit 1
}

Write-Host ''
Write-Host 'Ready to build KF2 Optimizer Next.' -ForegroundColor Green
if (-not $kf2SdkOk) {
    Write-Host ('The native app can be built and tested now. Install the KF2 ' +
        'SDK before creating a complete portable package.')
}
