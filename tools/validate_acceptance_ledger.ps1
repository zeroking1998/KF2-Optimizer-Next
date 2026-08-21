$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$ledger = Join-Path $root 'docs\FINAL_ACCEPTANCE.md'
if (-not (Test-Path -LiteralPath $ledger -PathType Leaf)) {
    throw 'Acceptance ledger missing'
}
$text = Get-Content -LiteralPath $ledger -Raw
$required = @(
    'Debug, Release and Ninja regression',
    'Two clean deterministic builds',
    'Portable package shape',
    'Fresh GUI start/exit',
    'Installed FleX runtime',
    'Offline FleX laboratory',
    'Windows DPI 100-200%',
    'Multi-monitor and mixed DPI',
    'No Windows system sounds',
    'Final gameplay acceptance',
    'Single final gameplay script'
)
foreach ($name in $required) {
    if (-not $text.Contains($name)) { throw "Acceptance gate missing: $name" }
}
if ($text -match '(43/43|50/50|58/58)') { throw 'Acceptance ledger contains a stale fixed test count' }
if ($text -notmatch 'Debug, Release and Ninja regression.*(PASS|PENDING CURRENT BUILD)') {
    throw 'Current Debug/Release/Ninja regression state is not explicit'
}
if ($text -notmatch 'Offline FleX laboratory.*(PASS \(AUTOMATED/LIVE/RESTORE\)|PENDING FINAL GAMEPLAY)') {
    throw 'Offline FleX validation state is not explicit'
}
if ($text -notmatch 'Final gameplay acceptance.*PENDING') {
    throw 'Final gameplay must remain pending until the final package is played'
}
if ($text -match 'Single final gameplay script \(completed\)') {
    throw 'A historical gameplay run is incorrectly marked as current'
}
Write-Host 'PASS: acceptance ledger separates current proof, pending gameplay and external blockers'
