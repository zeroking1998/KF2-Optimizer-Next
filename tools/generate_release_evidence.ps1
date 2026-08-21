[CmdletBinding()]
param([string] $PackageRoot = '')

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if ([string]::IsNullOrWhiteSpace($PackageRoot)) {
    $PackageRoot = Join-Path $projectRoot 'out\package\KF2OptimizerNext'
}
$root = [IO.Path]::GetFullPath($PackageRoot)
$allowed = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out\package')).TrimEnd('\') + '\'
if (-not $root.StartsWith($allowed, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Evidence generation is restricted to out\package'
}
$exe = Join-Path $root 'KF2Optimizer.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw 'Package executable missing' }
$evidence = Join-Path $projectRoot 'out\release-evidence'
New-Item -ItemType Directory -Path $evidence -Force | Out-Null
$hash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
$forwarder = Join-Path $root 'Data\Lab\flexRelease_x64.forwarder-lab.dll'
if (-not (Test-Path -LiteralPath $forwarder -PathType Leaf)) { throw 'FleX laboratory forwarder missing' }
$forwarderHash = (Get-FileHash -LiteralPath $forwarder -Algorithm SHA256).Hash
$telemetryModule = Join-Path $root 'Data\Lab\KF2OptimizerTelemetry.u'
if (-not (Test-Path -LiteralPath $telemetryModule -PathType Leaf)) { throw 'Offline telemetry module missing' }
$telemetryModuleHash = (Get-FileHash -LiteralPath $telemetryModule -Algorithm SHA256).Hash
$inventory = Join-Path $root 'Data\Documentation\ISSUE_72_PRODUCT_MATRIX.md'
if (-not (Test-Path -LiteralPath $inventory -PathType Leaf)) { throw 'Issue 72 product inventory missing' }
$inventoryHash = (Get-FileHash -LiteralPath $inventory -Algorithm SHA256).Hash
$inventoryJson = Join-Path $root 'Data\Documentation\issue72-feature-inventory.json'
if (-not (Test-Path -LiteralPath $inventoryJson -PathType Leaf)) { throw 'Machine-readable Issue 72 inventory missing' }
$inventoryDocument = Get-Content -LiteralPath $inventoryJson -Raw |
    ConvertFrom-Json -ErrorAction Stop
if ($inventoryDocument.schema -cne 'KF2_ISSUE72_INVENTORY_V3' -or
    $inventoryDocument.function_count -ne 149 -or
    @($inventoryDocument.records).Count -ne 149) {
    throw 'Machine-readable Issue 72 inventory failed validation'
}
$inventoryJsonHash = (Get-FileHash -LiteralPath $inventoryJson -Algorithm SHA256).Hash
$integrity = Join-Path $root 'Data\package-integrity.ini'
if (-not (Test-Path -LiteralPath $integrity -PathType Leaf)) { throw 'Runtime package integrity document missing' }
$integrityHash = (Get-FileHash -LiteralPath $integrity -Algorithm SHA256).Hash
$commit = (& git -C $projectRoot rev-parse --short=12 HEAD 2>$null)
if (-not $commit) { $commit = 'unknown' }
$workingTreeChanges = @(& git -C $projectRoot status --porcelain --untracked-files=normal 2>$null)
$workingTreeClean = $workingTreeChanges.Count -eq 0
$metadata = [ordered]@{
    schema_version = 1
    product = 'KF2 Optimizer Next'
    license = 'GPL-3.0-only'
    architecture = 'windows-x64-native'
    commit = $commit
    working_tree_clean = $workingTreeClean
    source_identity = if ($workingTreeClean) { $commit } else { "$commit.dirty" }
    package_sha256 = $hash
    generated_utc = [DateTime]::UtcNow.ToString('o')
    runtime_files = @(
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
        'Data/Documentation/PresentMon-LICENSE.txt',
        'Data/package-integrity.ini',
        'Data/package-manifest.json')
    issue72_inventory = [ordered]@{
        schema = 'KF2_ISSUE72_INVENTORY_V3'
        function_count = 149
        documentation_sha256 = $inventoryHash
        machine_readable_sha256 = $inventoryJsonHash
    }
    flex_hook = [ordered]@{
        mode = 'offline-laboratory-fail-closed'
        installed_automatically = $false
        abi_audit = '52 exports plus SHA-256'
        forwarder_sha256 = $forwarderHash
    }
    offline_gameplay_telemetry = [ordered]@{
        mode = 'offline-read-only-session'
        installed_automatically = $true
        package_sha256 = $telemetryModuleHash
    }
}
$metadata | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $evidence 'RELEASE_METADATA.json') -Encoding utf8
@("$hash  KF2Optimizer.exe", "$forwarderHash  Data/Lab/flexRelease_x64.forwarder-lab.dll", `
  "$telemetryModuleHash  Data/Lab/KF2OptimizerTelemetry.u", `
  "$inventoryHash  Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md", `
  "$inventoryJsonHash  Data/Documentation/issue72-feature-inventory.json", `
  "$integrityHash  Data/package-integrity.ini") |
    Set-Content -LiteralPath (Join-Path $evidence 'SHA256SUMS.txt') -Encoding ascii
$sbom = [ordered]@{
    bomFormat = 'CycloneDX'; specVersion = '1.5'; version = 1
    metadata = [ordered]@{ component = [ordered]@{
        type='application'; name='KF2 Optimizer Next'; version='0.1.0'
        licenses=@([ordered]@{ license=[ordered]@{ id='GPL-3.0-only' } })
    } }
    components = @(
        [ordered]@{ type='library'; name='PresentMon PresentData'; supplier=[ordered]@{name='Intel'}; scope='required' },
        [ordered]@{ type='library'; name='KF2 FleX offline laboratory forwarder'; scope='optional'; hashes=@([ordered]@{alg='SHA-256'; content=$forwarderHash}) }
        [ordered]@{ type='file'; name='KF2 read-only offline telemetry package'; scope='optional'; hashes=@([ordered]@{alg='SHA-256'; content=$telemetryModuleHash}) }
        [ordered]@{ type='file'; name='Issue 72 function inventory'; scope='required'; hashes=@([ordered]@{alg='SHA-256'; content=$inventoryJsonHash}) }
    )
}
$sbom | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $evidence 'SBOM.cdx.json') -Encoding utf8
Write-Host "PASS: release evidence generated at $evidence"
