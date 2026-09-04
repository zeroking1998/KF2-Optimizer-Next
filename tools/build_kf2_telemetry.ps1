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
$sdkConfigPath = Join-Path $resolvedUserRoot 'KFGame\Config\KFSDK.ini'
$packageRoot = Join-Path $resolvedUserRoot `
    'KFGame\Src\KF2OptimizerTelemetry'
$classesRoot = Join-Path $packageRoot 'Classes'
$compiledPath = Join-Path $resolvedUserRoot `
    'KFGame\Unpublished\BrewedPC\Script\KF2OptimizerTelemetry.u'
$publishedPath = Join-Path $resolvedUserRoot `
    'KFGame\Published\BrewedPC\KF2OptimizerTelemetry.u'
$shippingSeedPath = Join-Path $resolvedSdkRoot `
    'KFGame\BrewedPC\KF2OptimizerTelemetry.u'
$probeSource = Join-Path $projectRoot `
    'assets\offline_telemetry\kf2optimizertelemetryprobe.uc'
$mutatorSource = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerTelemetryMutator.uc'
$interactionSource = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerTelemetryInteraction.uc'
$adaptiveListenerSource = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerAdaptiveControlListener.uc'
$adaptiveConnectionSource = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerAdaptiveControlConnection.uc'
$adaptiveGraphicsSource = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerAdaptiveGraphics.uc'
$adaptiveGraphicsStateSource = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerAdaptiveGraphicsState.uc'
$nativeWakeTestSource = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerNativeWakeTest.uc'

foreach ($required in @($editorPath, $configPath,
                         $probeSource,
                         $mutatorSource, $interactionSource,
                         $adaptiveListenerSource,
                         $adaptiveConnectionSource, $adaptiveGraphicsSource,
                         $adaptiveGraphicsStateSource, $nativeWakeTestSource)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required KF2 telemetry build input is missing: $required"
    }
}
if (Test-Path -LiteralPath $packageRoot) {
    throw "The SDK staging package already exists and was preserved: $packageRoot"
}
if (-not (Test-Path -LiteralPath $resolvedSeed -PathType Leaf)) {
    throw ('The previous telemetry module is required for output verification. ' +
        'Pass -SeedModule <path>.')
}
if (Get-Process -Name KFGame, KFEditor -ErrorAction SilentlyContinue) {
    throw 'Close KF2 and KFEditor before compiling the telemetry module.'
}

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('KF2OptimizerTelemetry-' + [Guid]::NewGuid().ToString('N'))
$configBackup = Join-Path $temporaryRoot 'KFEngine.ini'
$sdkConfigBackup = Join-Path $temporaryRoot 'KFSDK.ini'
$compiledBackup = Join-Path $temporaryRoot 'KF2OptimizerTelemetry.u'
$publishedBackup = Join-Path $temporaryRoot `
    'published-KF2OptimizerTelemetry.u'
$shippingSeedBackup = Join-Path $temporaryRoot 'shipping-KF2OptimizerTelemetry.u'
$hadCompiledPackage = Test-Path -LiteralPath $compiledPath -PathType Leaf
$hadPublishedPackage = Test-Path -LiteralPath $publishedPath -PathType Leaf
$hadShippingSeed = Test-Path -LiteralPath $shippingSeedPath -PathType Leaf
$hadSdkConfig = Test-Path -LiteralPath $sdkConfigPath -PathType Leaf
$configHashBefore = (Get-FileHash -LiteralPath $configPath `
    -Algorithm SHA256).Hash
$sdkConfigHashBefore = if ($hadSdkConfig) {
    (Get-FileHash -LiteralPath $sdkConfigPath -Algorithm SHA256).Hash
} else {
    ''
}
$publishedHashBefore = if ($hadPublishedPackage) {
    (Get-FileHash -LiteralPath $publishedPath -Algorithm SHA256).Hash
} else {
    ''
}

New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
Copy-Item -LiteralPath $configPath -Destination $configBackup
if ($hadSdkConfig) {
    Copy-Item -LiteralPath $sdkConfigPath -Destination $sdkConfigBackup
}
if ($hadCompiledPackage) {
    Copy-Item -LiteralPath $compiledPath -Destination $compiledBackup
}
if ($hadPublishedPackage) {
    Copy-Item -LiteralPath $publishedPath -Destination $publishedBackup
}
if ($hadShippingSeed) {
    Copy-Item -LiteralPath $shippingSeedPath -Destination $shippingSeedBackup
}

$buildSucceeded = $false
try {
    New-Item -ItemType Directory -Path $classesRoot -Force | Out-Null
    Copy-Item -LiteralPath $probeSource -Destination `
        (Join-Path $classesRoot 'KF2OptimizerTelemetryProbe.uc')
    Copy-Item -LiteralPath $mutatorSource -Destination `
        (Join-Path $classesRoot 'KF2OptimizerTelemetryMutator.uc')
    Copy-Item -LiteralPath $interactionSource -Destination `
        (Join-Path $classesRoot 'KF2OptimizerTelemetryInteraction.uc')
    Copy-Item -LiteralPath $adaptiveGraphicsSource -Destination `
        (Join-Path $classesRoot 'KF2OptimizerAdaptiveGraphics.uc')
    Copy-Item -LiteralPath $adaptiveGraphicsStateSource -Destination `
        (Join-Path $classesRoot 'KF2OptimizerAdaptiveGraphicsState.uc')
    Copy-Item -LiteralPath $adaptiveListenerSource -Destination `
        (Join-Path $classesRoot 'KF2OptimizerAdaptiveControlListener.uc')
    Copy-Item -LiteralPath $adaptiveConnectionSource -Destination `
        (Join-Path $classesRoot 'KF2OptimizerAdaptiveControlConnection.uc')
    Copy-Item -LiteralPath $nativeWakeTestSource -Destination `
        (Join-Path $classesRoot 'KF2OptimizerNativeWakeTest.uc')
    # KFEditor uses source timestamps for its incremental make decision. Touch
    # only the disposable staging copies so a missing output is really rebuilt.
    # Copying the bootstrap package can give it the current timestamp on some
    # Windows filesystems. Keep staged sources unambiguously newer so KFEditor
    # cannot silently treat the seed module as an up-to-date build.
    $stagedTimestamp = [DateTime]::UtcNow.AddSeconds(2)
    Get-ChildItem -LiteralPath $classesRoot -File -Filter '*.uc' |
        ForEach-Object { $_.LastWriteTimeUtc = $stagedTimestamp }

    $sdkConfigText = @(
        '[ModPackages]'
        'ModPackages=KF2OptimizerTelemetry'
        'ModPackagesInPath=..\..\KFGame\Src'
        'ModOutputDir=..\..\KFGame\Unpublished\BrewedPC\Script'
        ''
    ) -join "`r`n"
    [IO.File]::WriteAllText(
        $sdkConfigPath, $sdkConfigText, [Text.UTF8Encoding]::new($false))

    if (Test-Path -LiteralPath $compiledPath -PathType Leaf) {
        Remove-Item -LiteralPath $compiledPath -Force
    }
    New-Item -ItemType Directory -Path (Split-Path -Parent $compiledPath) `
        -Force | Out-Null
    # The SDK mod compiler reads source from KFGame\Src. Keep the debug script
    # symbol enabled because the protected runtime protocol is carried by
    # explicit KF2OPT_* Launch.log receipts. A final-release script compile
    # removes those receipts and would make every live capability unverifiable.
    # Remove loadable binary copies transactionally so only staged source wins.
    if (Test-Path -LiteralPath $shippingSeedPath -PathType Leaf) {
        Remove-Item -LiteralPath $shippingSeedPath -Force
    }
    # Published modules are mounted ahead of staged source and can make
    # KFEditor report success without recompiling. Remove only the backed-up
    # package for the duration of this transaction.
    if (Test-Path -LiteralPath $publishedPath -PathType Leaf) {
        Remove-Item -LiteralPath $publishedPath -Force
    }
    $process = Start-Process -FilePath $editorPath -ArgumentList @(
        'make', '-debug', '-full', '-user', '-installed', '-modini',
        '-unattended', '-nopause'
    ) -WorkingDirectory (Split-Path -Parent $editorPath) -Wait -PassThru `
        -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "KFEditor compilation failed with exit code $($process.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $compiledPath -PathType Leaf)) {
        throw "KFEditor reported success but did not create: $compiledPath"
    }
    $compiledHash = (Get-FileHash -LiteralPath $compiledPath `
        -Algorithm SHA256).Hash
    $seedHash = (Get-FileHash -LiteralPath $resolvedSeed `
        -Algorithm SHA256).Hash
    if ($compiledHash -eq $seedHash) {
        throw ('KFEditor did not recompile the staged telemetry sources; ' +
            'the output still matches the bootstrap seed.')
    }
    # `make` produces an Unpublished development package. KF2's normal
    # installed runtime deliberately ignores that package unless it is started
    # with -useunpublished. Brew the freshly compiled package so the portable
    # app can stage a real Published\BrewedPC module without launch flags or a
    # write into the game installation.
    $brewProcess = Start-Process -FilePath $editorPath -ArgumentList @(
        'brewcontent', '-platform=PC', 'KF2OptimizerTelemetry',
        '-useunpublished', '-user', '-installed', '-modini', '-unattended',
        '-nopause'
    ) -WorkingDirectory (Split-Path -Parent $editorPath) -Wait -PassThru `
        -WindowStyle Hidden
    if ($brewProcess.ExitCode -ne 0) {
        throw "KFEditor brew failed with exit code $($brewProcess.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $publishedPath -PathType Leaf)) {
        throw "KFEditor reported a successful brew but did not create: $publishedPath"
    }
    $publishedHash = (Get-FileHash -LiteralPath $publishedPath `
        -Algorithm SHA256).Hash
    if ($publishedHash -eq $seedHash) {
        throw ('KFEditor did not brew the freshly compiled telemetry sources; ' +
            'the published output still matches the bootstrap seed.')
    }

    $outputDirectory = Split-Path -Parent $resolvedOutput
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
    Copy-Item -LiteralPath $publishedPath -Destination $resolvedOutput -Force
    $buildSucceeded = $true
}
finally {
    Copy-Item -LiteralPath $configBackup -Destination $configPath -Force
    if ($hadSdkConfig) {
        Copy-Item -LiteralPath $sdkConfigBackup -Destination $sdkConfigPath -Force
    }
    elseif (Test-Path -LiteralPath $sdkConfigPath -PathType Leaf) {
        Remove-Item -LiteralPath $sdkConfigPath -Force
    }
    if (Test-Path -LiteralPath $packageRoot) {
        Remove-Item -LiteralPath $packageRoot -Recurse -Force
    }
    if ($hadCompiledPackage) {
        Copy-Item -LiteralPath $compiledBackup -Destination $compiledPath -Force
    }
    elseif (Test-Path -LiteralPath $compiledPath -PathType Leaf) {
        Remove-Item -LiteralPath $compiledPath -Force
    }
    if ($hadPublishedPackage) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $publishedPath) `
            -Force | Out-Null
        Copy-Item -LiteralPath $publishedBackup -Destination $publishedPath -Force
    }
    elseif (Test-Path -LiteralPath $publishedPath -PathType Leaf) {
        Remove-Item -LiteralPath $publishedPath -Force
    }
    if ($hadShippingSeed) {
        Copy-Item -LiteralPath $shippingSeedBackup -Destination $shippingSeedPath -Force
    }
    elseif (Test-Path -LiteralPath $shippingSeedPath -PathType Leaf) {
        Remove-Item -LiteralPath $shippingSeedPath -Force
    }
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

$configHashAfter = (Get-FileHash -LiteralPath $configPath `
    -Algorithm SHA256).Hash
$sdkConfigRestored = if ($hadSdkConfig) {
    (Get-FileHash -LiteralPath $sdkConfigPath -Algorithm SHA256).Hash -eq `
        $sdkConfigHashBefore
} else {
    -not (Test-Path -LiteralPath $sdkConfigPath)
}
$publishedRestored = if ($hadPublishedPackage) {
    (Test-Path -LiteralPath $publishedPath -PathType Leaf) -and
    (Get-FileHash -LiteralPath $publishedPath -Algorithm SHA256).Hash -eq
        $publishedHashBefore
} else {
    -not (Test-Path -LiteralPath $publishedPath)
}
if ($configHashAfter -ne $configHashBefore -or -not $sdkConfigRestored -or
    -not $publishedRestored) {
    throw ('KF2 compiler INIs or the published telemetry module were not ' +
        'restored byte-for-byte after compilation.')
}
if (-not $buildSucceeded) {
    throw 'KF2 telemetry compilation did not complete.'
}

$hash = (Get-FileHash -LiteralPath $resolvedOutput -Algorithm SHA256).Hash
Write-Host "PASS: KF2 telemetry module compiled: $resolvedOutput"
Write-Host "SHA256: $hash"
Write-Host 'PASS: KF2 compiler INIs, published module and staging state restored'
