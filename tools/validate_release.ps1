[CmdletBinding()]
param(
    [Parameter()]
    [string] $PackageRoot = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    $PackageRoot = Join-Path $projectRoot 'out\package\KF2OptimizerNext'
}
$root = [IO.Path]::GetFullPath($PackageRoot)
if (-not (Test-Path -LiteralPath $root -PathType Container)) {
    throw "Package root does not exist: $root"
}
$files = @(Get-ChildItem -LiteralPath $root -File -Recurse)
$executables = @($files | Where-Object Extension -ieq '.exe')
if ($executables.Count -ne 1 -or $executables[0].Name -cne 'KF2Optimizer.exe' -or
    $executables[0].DirectoryName -ine $root) {
    throw 'Portable package must contain exactly one root KF2Optimizer.exe'
}
$unexpected = @($files | Where-Object {
    $_.FullName -ne $executables[0].FullName -and
    -not $_.FullName.StartsWith((Join-Path $root 'Data') + '\',
        [StringComparison]::OrdinalIgnoreCase)
})
if ($unexpected.Count -ne 0) {
    throw 'Portable runtime files are only allowed below the Data directory'
}
$bytes = [IO.File]::ReadAllBytes($executables[0].FullName)
if ($bytes.Length -lt 512 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
    throw 'Package executable is not a valid PE image'
}
$forwarder = Join-Path $root 'Data\Lab\flexRelease_x64.forwarder-lab.dll'
if (-not (Test-Path -LiteralPath $forwarder -PathType Leaf)) {
    throw 'Portable offline FleX laboratory forwarder is missing'
}
$forwarderBytes = [IO.File]::ReadAllBytes($forwarder)
if ($forwarderBytes.Length -lt 512 -or $forwarderBytes[0] -ne 0x4D -or $forwarderBytes[1] -ne 0x5A) {
    throw 'Offline FleX laboratory forwarder is not a valid PE image'
}
$inventory = Join-Path $root 'Data\Documentation\ISSUE_72_PRODUCT_MATRIX.md'
if (-not (Test-Path -LiteralPath $inventory -PathType Leaf)) {
    throw 'Issue 72 product inventory documentation is missing from the portable package'
}
$publicDocumentation = @(
    'README.md',
    'USER_GUIDE.md',
    'FEATURE_REFERENCE.md',
    'SAFETY.md',
    'SUPPORT.md',
    'THIRD_PARTY_NOTICES.md'
)
foreach ($document in $publicDocumentation) {
    $documentPath = Join-Path $root "Data\Documentation\$document"
    if (-not (Test-Path -LiteralPath $documentPath -PathType Leaf)) {
        throw "Public package documentation is missing: $document"
    }
}
$inventoryJson = Join-Path $root 'Data\Documentation\issue72-feature-inventory.json'
if (-not (Test-Path -LiteralPath $inventoryJson -PathType Leaf)) {
    throw 'Machine-readable Issue 72 inventory is missing from the portable package'
}
$inventoryDocument = Get-Content -LiteralPath $inventoryJson -Raw |
    ConvertFrom-Json -ErrorAction Stop
if ($inventoryDocument.schema -cne 'KF2_ISSUE72_INVENTORY_V3' -or
    $inventoryDocument.function_count -ne 149 -or
    @($inventoryDocument.records).Count -ne 149) {
    throw 'Machine-readable Issue 72 inventory is incomplete or incompatible'
}
$license = Join-Path $root 'Data\Documentation\PresentMon-LICENSE.txt'
if (-not (Test-Path -LiteralPath $license -PathType Leaf)) {
    throw 'PresentMon license is missing from the portable package'
}
$projectLicense = Join-Path $root 'Data\Documentation\LICENSE'
if (-not (Test-Path -LiteralPath $projectLicense -PathType Leaf)) {
    throw 'GPL-3.0 project license is missing from the portable package'
}
$projectLicenseText = Get-Content -LiteralPath $projectLicense -Raw
if (-not $projectLicenseText.Contains('GNU GENERAL PUBLIC LICENSE') -or
    -not $projectLicenseText.Contains('Version 3, 29 June 2007') -or
    -not $projectLicenseText.Contains('END OF TERMS AND CONDITIONS')) {
    throw 'Packaged GPL-3.0 project license is incomplete'
}
$packageManifestPath = Join-Path $root 'Data\package-manifest.json'
if (-not (Test-Path -LiteralPath $packageManifestPath -PathType Leaf)) {
    throw 'Portable package manifest is missing'
}
$packageManifest = Get-Content -LiteralPath $packageManifestPath -Raw |
    ConvertFrom-Json -ErrorAction Stop
if ($packageManifest.schema_version -ne 2 -or
    $packageManifest.license -cne 'GPL-3.0-only' -or
    @($packageManifest.managed_files).Count -ne 15) {
    throw 'Portable package manifest is incomplete or incompatible'
}
foreach ($relative in @($packageManifest.managed_files)) {
    $managed = Join-Path $root $relative
    if (-not (Test-Path -LiteralPath $managed -PathType Leaf)) {
        throw "Managed package file is missing: $relative"
    }
}
$payloadHashes = @($packageManifest.payload_hashes)
if ($payloadHashes.Count -ne 14) {
    throw 'Portable package hash manifest must cover all fourteen payload files'
}
$seenPayload = @{}
foreach ($entry in $payloadHashes) {
    $relative = [string]$entry.path
    $expectedHash = [string]$entry.sha256
    if ([string]::IsNullOrWhiteSpace($relative) -or
        $relative -eq 'Data/package-manifest.json' -or
        $relative.Contains(':') -or $relative.Contains('..') -or
        $relative.StartsWith('\') -or $relative.StartsWith('/') -or
        $expectedHash -notmatch '^[A-Fa-f0-9]{64}$' -or
        $seenPayload.ContainsKey($relative)) {
        throw "Portable package hash entry is unsafe or invalid: $relative"
    }
    $seenPayload[$relative] = $true
    $payloadPath = [IO.Path]::GetFullPath((Join-Path $root $relative))
    if (-not $payloadPath.StartsWith($root.TrimEnd('\') + '\',
            [StringComparison]::OrdinalIgnoreCase) -and
        -not $payloadPath.Equals($executables[0].FullName,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Portable package hash path escapes the package: $relative"
    }
    if (-not (Test-Path -LiteralPath $payloadPath -PathType Leaf)) {
        throw "Hashed package payload is missing: $relative"
    }
    $actualHash = (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash
    if ($actualHash -ine $expectedHash) {
        throw "Package payload hash mismatch: $relative"
    }
}
$integrityPath = Join-Path $root 'Data\package-integrity.ini'
$integrityLines = @(Get-Content -LiteralPath $integrityPath)
if ($integrityLines.Count -ne 17 -or
    $integrityLines[0] -cne 'schema_version=1' -or
    $integrityLines[1] -cne 'product=KF2OptimizerNext' -or
    $integrityLines[2] -notmatch '^source_identity=[A-Za-z0-9._-]{1,128}$' -or
    $integrityLines[3] -cne 'file_count=13' -or
    @($integrityLines | Where-Object { $_ -match '^file=' }).Count -ne 13) {
    throw 'Runtime package integrity document is invalid'
}
Write-Host "PASS: package has exactly one portable executable"
Write-Host "PASS: all fourteen managed payload hashes match"
Write-Host "SHA256: $((Get-FileHash -LiteralPath $executables[0].FullName -Algorithm SHA256).Hash)"
& (Join-Path $PSScriptRoot 'validate_acceptance_ledger.ps1')
