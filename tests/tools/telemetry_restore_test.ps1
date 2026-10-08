[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $BuildScript,
    [Parameter(Mandatory)][string] $TestRoot
)

$ErrorActionPreference = 'Stop'
$tokens = $null
$parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile(
    $BuildScript, [ref] $tokens, [ref] $parseErrors)
if ($parseErrors.Count) { throw $parseErrors[0] }
$transaction = $ast.Find({
    param($node)
    $node -is [Management.Automation.Language.TryStatementAst] -and
        $node.Finally -and $node.Finally.Extent.Text.Contains('$configBackup')
}, $true)
if (-not $transaction) { throw 'Telemetry cleanup transaction was not found.' }
# Run the real catch/finally, replacing only the SDK build body with a sentinel.
$cleanup = [ScriptBlock]::Create(
    'try { if ($failBuild) { throw $originalFailure } } ' +
    (($transaction.CatchClauses | ForEach-Object { $_.Extent.Text }) -join ' ') +
    ' finally ' + $transaction.Finally.Extent.Text)
$fixtureRoot = [IO.Path]::GetFullPath($TestRoot)
$fixturePrefix = $fixtureRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar

function Check-Restore {
    param([int[]] $FailedIndices, [bool] $HadOriginals = $true,
        [bool] $FailBuild = $false, [bool] $FailBackupCleanup = $false)

    $caseRoot = Join-Path $fixtureRoot ([Guid]::NewGuid().ToString('N'))
    $temporaryRoot = Join-Path $caseRoot 'backups'
    $packageRoot = Join-Path $caseRoot 'staging'
    $configPath = Join-Path $caseRoot 'engine.ini'
    $sdkConfigPath = Join-Path $caseRoot 'sdk.ini'
    $compiledPath = Join-Path $caseRoot 'compiled.u'
    $publishedPath = Join-Path $caseRoot 'published.u'
    $shippingSeedPath = Join-Path $caseRoot 'shipping.u'
    $configBackup = Join-Path $temporaryRoot 'engine.ini'
    $sdkConfigBackup = Join-Path $temporaryRoot 'sdk.ini'
    $compiledBackup = Join-Path $temporaryRoot 'compiled.u'
    $publishedBackup = Join-Path $temporaryRoot 'published.u'
    $shippingSeedBackup = Join-Path $temporaryRoot 'shipping.u'
    $targets = @($configPath, $sdkConfigPath, $packageRoot,
        $compiledPath, $publishedPath, $shippingSeedPath)
    $backups = @($configBackup, $sdkConfigBackup, '',
        $compiledBackup, $publishedBackup, $shippingSeedBackup)
    foreach ($path in @($caseRoot, $temporaryRoot) + $targets +
            @($backups | Where-Object { $_ })) {
        if (-not [IO.Path]::GetFullPath($path).StartsWith(
                $fixturePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Telemetry restore fixture escaped its test root.'
        }
    }
    $failedTargets = @($FailedIndices | ForEach-Object { $targets[$_] })
    if ($FailBackupCleanup) { $failedTargets += $temporaryRoot }
    $hadSdkConfig = $HadOriginals
    $hadCompiledPackage = $HadOriginals
    $hadPublishedPackage = $HadOriginals
    $hadShippingSeed = $HadOriginals
    $buildFailure = $null
    $originalFailure = [InvalidOperationException]::new('sentinel SDK build failure')
    function Copy-Item {
        param($LiteralPath, $Destination, [switch] $Force)
        if ($Destination -in $failedTargets) { throw "Injected restore failure: $Destination" }
        Microsoft.PowerShell.Management\Copy-Item @PSBoundParameters
    }
    function Remove-Item {
        param($LiteralPath, [switch] $Recurse, [switch] $Force)
        if ($LiteralPath -in $failedTargets) { throw "Injected restore failure: $LiteralPath" }
        Microsoft.PowerShell.Management\Remove-Item @PSBoundParameters
    }
    try {
        New-Item -ItemType Directory -Path $temporaryRoot, $packageRoot -Force | Out-Null
        foreach ($index in @(0, 1, 3, 4, 5)) {
            [IO.File]::WriteAllText($targets[$index], "changed-$index")
            if ($HadOriginals -or $index -eq 0) {
                [IO.File]::WriteAllText($backups[$index], "original-$index")
            }
        }
        $failure = $null
        try { & $cleanup }
        catch { $failure = $_.Exception }
        foreach ($index in 0..5) {
            $exists = Test-Path -LiteralPath $targets[$index]
            $shouldExist = $index -in $FailedIndices -or
                ($index -ne 2 -and ($HadOriginals -or $index -eq 0))
            if ($exists -ne $shouldExist) {
                throw "Independent restore left the wrong existence state: $index"
            }
            if ($exists -and $index -ne 2) {
                $expected = if ($index -in $FailedIndices) { "changed-$index" }
                    else { "original-$index" }
                if ([IO.File]::ReadAllText($targets[$index]) -cne $expected) {
                    throw "Independent restore did not restore exact bytes: $index"
                }
            }
        }
        $expectedCleanupFailures = $FailedIndices.Count + [int] $FailBackupCleanup
        if ($expectedCleanupFailures) {
            if ($failure -isnot [AggregateException] -or
                $failure.InnerExceptions.Count -ne
                    ($expectedCleanupFailures + [int] $FailBuild) -or
                -not $failure.Message.Contains($temporaryRoot) -or
                -not (Test-Path -LiteralPath $temporaryRoot)) {
                throw 'Incomplete cleanup did not report all failures and retain backups.'
            }
            foreach ($index in @(0, 1, 3, 4, 5)) {
                if (($HadOriginals -or $index -eq 0) -and
                    [IO.File]::ReadAllText($backups[$index]) -cne "original-$index") {
                    throw 'A recovery backup was changed.'
                }
            }
        }
        elseif ((Test-Path -LiteralPath $temporaryRoot) -or
            (([bool] $failure) -ne $FailBuild)) {
            throw 'Successful restoration did not finish cleanly.'
        }
        if ($FailBuild -and -not $failure.ToString().Contains($originalFailure.Message)) {
            throw 'The original build failure was lost.'
        }
    }
    finally {
        Microsoft.PowerShell.Management\Remove-Item -LiteralPath $caseRoot -Recurse -Force
    }
}

foreach ($originals in @($true, $false)) {
    Check-Restore -FailedIndices @() -HadOriginals $originals
    foreach ($index in 0..5) {
        Check-Restore -FailedIndices @($index) -HadOriginals $originals
    }
}
Check-Restore -FailedIndices @() -FailBuild $true
Check-Restore -FailedIndices @(0, 4) -FailBuild $true
Check-Restore -FailedIndices @() -FailBackupCleanup $true
Check-Restore -FailedIndices @() -FailBackupCleanup $true -FailBuild $true
Write-Host 'PASS: independent telemetry cleanup, backup retention and original errors'
