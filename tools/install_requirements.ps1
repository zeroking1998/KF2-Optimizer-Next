[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'KF2 Optimizer development requires Windows 10 or newer.'
}
if (-not (Get-Command winget -ErrorAction SilentlyContinue)) {
    throw 'Windows Package Manager is missing. Install App Installer from Microsoft Store.'
}

$commonArguments = @(
    'install',
    '--exact',
    '--accept-package-agreements',
    '--accept-source-agreements',
    '--silent'
)
$packages = @(
    'Git.Git',
    'Microsoft.PowerShell',
    'Kitware.CMake'
)

foreach ($package in $packages) {
    Write-Host "Installing or confirming $package..."
    & winget @commonArguments --id $package
    if ($LASTEXITCODE -ne 0) {
        throw "Windows Package Manager failed for $package with exit code $LASTEXITCODE."
    }
}

Write-Host 'Installing or confirming Visual Studio 2022 C++ Build Tools...'
$visualStudioOptions = @(
    '--wait',
    '--passive',
    '--add',
    'Microsoft.VisualStudio.Workload.VCTools',
    '--includeRecommended'
) -join ' '
$visualStudioArguments = $commonArguments + @(
    '--id',
    'Microsoft.VisualStudio.2022.BuildTools',
    '--override',
    $visualStudioOptions
)
& winget @visualStudioArguments
if ($LASTEXITCODE -ne 0) {
    throw "Visual Studio Build Tools setup failed with exit code $LASTEXITCODE."
}

Write-Host ''
Write-Host 'Development tools are installed.' -ForegroundColor Green
Write-Host 'Close this window, open a new terminal, then run build.cmd.'
Write-Host ('For a complete portable package, also install the official ' +
    'Killing Floor 2 SDK through Steam.')
