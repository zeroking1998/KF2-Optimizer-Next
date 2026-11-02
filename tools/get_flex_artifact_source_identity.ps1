[CmdletBinding()]
param(
    [Parameter(Mandatory)]
    [string] $Artifact
)

$ErrorActionPreference = 'Stop'
$path = [IO.Path]::GetFullPath($Artifact)
if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "FleX artifact is missing: $path"
}
$bytes = [IO.File]::ReadAllBytes($path)
if ($bytes.Length -lt 512 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
    throw "FleX artifact is not a valid PE image: $path"
}
$text = [Text.Encoding]::ASCII.GetString($bytes)
$prefix = 'KF2_FLEX_BUILD_COMMIT='
$start = $text.IndexOf($prefix, [StringComparison]::Ordinal)
if ($start -lt 0) {
    throw "FleX artifact source identity is missing: $path"
}
$start += $prefix.Length
$end = $text.IndexOf([char]0, $start)
if ($end -lt 0) { throw "FleX artifact source identity is invalid: $path" }
$identity = $text.Substring($start, $end - $start)
if ($identity -cnotmatch '^[A-Za-z0-9._-]{1,128}$') {
    throw "FleX artifact source identity is invalid: $path"
}
$identity
