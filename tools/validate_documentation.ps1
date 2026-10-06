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
    '.vsconfig',
    'setup.cmd',
    'build.cmd',
    'package.cmd',
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
    'docs/BUILDING.md',
    'docs/CODE_STYLE.md',
    'docs/GLOSSARY.md',
    'docs/OPEN_SOURCE_CHECKLIST.md',
    '.github/workflows/windows-ci.yml',
    '.github/ISSUE_TEMPLATE/bug_report.yml',
    '.github/ISSUE_TEMPLATE/feature_request.yml',
    '.github/ISSUE_TEMPLATE/help_request.yml',
    '.github/PULL_REQUEST_TEMPLATE.md',
    'tools/build_kf2_telemetry.ps1',
    'tools/check_requirements.ps1',
    'tools/install_requirements.ps1',
    'tools/download_telemetry_seed.ps1',
    'tools/build_for_contributors.ps1',
    'tools/validate_publication.ps1'
)

foreach ($relativePath in $requiredFiles) {
    $absolutePath = Join-Path $repositoryRoot $relativePath
    if (-not (Test-Path -LiteralPath $absolutePath -PathType Leaf)) {
        $failures.Add("Missing required documentation file: $relativePath")
    }
}

# Counts are inventory, not run results. Count literal registrations for the
# default full Windows build; never silently guess generated test names.
$testSource = Get-Content -LiteralPath (Join-Path $repositoryRoot 'tests/CMakeLists.txt') -Raw -Encoding UTF8
$testNames = [regex]::Matches($testSource, '(?m)^\s*add_test\s*\(\s*NAME\s+([A-Za-z0-9_]+)\b')
$testCount = $testNames.Count
if ($testCount -eq 0 -or
    $testCount -ne [regex]::Matches($testSource, '(?m)^\s*add_test\s*\(').Count -or
    @($testNames | ForEach-Object { $_.Groups[1].Value } |
        Select-Object -Unique).Count -ne $testCount) {
    $failures.Add('Default Windows test inventory cannot be counted unambiguously.')
}
$catalogSource = Get-Content -LiteralPath (Join-Path $repositoryRoot 'src/config/kf2_catalog.cpp') -Raw -Encoding UTF8
$catalogNames = [regex]::Matches($catalogSource, '(?m)^\s*\{SettingId::([A-Za-z0-9_]+),')
$catalogCount = $catalogNames.Count
$catalogHeader = Get-Content -LiteralPath (Join-Path $repositoryRoot 'include/kf2/config/kf2_catalog.hpp') -Raw -Encoding UTF8
$declared = [regex]::Match($catalogHeader, 'kVerifiedSettingCount\{([0-9]+)\}')
if (-not $declared.Success -or $catalogCount -eq 0 -or
    [int]$declared.Groups[1].Value -ne $catalogCount -or
    @($catalogNames | ForEach-Object { $_.Groups[1].Value } |
        Select-Object -Unique).Count -ne $catalogCount) {
    $failures.Add('Native settings catalog inventory disagrees with its declared size.')
}
$inventory = Get-Content -LiteralPath (Join-Path $repositoryRoot 'docs/ISSUE_72_PRODUCT_MATRIX.md') -Raw -Encoding UTF8
foreach ($fact in @(
    @{ Label = 'Default full Windows CTest registrations'; Count = $testCount },
    @{ Label = 'Native managed settings catalog definitions'; Count = $catalogCount }
)) {
    $records = [regex]::Matches($inventory,
        '(?m)^- ' + [regex]::Escape($fact.Label) + ': ([0-9]+)\.\r?$')
    if ($records.Count -ne 1 -or [int]$records[0].Groups[1].Value -ne $fact.Count) {
        $failures.Add("Stale or missing validation inventory: $($fact.Label); expected $($fact.Count).")
    }
}
foreach ($relative in @('docs/PROJECT_STATUS.md', 'docs/ISSUE_72_PRODUCT_MATRIX.md',
                        'docs/FINAL_ACCEPTANCE.md', 'docs/function-matrix.md')) {
    $status = Get-Content -LiteralPath (Join-Path $repositoryRoot $relative) -Raw -Encoding UTF8
    foreach ($match in [regex]::Matches($status,
        '(?i)\b([0-9]+)(?:/([0-9]+))?(?:-test|\s+tests?)\b|\bsuite passes ([0-9]+)/([0-9]+)')) {
        $counts = @($match.Groups | Select-Object -Skip 1 |
            Where-Object { $_.Success } | ForEach-Object { [int]$_.Value })
        if (@($counts | Where-Object { $_ -ne $testCount }).Count -ne 0) {
            $failures.Add("Stale test total in $relative; use the checked validation inventory.")
        }
    }
    foreach ($match in [regex]::Matches($status,
        '(?i)\b([0-9]+)(?:-setting (?:verified )?catalog|\s+(?:typed\s+)?(?:catalog\s+)?settings)\b')) {
        if ([int]$match.Groups[1].Value -ne $catalogCount) {
            $failures.Add("Stale catalog total in $relative; use the checked validation inventory.")
        }
    }
}

$markdownFiles = Get-ChildItem -LiteralPath $repositoryRoot -Recurse -File -Filter '*.md' |
    Where-Object {
        $_.FullName -notlike (Join-Path $repositoryRoot 'out\*') -and
        $_.FullName -notlike (Join-Path $repositoryRoot 'third_party\*') -and
        $_.FullName -notlike (Join-Path $repositoryRoot '.git\*')
    }

foreach ($markdownFile in $markdownFiles) {
    $content = Get-Content -LiteralPath $markdownFile.FullName -Raw -Encoding UTF8
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
$germanPattern = '(?i)\b(?:spiel|leiche|leichen|einstellungen|optimierung|diagnose|pr\u00fcfung|vorschau|wiederherstellung|qualit\u00e4t|steuerung|ordner|meldungen|werkzeuge|weitere|starten|verf\u00fcgbar|best\u00e4tigt|gesch\u00fctzt|gepr\u00fcft|oben|unten|rechts|normaler|sicherer|benutzer|anzeige|leistung|fehler|beobachten|verwerfen|vollstaendig|ausschliesslich|zusaetzlich|spaeter|zustaende|groesse|aender\w*|pruef\w*|rueck\w*|ueber\w*)\b|[\u00c4\u00d6\u00dc\u00e4\u00f6\u00fc\u00df]'

foreach ($root in $englishRoots) {
    $files = Get-ChildItem -LiteralPath (Join-Path $repositoryRoot $root) -Recurse -File |
        Where-Object {
            $sourceExtensions -contains $_.Extension -and
            $_.FullName -ne $PSCommandPath
        }
    foreach ($file in $files) {
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $file.FullName -Encoding UTF8) {
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
    foreach ($line in Get-Content -LiteralPath $markdownFile.FullName -Encoding UTF8) {
        $lineNumber++
        if ($line -match $germanPattern) {
            $relativeSource = $markdownFile.FullName.Substring(
                $repositoryRoot.Length + 1)
            $failures.Add("Non-English documentation text in ${relativeSource}:${lineNumber}")
        }
    }
}

$readme = Get-Content -LiteralPath (Join-Path $repositoryRoot 'README.md') -Raw -Encoding UTF8
foreach ($heading in @('## What it does', '## Start here', '## Build from source',
                        '## Contributing', '## License')) {
    if (-not $readme.Contains($heading)) {
        $failures.Add("README is missing required heading: $heading")
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw "Documentation validation failed with $($failures.Count) error(s)."
}

$license = Get-Content -LiteralPath (Join-Path $repositoryRoot 'LICENSE') -Raw -Encoding UTF8
if (-not $license.Contains('GNU GENERAL PUBLIC LICENSE') -or
    -not $license.Contains('Version 3, 29 June 2007') -or
    -not $license.Contains('END OF TERMS AND CONDITIONS')) {
    throw 'The root LICENSE is not a complete GNU GPL version 3 license.'
}

Write-Host "Documentation validation passed: $($markdownFiles.Count) Markdown files checked; $testCount default Windows test registrations; $catalogCount native catalog definitions."
