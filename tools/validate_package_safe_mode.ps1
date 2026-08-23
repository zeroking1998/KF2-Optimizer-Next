[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$packageBase = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out\package'))
$sourcePackage = Join-Path $packageBase 'KF2OptimizerNext'
$testRoot = Join-Path $packageBase 'package-component-validation'
$looseRoot = Join-Path $packageBase 'loose-release-validation'

function Clear-ValidationRoot([string] $Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if (-not $full.StartsWith($packageBase.TrimEnd('\') + '\',
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Unsafe package test root: $full"
    }
    if (Test-Path -LiteralPath $full) {
        Remove-Item -LiteralPath $full -Recurse -Force
    }
}

function Wait-ForMainWindow([Diagnostics.Process] $Process) {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Package-integrity candidate exited early with code $($Process.ExitCode)"
        }
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            return $Process.MainWindowHandle
        }
        Start-Sleep -Milliseconds 100
    }
    throw 'Package-integrity candidate did not create a visible main window'
}

function Stop-Candidate([Diagnostics.Process] $Process) {
    if (-not $Process.HasExited) {
        [void]$Process.CloseMainWindow()
        if (-not $Process.WaitForExit(15000)) {
            $Process.Kill()
            throw 'Package-integrity candidate did not exit after closing its window'
        }
    }
    if ($Process.ExitCode -ne 0) {
        throw "Package-integrity candidate exit code was $($Process.ExitCode)"
    }
}

function Assert-ComponentEvidence([string] $Root) {
    $eventPath = Join-Path $Root 'Data\logs\session-events.json'
    if (-not (Test-Path -LiteralPath $eventPath -PathType Leaf)) {
        throw 'Package-integrity candidate did not create its local event log'
    }
    $events = Get-Content -LiteralPath $eventPath -Raw
    if ($events -notmatch 'PACKAGE_INTEGRITY_FAILED') {
        throw 'Package integrity failure was not recorded'
    }
    $settingsPath = Join-Path $Root 'Data\settings.ini'
    if (-not (Test-Path -LiteralPath $settingsPath -PathType Leaf)) {
        throw 'Package-integrity candidate did not initialize portable settings'
    }
}

if (@(Get-Process KF2Optimizer -ErrorAction SilentlyContinue).Count -ne 0) {
    throw 'Close existing KF2Optimizer instances before package component validation'
}
if (-not (Test-Path -LiteralPath $sourcePackage -PathType Container)) {
    throw 'Build and validate the portable package before component validation'
}

Clear-ValidationRoot $testRoot
Clear-ValidationRoot $looseRoot
Copy-Item -LiteralPath $sourcePackage -Destination $testRoot -Recurse
$damagedFile = Join-Path $testRoot 'Data\Documentation\PresentMon-LICENSE.txt'
[IO.File]::AppendAllText($damagedFile, "`nintentional validation damage")
$damagedProcess = Start-Process -FilePath (Join-Path $testRoot 'KF2Optimizer.exe') `
    -PassThru
try {
    [void](Wait-ForMainWindow $damagedProcess)
    Assert-ComponentEvidence $testRoot
}
finally {
    Stop-Candidate $damagedProcess
}

New-Item -ItemType Directory -Path $looseRoot | Out-Null
Copy-Item -LiteralPath (Join-Path $sourcePackage 'KF2Optimizer.exe') `
    -Destination (Join-Path $looseRoot 'KF2Optimizer.exe')
$looseProcess = Start-Process -FilePath (Join-Path $looseRoot 'KF2Optimizer.exe') `
    -PassThru
try {
    [void](Wait-ForMainWindow $looseProcess)
    Assert-ComponentEvidence $looseRoot
}
finally {
    Stop-Candidate $looseProcess
}

Write-Host 'PASS: damaged package and loose Release EXE start visibly, record exact package-integrity evidence and remain repairable'
