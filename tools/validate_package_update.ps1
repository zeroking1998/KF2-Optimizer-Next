[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$packageRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot `
    'out\package\package-update-validation'))
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out\package')).TrimEnd('\') + '\'
if (-not $packageRoot.StartsWith($allowedRoot,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Package update validation root escaped out\package'
}
if (Test-Path -LiteralPath $packageRoot) {
    Remove-Item -LiteralPath $packageRoot -Recurse -Force
}

& (Join-Path $PSScriptRoot 'package.ps1') -Destination $packageRoot -SkipBuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$userFile = Join-Path $packageRoot 'Data\settings.ini'
$userBytes = [Text.Encoding]::UTF8.GetBytes("user-owned-setting=true`n")
[IO.File]::WriteAllBytes($userFile, $userBytes)
$obsolete = Join-Path $packageRoot 'Data\Documentation\legacy-function-matrix.md'
[IO.File]::WriteAllText($obsolete, 'old package payload', [Text.Encoding]::UTF8)
$manifestPath = Join-Path $packageRoot 'Data\package-manifest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$manifest.managed_files = @($manifest.managed_files) + `
    'Data/Documentation/legacy-function-matrix.md'
$manifest | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $manifestPath -Encoding utf8

& (Join-Path $PSScriptRoot 'package.ps1') -Destination $packageRoot -SkipBuild
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
if (Test-Path -LiteralPath $obsolete) {
    throw 'Package update did not remove an obsolete package-managed file'
}
if (-not (Test-Path -LiteralPath $userFile -PathType Leaf) -or
    [Convert]::ToBase64String([IO.File]::ReadAllBytes($userFile)) -cne
        [Convert]::ToBase64String($userBytes)) {
    throw 'Package update changed or removed portable user data'
}

& (Join-Path $PSScriptRoot 'validate_release.ps1') -PackageRoot $packageRoot
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
Write-Host 'PASS: portable update removes only managed files and preserves user Data byte-for-byte'
