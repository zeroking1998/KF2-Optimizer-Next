[CmdletBinding()]
param(
    [Parameter()]
    [string] $SdkRoot = '',

    [Parameter()]
    [string] $Kf2UserRoot = '',

    [Parameter()]
    [string] $OutputPath = '',

    [Parameter()]
    [string] $SeedModule = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))

function Find-Kf2SdkRoot {
    if (-not [string]::IsNullOrWhiteSpace($SdkRoot)) {
        return [IO.Path]::GetFullPath($SdkRoot)
    }
    if (-not [string]::IsNullOrWhiteSpace($env:KF2_SDK_ROOT)) {
        return [IO.Path]::GetFullPath($env:KF2_SDK_ROOT)
    }

    $candidates = [Collections.Generic.List[string]]::new()
    try {
        $steamPath = (Get-ItemProperty -LiteralPath `
            'HKCU:\Software\Valve\Steam' -Name SteamPath `
            -ErrorAction Stop).SteamPath
        if (-not [string]::IsNullOrWhiteSpace($steamPath)) {
            $candidates.Add((Join-Path $steamPath `
                'steamapps\common\killingfloor2'))
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
    throw 'KF2 SDK was not found. Install it through Steam or pass -SdkRoot.'
}

if ([string]::IsNullOrWhiteSpace($Kf2UserRoot)) {
    $documents = [Environment]::GetFolderPath(
        [Environment+SpecialFolder]::MyDocuments)
    $Kf2UserRoot = Join-Path $documents 'My Games\KillingFloor2'
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $projectRoot `
        'assets\offline_telemetry\KF2OptimizerTelemetry.u'
}

$resolvedSdkRoot = Find-Kf2SdkRoot
$resolvedUserRoot = [IO.Path]::GetFullPath($Kf2UserRoot)
$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)
if ([string]::IsNullOrWhiteSpace($SeedModule)) {
    $SeedModule = $resolvedOutput
}
$resolvedSeed = [IO.Path]::GetFullPath($SeedModule)
$editorPath = Join-Path $resolvedSdkRoot 'Binaries\Win64\KFEditor.exe'
$configPath = Join-Path $resolvedUserRoot 'KFGame\Config\KFEngine.ini'
$packageRoot = Join-Path $resolvedSdkRoot `
    'Development\Src\KF2OptimizerTelemetry'
$classesRoot = Join-Path $packageRoot 'Classes'
$compiledPath = Join-Path $resolvedUserRoot `
    'KFGame\Unpublished\BrewedPC\Script\KF2OptimizerTelemetry.u'
$probeSource = Join-Path $projectRoot `
    'assets\offline_telemetry\kf2optimizertelemetryprobe.uc'
$viewportSource = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerTelemetryViewport.uc'

foreach ($required in @($editorPath, $configPath, $probeSource,
                         $viewportSource)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required KF2 telemetry build input is missing: $required"
    }
}
if (-not (Test-Path -LiteralPath $resolvedSeed -PathType Leaf)) {
    throw ('The KF2 compiler requires a previous telemetry module as its ' +
        'bootstrap seed. Download it from the latest complete release and ' +
        'pass -SeedModule <path>.')
}
if (Test-Path -LiteralPath $packageRoot) {
    throw "The SDK staging package already exists and was preserved: $packageRoot"
}
if (Get-Process -Name KFGame, KFEditor -ErrorAction SilentlyContinue) {
    throw 'Close KF2 and KFEditor before compiling the telemetry module.'
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('KF2OptimizerTelemetry-' + [Guid]::NewGuid().ToString('N'))
$configBackup = Join-Path $temporaryRoot 'KFEngine.ini'
$compiledBackup = Join-Path $temporaryRoot 'KF2OptimizerTelemetry.u'
$hadCompiledPackage = Test-Path -LiteralPath $compiledPath -PathType Leaf
$configHashBefore = (Get-FileHash -LiteralPath $configPath `
    -Algorithm SHA256).Hash

New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
Copy-Item -LiteralPath $configPath -Destination $configBackup
if ($hadCompiledPackage) {
    Copy-Item -LiteralPath $compiledPath -Destination $compiledBackup
}

$buildSucceeded = $false
try {
    New-Item -ItemType Directory -Path $classesRoot -Force | Out-Null
    Copy-Item -LiteralPath $probeSource -Destination `
        (Join-Path $classesRoot 'KF2OptimizerTelemetryProbe.uc')
    Copy-Item -LiteralPath $viewportSource -Destination `
        (Join-Path $classesRoot 'KF2OptimizerTelemetryViewport.uc')
    # KFEditor uses source timestamps for its incremental make decision. Touch
    # only the disposable staging copies so a missing output is really rebuilt.
    $stagedTimestamp = [DateTime]::UtcNow.AddSeconds(2)
    Get-ChildItem -LiteralPath $classesRoot -File -Filter '*.uc' |
        ForEach-Object { $_.LastWriteTimeUtc = $stagedTimestamp }

    $configText = [IO.File]::ReadAllText($configPath)
    if ($configText -notmatch `
        '(?m)^EditPackages=KF2OptimizerTelemetry\s*$') {
        $newline = if ($configText.Contains("`r`n")) { "`r`n" } else { "`n" }
        $anchor = 'EditPackages=RCam'
        $anchorIndex = $configText.IndexOf(
            $anchor, [StringComparison]::OrdinalIgnoreCase)
        if ($anchorIndex -lt 0) {
            throw 'KFEngine.ini has no EditPackages=RCam anchor.'
        }
        $insertIndex = $anchorIndex + $anchor.Length
        $configText = $configText.Insert(
            $insertIndex, $newline + 'EditPackages=KF2OptimizerTelemetry')
        [IO.File]::WriteAllText(
            $configPath, $configText, [Text.UTF8Encoding]::new($false))
    }

    if (Test-Path -LiteralPath $compiledPath -PathType Leaf) {
        Remove-Item -LiteralPath $compiledPath -Force
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $compiledPath) `
        -Force | Out-Null
    Copy-Item -LiteralPath $resolvedSeed -Destination $compiledPath -Force
    $process = Start-Process -FilePath $editorPath -ArgumentList @(
        'make', '-useunpublished', '-unattended', '-nopause'
    ) -WorkingDirectory (Split-Path -Parent $editorPath) -Wait -PassThru `
        -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "KFEditor compilation failed with exit code $($process.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $compiledPath -PathType Leaf)) {
        throw "KFEditor reported success but did not create: $compiledPath"
    }

    $outputDirectory = Split-Path -Parent $resolvedOutput
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    Copy-Item -LiteralPath $compiledPath -Destination $resolvedOutput -Force
    $buildSucceeded = $true
}
finally {
    Copy-Item -LiteralPath $configBackup -Destination $configPath -Force
    if (Test-Path -LiteralPath $packageRoot) {
        Remove-Item -LiteralPath $packageRoot -Recurse -Force
    }
    if ($hadCompiledPackage) {
        Copy-Item -LiteralPath $compiledBackup -Destination $compiledPath -Force
    }
    elseif (Test-Path -LiteralPath $compiledPath -PathType Leaf) {
        Remove-Item -LiteralPath $compiledPath -Force
    }
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

$configHashAfter = (Get-FileHash -LiteralPath $configPath `
    -Algorithm SHA256).Hash
if ($configHashAfter -ne $configHashBefore) {
    throw 'KFEngine.ini was not restored byte-for-byte after compilation.'
}
if (-not $buildSucceeded) {
    throw 'KF2 telemetry compilation did not complete.'
}

$hash = (Get-FileHash -LiteralPath $resolvedOutput -Algorithm SHA256).Hash
Write-Host "PASS: KF2 telemetry module compiled: $resolvedOutput"
Write-Host "SHA256: $hash"
Write-Host 'PASS: KFEngine.ini and SDK staging state restored'
