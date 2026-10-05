[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $FingerprintScript,

    [Parameter(Mandatory)]
    [string] $SourceRoot
)

$ErrorActionPreference = 'Stop'
$sourceNames = @(Get-ChildItem -LiteralPath $SourceRoot -File -Filter '*.uc' |
    Sort-Object Name | ForEach-Object Name)
if ($sourceNames.Count -eq 0) { throw 'No authored telemetry sources were found.' }
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) `
    ('KF2TelemetryFingerprintTest-' + [Guid]::NewGuid().ToString('N'))

try {
    New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
    foreach ($sourceName in $sourceNames) {
        Copy-Item -LiteralPath (Join-Path $SourceRoot $sourceName) `
            -Destination (Join-Path $temporaryRoot $sourceName)
    }

    $expected = (& $FingerprintScript -SourceRoot $SourceRoot).Trim()
    $copied = (& $FingerprintScript -SourceRoot $temporaryRoot).Trim()
    if ($expected -notmatch '^[0-9a-f]{64}$' -or $copied -ne $expected) {
        throw 'Equivalent telemetry sources did not produce the same fingerprint.'
    }

    foreach ($sourceName in $sourceNames) {
        $sourcePath = Join-Path $temporaryRoot $sourceName
        Add-Content -LiteralPath $sourcePath -Value "`n// fingerprint regression mutation"
        $changed = (& $FingerprintScript -SourceRoot $temporaryRoot).Trim()
        if ($changed -eq $expected) {
            throw "Source change did not invalidate the fingerprint: $sourceName"
        }
        Copy-Item -LiteralPath (Join-Path $SourceRoot $sourceName) `
            -Destination $sourcePath -Force
        Remove-Item -LiteralPath $sourcePath
        $missingWasRejected = $false
        try { & $FingerprintScript -SourceRoot $temporaryRoot | Out-Null }
        catch {
            if (-not $_.Exception.Message.Contains('Required KF2 telemetry source is missing')) {
                throw
            }
            $missingWasRejected = $true
        }
        if (-not $missingWasRejected) {
            throw "Missing source was not rejected: $sourceName"
        }
        Copy-Item -LiteralPath (Join-Path $SourceRoot $sourceName) `
            -Destination $sourcePath -Force
        if ((& $FingerprintScript -SourceRoot $temporaryRoot).Trim() -cne $expected) {
            throw "Restored source did not restore the exact fingerprint: $sourceName"
        }
    }
}
finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        $temporaryPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if (-not [IO.Path]::GetFullPath($temporaryRoot).StartsWith(
                $temporaryPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Fingerprint fixture escaped its temporary root.'
        }
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}

Write-Host 'PASS: telemetry source fingerprint is stable and change-sensitive'
