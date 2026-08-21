[CmdletBinding()]
param(
    [Parameter()]
    [string] $Destination = '',

    [Parameter()]
    [switch] $SkipBuild,

    [Parameter()]
    [string] $TelemetrySeedModule = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$defaultRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out\package'))
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $defaultRoot 'KF2OptimizerNext'
}
$destinationRoot = [IO.Path]::GetFullPath($Destination)
$allowedPrefix = $defaultRoot.TrimEnd('\') + '\'
if (-not $destinationRoot.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Package destination must be below $defaultRoot"
}
$knownManagedPaths = [Collections.Generic.HashSet[string]]::new(
    [StringComparer]::OrdinalIgnoreCase)
@(
    'KF2Optimizer.exe',
    'Data/Lab/flexRelease_x64.forwarder-lab.dll',
    'Data/Lab/KF2OptimizerTelemetry.u',
    'Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md',
    'Data/Documentation/README.md',
    'Data/Documentation/USER_GUIDE.md',
    'Data/Documentation/FEATURE_REFERENCE.md',
    'Data/Documentation/SAFETY.md',
    'Data/Documentation/SUPPORT.md',
    'Data/Documentation/LICENSE',
    'Data/Documentation/THIRD_PARTY_NOTICES.md',
    'Data/Documentation/GPL-3.0-LICENSE.txt',
    'Data/Documentation/issue72-feature-inventory.json',
    'Data/Documentation/PresentMon-LICENSE.txt',
    'Data/package-integrity.ini',
    'Data/Documentation/legacy-function-matrix.md',
    'Data/package-manifest.json'
) | ForEach-Object { [void]$knownManagedPaths.Add($_) }

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$source = Join-Path $projectRoot 'out\build\windows-x64-release\Release\KF2Optimizer.exe'
if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
    throw "Release executable is missing: $source"
}
if (-not (Test-Path -LiteralPath $destinationRoot)) {
    New-Item -ItemType Directory -Path $destinationRoot | Out-Null
}
$manifestPath = Join-Path $destinationRoot 'Data\package-manifest.json'
if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    try {
        $previousManifest = Get-Content -LiteralPath $manifestPath -Raw |
            ConvertFrom-Json -ErrorAction Stop
    }
    catch { throw "Existing package manifest is invalid; no managed file was removed: $_" }
    if ($previousManifest.schema_version -notin @(1, 2) -or
        $null -eq $previousManifest.managed_files) {
        throw 'Existing package manifest has an unsupported schema'
    }
    $destinationPrefix = $destinationRoot.TrimEnd('\') + '\'
    foreach ($relative in @($previousManifest.managed_files)) {
        if ($relative -isnot [string] -or [string]::IsNullOrWhiteSpace($relative) -or
            $relative.Contains(':') -or $relative.Contains('..') -or
            $relative.StartsWith('\') -or $relative.StartsWith('/') -or
            -not $knownManagedPaths.Contains($relative)) {
            throw "Existing package manifest contains an unsafe path: $relative"
        }
        $managed = [IO.Path]::GetFullPath((Join-Path $destinationRoot $relative))
        if (-not $managed.StartsWith($destinationPrefix,
                [StringComparison]::OrdinalIgnoreCase) -and
            -not $managed.Equals((Join-Path $destinationRoot 'KF2Optimizer.exe'),
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Existing package manifest escapes the package root: $relative"
        }
        if (Test-Path -LiteralPath $managed -PathType Leaf) {
            Remove-Item -LiteralPath $managed -Force
        }
    }
}
# Preserve the portable Data directory. It contains the user's overlay size,
# position and enabled state and must survive application updates.
Copy-Item -LiteralPath $source -Destination (Join-Path $destinationRoot 'KF2Optimizer.exe')
$labDirectory = Join-Path $destinationRoot 'Data\Lab'
New-Item -ItemType Directory -Path $labDirectory -Force | Out-Null
$forwarder = Join-Path $projectRoot 'out\build\windows-x64-release\flexRelease_x64.forwarder-lab.dll'
if (-not (Test-Path -LiteralPath $forwarder -PathType Leaf)) {
    throw "Offline FleX laboratory forwarder is missing: $forwarder"
}
Copy-Item -LiteralPath $forwarder -Destination (Join-Path $labDirectory 'flexRelease_x64.forwarder-lab.dll') -Force
$telemetryModule = Join-Path $projectRoot `
    'assets\offline_telemetry\KF2OptimizerTelemetry.u'
if (-not (Test-Path -LiteralPath $telemetryModule -PathType Leaf)) {
    Write-Host 'Telemetry module is missing; compiling it with the installed KF2 SDK.'
    $telemetryBuild = @{
        OutputPath = $telemetryModule
    }
    if (-not [string]::IsNullOrWhiteSpace($TelemetrySeedModule)) {
        $telemetryBuild.SeedModule = $TelemetrySeedModule
    }
    & (Join-Path $PSScriptRoot 'build_kf2_telemetry.ps1') @telemetryBuild
}
$expectedTelemetryHash = `
    '89F441307624239A2D169ABAB79F9F3DAEBABB6E2EA86120E429691D4035063F'
$actualTelemetryHash = (Get-FileHash -LiteralPath $telemetryModule `
    -Algorithm SHA256).Hash
if ($actualTelemetryHash -ne $expectedTelemetryHash) {
    throw 'Offline telemetry module does not match its pinned compiled build'
}
Copy-Item -LiteralPath $telemetryModule -Destination `
    (Join-Path $labDirectory 'KF2OptimizerTelemetry.u') -Force
$documentationDirectory = Join-Path $destinationRoot 'Data\Documentation'
New-Item -ItemType Directory -Path $documentationDirectory -Force | Out-Null
$packageDocumentation = [ordered]@{
    'README.md' = 'README.md'
    'docs\USER_GUIDE.md' = 'USER_GUIDE.md'
    'docs\FEATURE_REFERENCE.md' = 'FEATURE_REFERENCE.md'
    'docs\SAFETY.md' = 'SAFETY.md'
    'SUPPORT.md' = 'SUPPORT.md'
    'THIRD_PARTY_NOTICES.md' = 'THIRD_PARTY_NOTICES.md'
    'docs\ISSUE_72_PRODUCT_MATRIX.md' = 'ISSUE_72_PRODUCT_MATRIX.md'
}
foreach ($entry in $packageDocumentation.GetEnumerator()) {
    $sourcePath = Join-Path $projectRoot $entry.Key
    if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
        throw "Package documentation is missing: $($entry.Key)"
    }
    Copy-Item -LiteralPath $sourcePath -Destination `
        (Join-Path $documentationDirectory $entry.Value) -Force
}
$licenseSource = Join-Path $projectRoot 'third_party\presentmon\LICENSE.txt'
Copy-Item -LiteralPath $licenseSource -Destination `
    (Join-Path $documentationDirectory 'PresentMon-LICENSE.txt') -Force
$projectLicenseSource = Join-Path $projectRoot 'LICENSE'
Copy-Item -LiteralPath $projectLicenseSource -Destination `
    (Join-Path $documentationDirectory 'LICENSE') -Force

if (-not $SkipBuild) {
    & cmake --build (Join-Path $projectRoot 'out\build\windows-x64-release') `
        --config Release --target KF2InventoryExport
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
$inventoryExporter = Join-Path $projectRoot `
    'out\build\windows-x64-release\Release\KF2InventoryExport.exe'
if (-not (Test-Path -LiteralPath $inventoryExporter -PathType Leaf)) {
    throw 'Issue 72 inventory exporter is missing'
}
$commit = (& git -C $projectRoot rev-parse --short=12 HEAD 2>$null)
if (-not $commit) { $commit = 'unknown' }
$workingTreeChanges = @(& git -C $projectRoot status --porcelain `
    --untracked-files=normal 2>$null)
if ($workingTreeChanges.Count -ne 0) { $commit = "$commit.dirty" }
$inventoryJson = Join-Path $documentationDirectory `
    'issue72-feature-inventory.json'
& $inventoryExporter $inventoryJson "0.1.0+$commit (release)"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$integrityPayloadFiles = @(
    'KF2Optimizer.exe',
    'Data/Lab/flexRelease_x64.forwarder-lab.dll',
    'Data/Lab/KF2OptimizerTelemetry.u',
    'Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md',
    'Data/Documentation/README.md',
    'Data/Documentation/USER_GUIDE.md',
    'Data/Documentation/FEATURE_REFERENCE.md',
    'Data/Documentation/SAFETY.md',
    'Data/Documentation/SUPPORT.md',
    'Data/Documentation/LICENSE',
    'Data/Documentation/THIRD_PARTY_NOTICES.md',
    'Data/Documentation/issue72-feature-inventory.json',
    'Data/Documentation/PresentMon-LICENSE.txt')
$integrityHashes = @($integrityPayloadFiles | ForEach-Object {
    $payloadPath = Join-Path $destinationRoot $_
    if (-not (Test-Path -LiteralPath $payloadPath -PathType Leaf)) {
        throw "Package payload is missing before manifest generation: $_"
    }
    [ordered]@{
        path = $_
        sha256 = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash
    }
})
$integrityPath = Join-Path $destinationRoot 'Data\package-integrity.ini'
$integrityLines = @(
    'schema_version=1',
    'product=KF2OptimizerNext',
    "source_identity=$commit",
    "file_count=$($integrityHashes.Count)")
$integrityLines += @($integrityHashes | ForEach-Object {
    "file=$($_.path)|$($_.sha256)"
})
$integrityLines | Set-Content -LiteralPath $integrityPath -Encoding ascii

$payloadFiles = @($integrityPayloadFiles) + 'Data/package-integrity.ini'
$managedFiles = @($payloadFiles) + 'Data/package-manifest.json'
$payloadHashes = @($payloadFiles | ForEach-Object {
    $payloadPath = Join-Path $destinationRoot $_
    [ordered]@{
        path = $_
        sha256 = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash
    }
})
$packageManifest = [ordered]@{
    schema_version = 2
    product = 'KF2 Optimizer Next'
    package_version = '0.1.0'
    license = 'GPL-3.0-only'
    source_identity = $commit
    managed_files = $managedFiles
    payload_hashes = $payloadHashes
    preserved_user_data = @('settings.ini',
        'adaptive-locks.ini', 'logs',
        'backups', 'benchmarks', 'session-config', 'flex-lab',
        'offline-telemetry-lab')
}
New-Item -ItemType Directory -Path (Split-Path -Parent $manifestPath) -Force |
    Out-Null
$packageManifest | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath $manifestPath -Encoding utf8
$hash = (Get-FileHash -LiteralPath (Join-Path $destinationRoot 'KF2Optimizer.exe') -Algorithm SHA256).Hash
Write-Host "PASS: portable package created at $destinationRoot"
Write-Host "SHA256: $hash"
& (Join-Path $PSScriptRoot 'generate_release_evidence.ps1') -PackageRoot $destinationRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
