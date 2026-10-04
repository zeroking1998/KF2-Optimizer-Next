[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $Executable,
    [Parameter(Mandatory)][string] $TestRoot,
    [Parameter(Mandatory)][string] $ExpectedBuildIdentity
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$reader = Join-Path $projectRoot 'tools/get_executable_build_identity.ps1'
$validator = Join-Path $projectRoot 'tools/validate_release.ps1'
$identity = & $reader -Executable $Executable
$actual = "$($identity.version)+$($identity.source_identity) ($($identity.channel))"
if ($actual -cne $ExpectedBuildIdentity) {
    throw "Embedded executable identity differs from the compiled app: $actual"
}
$current = (& git -C $projectRoot rev-parse --short=12 HEAD).Trim()
$stale = if ($current -ceq '000000000000') { 'ffffffffffff' } else { '000000000000' }
$dirty = $identity.source_identity.EndsWith('.dirty')
$development = $dirty -or $identity.channel -ne 'release'
$testPrefix = [IO.Path]::GetFullPath($TestRoot).TrimEnd('\') + '\'
$root = Join-Path $testPrefix ('release-identity-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root -Force | Out-Null
$manifestPath = Join-Path $root 'Data/package-manifest.json'
$integrityPath = Join-Path $root 'Data/package-integrity.ini'
$payload = @('KF2Optimizer.exe', 'Data/Lab/flexRelease_x64.forwarder-lab.dll',
    'Data/Lab/KF2OptimizerTelemetry.u', 'Data/Documentation/ISSUE_72_PRODUCT_MATRIX.md',
    'Data/Documentation/README.md', 'Data/Documentation/USER_GUIDE.md',
    'Data/Documentation/UPDATES.md', 'Data/Documentation/FEATURE_REFERENCE.md',
    'Data/Documentation/SAFETY.md', 'Data/Documentation/SUPPORT.md',
    'Data/Documentation/LICENSE', 'Data/Documentation/THIRD_PARTY_NOTICES.md',
    'Data/Documentation/issue72-feature-inventory.json')

function Write-Manifest([string] $Source, [string] $IntegritySource = $Source) {
    $lines = @('schema_version=1', 'product=KF2OptimizerNext',
        "source_identity=$IntegritySource", 'file_count=13')
    $lines += @($payload | ForEach-Object {
        "file=$_|$((Get-FileHash -LiteralPath (Join-Path $root $_)).Hash)"
    })
    $lines | Set-Content -LiteralPath $integrityPath -Encoding ascii
    [ordered]@{
        schema_version = 2; package_version = $identity.version
        license = 'GPL-3.0-only'; source_identity = $Source
        managed_files = @($payload) + 'Data/package-integrity.ini' + 'Data/package-manifest.json'
        payload_hashes = @(@($payload) + 'Data/package-integrity.ini' | ForEach-Object {
            @{ path = $_; sha256 = (Get-FileHash -LiteralPath (Join-Path $root $_)).Hash }
        })
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
}

function Check-Validation([string] $ExpectedError, [string[]] $Arguments = @()) {
    $output = (& (Get-Process -Id $PID).Path -NoProfile -File $validator `
        -PackageRoot $root @Arguments 2>&1 | Out-String)
    $code = $LASTEXITCODE
    if ($ExpectedError) {
        if ($code -eq 0 -or -not $output.Contains($ExpectedError)) {
            throw "Expected '$ExpectedError', exit=$code output=$output"
        }
    } elseif ($code -ne 0) {
        throw "Matching package was rejected: $output"
    }
}

try {
    foreach ($relative in $payload) {
        $path = Join-Path $root $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force | Out-Null
        Set-Content -LiteralPath $path -Value 'fixture' -Encoding ascii
    }
    Copy-Item -LiteralPath $Executable -Destination (Join-Path $root 'KF2Optimizer.exe') -Force
    # Only the forwarder's PE prefix is relevant to the release script fixture.
    Copy-Item -LiteralPath $Executable -Destination `
        (Join-Path $root 'Data/Lab/flexRelease_x64.forwarder-lab.dll') -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'LICENSE') -Destination `
        (Join-Path $root 'Data/Documentation/LICENSE') -Force
    @{ schema = 'KF2_ISSUE72_INVENTORY_V3'; function_count = 149; records = @(1..149) } |
        ConvertTo-Json | Set-Content -LiteralPath `
            (Join-Path $root 'Data/Documentation/issue72-feature-inventory.json') -Encoding utf8
    $matchingArguments = if ($development) { @('-DevelopmentPackage') } else { @() }
    Write-Manifest $identity.source_identity
    if ($identity.source_identity -notin @($current, "$current.dirty")) {
        # Direct CMake developer builds may intentionally use "unknown".
        Check-Validation 'Stale package source identity'
        Write-Host 'PASS: compiled identity and rejection of an unbound developer build'
        return
    }
    Check-Validation '' $matchingArguments
    if (-not $development) { Check-Validation '' @('-ExpectedRevision', $current) }
    if ($dirty) { Check-Validation 'Release candidates must not have a dirty' }

    Write-Manifest $stale
    Check-Validation 'Stale package source identity'
    Write-Manifest "$current.dirty"
    Check-Validation 'Release candidates must not have a dirty'
    Write-Manifest $identity.source_identity $stale
    Check-Validation 'integrity source identity does not match' $matchingArguments
    # Change only the fixture's version resource, keeping its metadata/hashes valid.
    # No parent Git revision is needed, so shallow CI checkouts are supported.
    $executablePath = Join-Path $root 'KF2Optimizer.exe'
    $bytes = [IO.File]::ReadAllBytes($executablePath)
    $imageText = [Text.Encoding]::Unicode.GetString($bytes)
    $offset = $imageText.IndexOf($actual, [StringComparison]::Ordinal)
    if ($offset -lt 0) { throw 'Fixture version resource was not found' }
    $wrongSource = $stale + $identity.source_identity.Substring(12)
    $replacement = [Text.Encoding]::Unicode.GetBytes(
        "$($identity.version)+$wrongSource ($($identity.channel))")
    [Array]::Copy($replacement, 0, $bytes, $offset * 2, $replacement.Length)
    [IO.File]::WriteAllBytes($executablePath, $bytes)
    Write-Manifest $identity.source_identity
    Check-Validation 'Executable build identity does not match' $matchingArguments
    Copy-Item -LiteralPath $Executable -Destination $executablePath -Force
    Write-Manifest $identity.source_identity
    # Metadata cannot make a PE without embedded build identity pass.
    $missingIdentity = [byte[]]::new(512)
    $missingIdentity[0] = 0x4d; $missingIdentity[1] = 0x5a
    [IO.File]::WriteAllBytes((Join-Path $root 'KF2Optimizer.exe'), $missingIdentity)
    Write-Manifest $identity.source_identity
    Check-Validation 'Executable build identity is missing or invalid' $matchingArguments
    Write-Host 'PASS: compiled identity, matching package, stale/dirty/mixed/missing identity cases'
} finally {
    $resolved = [IO.Path]::GetFullPath($root)
    if (-not $resolved.StartsWith($testPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Release identity fixture escaped its test sandbox'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
