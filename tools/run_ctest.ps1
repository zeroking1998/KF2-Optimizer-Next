# All CTest entry points share the host process list, even across worktrees.
$ErrorActionPreference = 'Stop'
$testRun = [System.Threading.Mutex]::new($false, 'Global\KF2OptimizerNext.TestRun')
$ownsTestRun = $false
try {
    try { $ownsTestRun = $testRun.WaitOne(0) }
    catch [System.Threading.AbandonedMutexException] { $ownsTestRun = $true }
    if (-not $ownsTestRun) {
        throw 'Another KF2 Optimizer test run is active on this host. Run test suites sequentially.'
    }
    & ctest @args --parallel 1
    exit $LASTEXITCODE
}
finally {
    if ($ownsTestRun) { $testRun.ReleaseMutex() }
    $testRun.Dispose()
}
