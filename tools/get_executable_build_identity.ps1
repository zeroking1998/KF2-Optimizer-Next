[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $Executable
)

$ErrorActionPreference = 'Stop'
# Read Windows version resources as data; never execute a package to inspect it.
$version = [Diagnostics.FileVersionInfo]::GetVersionInfo(
    [IO.Path]::GetFullPath($Executable)).ProductVersion
if ($version -cnotmatch '^(?<version>[0-9]+\.[0-9]+\.[0-9]+(?:-[A-Za-z0-9.-]+)?)\+(?<source>[A-Za-z0-9._-]{1,128}) \((?<channel>dev|release)\)$') {
    throw 'Executable build identity is missing or invalid; rebuild the package'
}
[pscustomobject]@{
    version = $Matches.version
    source_identity = $Matches.source
    channel = $Matches.channel
}
