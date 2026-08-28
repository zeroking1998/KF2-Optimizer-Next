[CmdletBinding()]
param(
    [Parameter()]
    [string] $Destination = ''
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $projectRoot 'out\bootstrap\KF2OptimizerTelemetry.u'
}
$resolvedDestination = [IO.Path]::GetFullPath($Destination)
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out')).TrimEnd('\') + '\'
if (-not $resolvedDestination.StartsWith(
        $allowedRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Telemetry seed destination must be below $allowedRoot"
}

$repository = 'zeroking1998/KF2-Optimizer-Next'
$apiUri = "https://api.github.com/repos/$repository/releases?per_page=20"
$headers = @{
    Accept = 'application/vnd.github+json'
    'User-Agent' = 'KF2OptimizerNext-source-build'
    'X-GitHub-Api-Version' = '2022-11-28'
}

Write-Host 'Finding the newest published KF2 Optimizer release...'
$releases = Invoke-RestMethod -Uri $apiUri -Headers $headers
$release = $releases |
    Where-Object { -not $_.draft -and $null -ne $_.published_at } |
    Sort-Object { [DateTimeOffset]$_.published_at } -Descending |
    Select-Object -First 1
if ($null -eq $release -or $release.tag_name -notmatch '^v[0-9A-Za-z.-]+$') {
    throw 'No valid published KF2 Optimizer release was found.'
}

$expectedAssetName = "KF2OptimizerNext-$($release.tag_name)-win64.zip"
$assets = @($release.assets | Where-Object { $_.name -eq $expectedAssetName })
if ($assets.Count -ne 1) {
    throw "Release $($release.tag_name) does not contain exactly one $expectedAssetName asset."
}
$asset = $assets[0]
if ($asset.browser_download_url -notlike
    "https://github.com/$repository/releases/download/$($release.tag_name)/*") {
    throw 'The release asset URL does not belong to the official repository and tag.'
}
if ($asset.size -lt 1KB -or $asset.size -gt 100MB) {
    throw "The release asset has an invalid size: $($asset.size) bytes."
}
if ([string]$asset.digest -notmatch '^sha256:([0-9A-Fa-f]{64})$') {
    throw 'The release asset does not provide a valid GitHub SHA-256 digest.'
}
$expectedHash = $Matches[1].ToLowerInvariant()

$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'KF2OptimizerSeed-' + [Guid]::NewGuid().ToString('N'))
$zipPath = Join-Path $temporaryRoot $expectedAssetName
$extractRoot = Join-Path $temporaryRoot 'extracted'
try {
    New-Item -ItemType Directory -Path $extractRoot -Force | Out-Null
    Write-Host "Downloading $expectedAssetName..."
    Invoke-WebRequest -Uri $asset.browser_download_url -Headers $headers -OutFile $zipPath

    $downloadedSize = (Get-Item -LiteralPath $zipPath).Length
    if ($downloadedSize -ne [long]$asset.size) {
        throw "Release asset size mismatch: expected $($asset.size), received $downloadedSize."
    }
    $actualHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "Release asset SHA-256 mismatch: expected $expectedHash, received $actualHash."
    }

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        if ($archive.Entries.Count -gt 5000) {
            throw 'The verified release ZIP contains too many entries.'
        }
        $expandedBytes = [long]0
        $extractPrefix = [IO.Path]::GetFullPath($extractRoot).TrimEnd('\') + '\'
        foreach ($entry in $archive.Entries) {
            $expandedBytes += [long]$entry.Length
            if ($expandedBytes -gt 200MB) {
                throw 'The verified release ZIP expands beyond the allowed size.'
            }
            $entryPath = [IO.Path]::GetFullPath(
                (Join-Path $extractRoot $entry.FullName))
            if (-not $entryPath.StartsWith(
                    $extractPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'The verified release ZIP contains an unsafe path.'
            }
        }
    }
    finally {
        $archive.Dispose()
    }

    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractRoot
    $seedFiles = @(Get-ChildItem -LiteralPath $extractRoot -Recurse -File |
        Where-Object {
            $_.FullName.Replace('/', '\') -like
                '*\Data\Lab\KF2OptimizerTelemetry.u'
        })
    if ($seedFiles.Count -ne 1) {
        throw 'The verified release does not contain exactly one telemetry seed.'
    }

    $destinationParent = Split-Path -Parent $resolvedDestination
    New-Item -ItemType Directory -Path $destinationParent -Force | Out-Null
    Copy-Item -LiteralPath $seedFiles[0].FullName -Destination $resolvedDestination -Force
    Write-Host "Verified telemetry seed: $resolvedDestination" -ForegroundColor Green
    Write-Output $resolvedDestination
}
finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}
