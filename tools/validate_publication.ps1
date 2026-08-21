[CmdletBinding()]
param(
    [switch] $AllowEnclosingRepository
)

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$failures = [System.Collections.Generic.List[string]]::new()

$requiredFiles = @(
    '.gitattributes',
    '.gitignore',
    'LICENSE',
    'README.md',
    'THIRD_PARTY_NOTICES.md',
    'assets/PROVENANCE.md',
    'third_party/presentmon/LICENSE.txt',
    '.github/workflows/windows-ci.yml',
    '.github/ISSUE_TEMPLATE/bug_report.yml',
    '.github/ISSUE_TEMPLATE/feature_request.yml',
    '.github/PULL_REQUEST_TEMPLATE.md'
)
foreach ($relativePath in $requiredFiles) {
    $requiredPath = Join-Path $projectRoot $relativePath
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        $failures.Add("Missing publication file: $relativePath")
    }
}

$gitRootText = (& git -C $projectRoot rev-parse --show-toplevel 2>$null)
if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($gitRootText)) {
    $gitRoot = [IO.Path]::GetFullPath($gitRootText.Trim())
    if ($gitRoot -ne $projectRoot) {
        $message = "The enclosing Git history starts at $gitRoot. Publish a " +
            'fresh repository rooted at KF2Optimizer; do not push the enclosing history.'
        if ($AllowEnclosingRepository) {
            Write-Warning $message
        }
        else {
            $failures.Add($message)
        }
    }
}

$files = @(Get-ChildItem -LiteralPath $projectRoot -Recurse -File -Force |
    Where-Object {
        $relativePath = $_.FullName.Substring($projectRoot.Length + 1)
        $relativePath -notlike 'out\*' -and
        $relativePath -notlike '.git\*' -and
        $relativePath -ne 'assets\offline_telemetry\KF2OptimizerTelemetry.u'
    })

$forbiddenExtensions = @('.exe', '.dll', '.pdb', '.lib', '.zip', '.7z',
                         '.rar', '.pfx', '.p12', '.pem', '.key')
foreach ($file in $files) {
    $relativePath = $file.FullName.Substring($projectRoot.Length + 1)
    if ($forbiddenExtensions -contains $file.Extension.ToLowerInvariant()) {
        $failures.Add("Forbidden publication artifact: $relativePath")
    }
    if ($file.Length -gt 5MB) {
        $failures.Add("Publication file exceeds 5 MiB: $relativePath")
    }
    if ($file.Name -match '(?i)(password|credential|private[-_]?key|secret|token)') {
        $failures.Add("Sensitive-looking file name: $relativePath")
    }
}

$textExtensions = @('.cpp', '.c', '.h', '.hpp', '.ps1', '.cmake', '.md',
                    '.yml', '.yaml', '.json', '.ini', '.uc', '.txt')
$secretPattern = 'BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY|github_pat_[A-Za-z0-9_]+|ghp_[A-Za-z0-9]+|AKIA[0-9A-Z]{16}|(?i)(?:api[_-]?key|client[_-]?secret|access[_-]?token)\s*[:=]\s*["''][^"'']{8,}'
$personalPathPattern = '(?i)(?:[A-Z]:\\Users\\alexw|D:\\KF2|E:\\KF2|zeroking1998)'
foreach ($file in $files | Where-Object {
        $textExtensions -contains $_.Extension.ToLowerInvariant() -and
        $_.FullName -ne $PSCommandPath
    }) {
    $content = Get-Content -LiteralPath $file.FullName -Raw
    $relativePath = $file.FullName.Substring($projectRoot.Length + 1)
    if ($content -match $secretPattern) {
        $failures.Add("Potential secret in: $relativePath")
    }
    if ($content -match $personalPathPattern) {
        $failures.Add("Personal or workspace identity in: $relativePath")
    }
}

$ignoreText = Get-Content -LiteralPath (Join-Path $projectRoot '.gitignore') -Raw
if ($ignoreText -notmatch '(?m)^/out/\r?$') {
    $failures.Add('.gitignore must exclude /out/.')
}
if ($ignoreText -notmatch
        '(?m)^/assets/offline_telemetry/KF2OptimizerTelemetry\.u\r?$') {
    $failures.Add('.gitignore must exclude the locally SDK-compiled telemetry package.')
}

$presentMonLicensePath = Join-Path $projectRoot `
    'third_party/presentmon/LICENSE.txt'
$presentMonLicense = Get-Content -LiteralPath $presentMonLicensePath -Raw
if ($presentMonLicense -notmatch 'Copyright \(C\) 2017-2024 Intel Corporation' -or
    $presentMonLicense -notmatch 'Permission is hereby granted') {
    $failures.Add('The complete Intel PresentMon license is missing or changed.')
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    throw "Publication validation failed with $($failures.Count) error(s)."
}

Write-Host "Publication validation passed: $($files.Count) active files checked."
