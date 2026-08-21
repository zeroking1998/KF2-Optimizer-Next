[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$failures = [System.Collections.Generic.List[string]]::new()

$requiredFiles = @(
    'LICENSE',
    '.gitattributes',
    '.gitignore',
    'README.md',
    'CONTRIBUTING.md',
    'CODE_OF_CONDUCT.md',
    'SECURITY.md',
    'SUPPORT.md',
    'CHANGELOG.md',
    'ROADMAP.md',
    'THIRD_PARTY_NOTICES.md',
    'assets/PROVENANCE.md',
    'docs/README.md',
    'docs/BINARY_DISTRIBUTION.md',
    'docs/USER_GUIDE.md',
    'docs/HOW_IT_WORKS.md',
    'docs/FEATURE_REFERENCE.md',
    'docs/SAFETY.md',
    'docs/DEVELOPER_GUIDE.md',
    'docs/CODE_STYLE.md',
    'docs/GLOSSARY.md',
    'docs/OPEN_SOURCE_CHECKLIST.md',
    '.github/workflows/windows-ci.yml',
    '.github/ISSUE_TEMPLATE/bug_report.yml',
    '.github/ISSUE_TEMPLATE/feature_request.yml',
    '.github/PULL_REQUEST_TEMPLATE.md',
    'tools/build_kf2_telemetry.ps1',
    'tools/validate_publication.ps1'
)

foreach ($relativePath in $requiredFiles) {
    $absolutePath = Join-Path $repositoryRoot $relativePath
    if (-not (Test-Path -LiteralPath $absolutePath -PathType Leaf)) {
        $failures.Add("Missing required documentation file: $relativePath")
    }
}

$markdownFiles = Get-ChildItem -LiteralPath $repositoryRoot -Recurse -File -Filter '*.md' |
    Where-Object {
        $_.FullName -notlike (Join-Path $repositoryRoot 'out\*') -and
        $_.FullName -notlike (Join-Path $repositoryRoot 'third_party\*') -and
        $_.FullName -notlike (Join-Path $repositoryRoot '.git\*')
    }

foreach ($markdownFile in $markdownFiles) {
    $content = Get-Content -LiteralPath $markdownFile.FullName -Raw
    $matches = [regex]::Matches($content, '\[[^\]]+\]\(([^)]+)\)')
    foreach ($match in $matches) {
        $target = $match.Groups[1].Value.Trim()
        if ($target -match '^(?:https?://|mailto:|#)') { continue }
        $target = ($target -split '#', 2)[0]
        if ([string]::IsNullOrWhiteSpace($target)) { continue }
        $decodedTarget = [Uri]::UnescapeDataString($target)
        $resolved = Join-Path $markdownFile.DirectoryName $decodedTarget
        if (-not (Test-Path -LiteralPath $resolved)) {
            $relativeSource = $markdownFile.FullName.Substring(
                $repositoryRoot.Length + 1)
            $failures.Add("Broken local link in ${relativeSource}: $target")
        }
    }
}

$englishRoots = @('src', 'include', 'tests', 'assets', 'tools', '.github')
$sourceExtensions = @('.cpp', '.c', '.h', '.hpp', '.ps1', '.cmake', '.uc',
                      '.yml', '.yaml')
$germanPattern = '(?i)\b(?:spiel|leiche|leichen|einstellungen|optimierung|diagnose|prüfung|vorschau|wiederherstellung|qualität|steuerung|ordner|meldungen|werkzeuge|weitere|starten|verfügbar|bestätigt|geschützt|geprüft|oben|unten|rechts|normaler|sicherer|benutzer|anzeige|leistung|fehler|beobachten|verwerfen|vollstaendig|ausschliesslich|zusaetzlich|spaeter|zustaende|groesse|aender\w*|pruef\w*|rueck\w*|ueber\w*)\b|[ÄÖÜäöüß]'

foreach ($root in $englishRoots) {
    $files = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot $root) -Recurse -File |
        Where-Object {
            $sourceExtensions -contains $_.Extension -and
            $_.FullName -ne $PSCommandPath
        }
    foreach ($file in $files) {
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $file.FullName) {
            $lineNumber++
            if ($line -match $germanPattern) {
                $relativeSource = $file.FullName.Substring(
                    $repositoryRoot.Length + 1)
                $failures.Add("Non-English project text in ${relativeSource}:${lineNumber}")
            }
        }
    }
}

foreach ($markdownFile in $markdownFiles) {
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $markdownFile.FullName) {
        $lineNumber++
        if ($line -match $germanPattern) {
            $relativeSource = $markdownFile.FullName.Substring(
                $repositoryRoot.Length + 1)
            $failures.Add("Non-English documentation text in ${relativeSource}:${lineNumber}")
        }
    }
}

$readme = Get-Content -LiteralPath (Join-Path $repositoryRoot 'README.md') -Raw
foreach ($heading in @('## What it does', '## Start here', '## Build and test',
                        '## Contributing', '## License')) {
    if (-not $readme.Contains($heading)) {
        $failures.Add("README is missing required heading: $heading")
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw "Documentation validation failed with $($failures.Count) error(s)."
}

$license = Get-Content -LiteralPath (Join-Path $repositoryRoot 'LICENSE') -Raw
if (-not $license.Contains('GNU GENERAL PUBLIC LICENSE') -or
    -not $license.Contains('Version 3, 29 June 2007') -or
    -not $license.Contains('END OF TERMS AND CONDITIONS')) {
    throw 'The root LICENSE is not a complete GNU GPL version 3 license.'
}

Write-Host "Documentation validation passed: $($markdownFiles.Count) Markdown files checked."
