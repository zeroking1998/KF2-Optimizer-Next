[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$validationRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'out\validation'))
$buildRoots = @(
    [IO.Path]::GetFullPath((Join-Path $validationRoot 'build-a')),
    [IO.Path]::GetFullPath((Join-Path $validationRoot 'build-b'))
)

function Write-Pass([string] $Message) {
    Write-Host "PASS: $Message" -ForegroundColor Green
}

function Assert-True([bool] $Condition, [string] $Message) {
    if (-not $Condition) {
        throw $Message
    }
}

function Invoke-Native([string] $Command, [string[]] $Arguments) {
    & $Command @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Native command failed ($LASTEXITCODE): $Command $($Arguments -join ' ')"
    }
}

function Clear-ValidatedDirectory([string] $Target) {
    $fullTarget = [IO.Path]::GetFullPath($Target)
    $allowedPrefix = $validationRoot.TrimEnd('\') + '\'
    Assert-True ($fullTarget.StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) `
        "Refusing to remove path outside validation root: $fullTarget"
    if (Test-Path -LiteralPath $fullTarget) {
        $lastError = $null
        for ($attempt = 0; $attempt -lt 8 -and (Test-Path -LiteralPath $fullTarget); $attempt++) {
            try {
                Remove-Item -LiteralPath $fullTarget -Recurse -Force -ErrorAction Stop
            }
            catch {
                $lastError = $_
                Start-Sleep -Milliseconds (25 * [Math]::Pow(2, $attempt))
            }
        }
        if (Test-Path -LiteralPath $fullTarget) {
            throw "Validation directory could not be cleared: $fullTarget | $lastError"
        }
    }
}

$requiredDocs = @('architecture.md', 'function-matrix.md', 'testing.md') |
    ForEach-Object { Join-Path $projectRoot "docs\$_" }
foreach ($document in $requiredDocs) {
    Assert-True (Test-Path -LiteralPath $document -PathType Leaf) `
        "Foundation documentation is missing: $document"
}
Write-Pass 'foundation documentation exists'

$sourceFiles = @(
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'src') -File -Recurse
    Get-ChildItem -LiteralPath (Join-Path $projectRoot 'include') -File -Recurse
    Get-Item -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt')
)
$legacyReferences = @($sourceFiles | Select-String -Pattern 'Quellcode|R24Runtime')
Assert-True ($legacyReferences.Count -eq 0) 'New source references a legacy implementation path'
Write-Pass 'new source is isolated from legacy paths'

$matrixText = Get-Content -Raw -LiteralPath (Join-Path $projectRoot 'docs\function-matrix.md')
$expectedIds = @(
    1..33 | ForEach-Object { 'N{0:D2}' -f $_ }
    1..10 | ForEach-Object { 'L{0:D2}' -f $_ }
    1..6 | ForEach-Object { 'X{0:D2}' -f $_ }
)
$missingIds = @($expectedIds | Where-Object { $matrixText -notmatch "\b$($_)\b" })
Assert-True ($missingIds.Count -eq 0) "Function matrix is missing: $($missingIds -join ', ')"
Write-Pass 'function matrix contains N01-N33, L01-L10, and X01-X06'

Invoke-Native 'pwsh' @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'test.ps1'), '-Configuration', 'Debug')
Invoke-Native 'pwsh' @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'test.ps1'), '-Configuration', 'Release')
Write-Pass 'Debug and Release tests pass'
Invoke-Native 'pwsh' @('-NoProfile', '-File', (Join-Path $PSScriptRoot 'validate_gui.ps1'), '-Configuration', 'Release')
Write-Pass 'deterministic native GUI captures pass'

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
Assert-True (Test-Path -LiteralPath $vswhere -PathType Leaf) 'vswhere.exe was not found'
$installation = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
Assert-True (-not [string]::IsNullOrWhiteSpace($installation)) 'Visual Studio installation was not found'
$dumpbin = Get-ChildItem -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') `
    -Filter dumpbin.exe -File -Recurse |
    Where-Object FullName -Match 'Hostx64\\x64' |
    Sort-Object FullName -Descending |
    Select-Object -First 1 -ExpandProperty FullName
Assert-True (-not [string]::IsNullOrWhiteSpace($dumpbin)) 'x64 dumpbin.exe was not found'

foreach ($buildRoot in $buildRoots) {
    Clear-ValidatedDirectory $buildRoot
    Invoke-Native 'cmake' @(
        '-S', $projectRoot,
        '-B', $buildRoot,
        '-G', 'Visual Studio 17 2022',
        '-A', 'x64',
        '-DBUILD_TESTING=ON',
        '-DKF2_VERSION=0.1.0',
        '-DKF2_BUILD_COMMIT=validation',
        '-DKF2_BUILD_CHANNEL=validation'
    )
    Invoke-Native 'cmake' @('--build', $buildRoot, '--config', 'Release', '--clean-first')
    Invoke-Native 'ctest' @('--test-dir', $buildRoot, '-C', 'Release', '--output-on-failure')

    $releaseRoot = Join-Path $buildRoot 'Release'
    $productExecutables = @(Get-ChildItem -LiteralPath $releaseRoot -Filter '*.exe' -File)
    Assert-True ($productExecutables.Count -eq 1) `
        "Expected one product executable in $releaseRoot, found $($productExecutables.Count)"
    Assert-True ($productExecutables[0].Name -ceq 'KF2Optimizer.exe') `
        "Unexpected product executable: $($productExecutables[0].Name)"
    $productDlls = @(Get-ChildItem -LiteralPath $releaseRoot -Filter '*.dll' -File)
    Assert-True ($productDlls.Count -eq 0) 'Foundation produced an unexpected root product DLL'
    $labForwarder = Join-Path $buildRoot 'flexRelease_x64.forwarder-lab.dll'
    Assert-True (Test-Path -LiteralPath $labForwarder -PathType Leaf) `
        'Offline laboratory forwarder was not reproducibly built'
}
Write-Pass 'release shape contains one user-facing executable and one isolated laboratory DLL'

$firstExecutable = Join-Path $buildRoots[0] 'Release\KF2Optimizer.exe'
$headers = (& $dumpbin /headers $firstExecutable | Out-String)
Assert-True ($headers -match '8664 machine \(x64\)') 'Product is not a PE32+ x64 image'
Assert-True ($headers -match 'subsystem \(Windows GUI\)') 'Product is not a Windows GUI executable'

$imports = (& $dumpbin /imports $firstExecutable | Out-String)
$forbiddenImports = 'clr|webview|detours|ReadProcessMemory|WriteProcessMemory|flexRelease'
Assert-True ($imports -notmatch $forbiddenImports) 'Product contains a forbidden import'
Write-Pass 'PE architecture, subsystem, and imports are valid'

$firstHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $firstExecutable).Hash
$secondHash = (Get-FileHash -Algorithm SHA256 -LiteralPath `
    (Join-Path $buildRoots[1] 'Release\KF2Optimizer.exe')).Hash
Assert-True ($firstHash -ceq $secondHash) `
    "Release builds are not reproducible: $firstHash != $secondHash"
Write-Pass "two clean Release builds are identical ($firstHash)"
$firstForwarderHash = (Get-FileHash -Algorithm SHA256 -LiteralPath `
    (Join-Path $buildRoots[0] 'flexRelease_x64.forwarder-lab.dll')).Hash
$secondForwarderHash = (Get-FileHash -Algorithm SHA256 -LiteralPath `
    (Join-Path $buildRoots[1] 'flexRelease_x64.forwarder-lab.dll')).Hash
Assert-True ($firstForwarderHash -ceq $secondForwarderHash) `
    "FleX laboratory forwarder is not reproducible: $firstForwarderHash != $secondForwarderHash"
Write-Pass "two clean FleX laboratory forwarders are identical ($firstForwarderHash)"
