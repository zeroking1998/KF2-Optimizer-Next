param(
    [Parameter(Mandatory = $true)]
    [string]$ConfigRoot,
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$source = [System.IO.Path]::GetFullPath($ConfigRoot)
$engine = Join-Path $source 'KFEngine.ini'
if (-not (Test-Path -LiteralPath $engine -PathType Leaf)) {
    Write-Output 'BLOCKED: KFEngine.ini was not found in the supplied config root.'
    exit 3
}

$project = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = Join-Path $project ("out/build/windows-x64-" + $Configuration.ToLowerInvariant())
$validationBase = [System.IO.Path]::GetFullPath((Join-Path $project 'out/config-roundtrip'))
$validation = Join-Path $validationBase ([guid]::NewGuid().ToString('N'))
$copy = Join-Path $validation 'Config'
$state = Join-Path $validation 'State'

New-Item -ItemType Directory -Path $copy -Force | Out-Null
try {
    $names = @('KFEngine.ini', 'KFGame.ini', 'KFSystemSettings.ini')
    $sourceHashes = @{}
    $copyHashes = @{}
    foreach ($name in $names) {
        $candidate = Join-Path $source $name
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "Required source INI is missing: $name"
        }
        $sourceHashes[$name] = (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash
        Copy-Item -LiteralPath $candidate -Destination (Join-Path $copy $name)
        $copyHashes[$name] = (Get-FileHash -LiteralPath (Join-Path $copy $name) -Algorithm SHA256).Hash
    }
    cmake --build $build --target KF2ConfigRoundtrip --config $Configuration
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    $tool = Join-Path $build ("$Configuration/KF2ConfigRoundtrip.exe")
    & $tool $copy $state
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    foreach ($name in $names) {
        $copyAfter = (Get-FileHash -LiteralPath (Join-Path $copy $name) -Algorithm SHA256).Hash
        if ($copyHashes[$name] -ne $copyAfter) {
            throw "Roundtrip hash mismatch: $name"
        }
        $sourceAfter = (Get-FileHash -LiteralPath (Join-Path $source $name) -Algorithm SHA256).Hash
        if ($sourceHashes[$name] -ne $sourceAfter) {
            throw "Source INI changed unexpectedly: $name"
        }
    }
    Write-Output 'PASS: copied real 3-file config restored byte-identically; source INIs stayed unchanged.'
}
finally {
    $resolvedBase = [System.IO.Path]::GetFullPath($validationBase)
    $resolvedValidation = [System.IO.Path]::GetFullPath($validation)
    if ($resolvedValidation.StartsWith($resolvedBase + [System.IO.Path]::DirectorySeparatorChar,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        Remove-Item -LiteralPath $resolvedValidation -Recurse -Force -ErrorAction SilentlyContinue
    }
}
