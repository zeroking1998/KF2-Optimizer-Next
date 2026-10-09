[CmdletBinding()]
param([Parameter(Mandatory)][string] $TestRoot)

$ErrorActionPreference = 'Stop'
$project = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$prefix = [IO.Path]::GetFullPath($TestRoot).TrimEnd('\') + '\'
$fixture = Join-Path $prefix ('validation-' + [guid]::NewGuid().ToString('N'))
if (-not [IO.Path]::GetFullPath($fixture).StartsWith($prefix,
        [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe validation fixture' }

try {
    foreach ($relative in @('README.md', 'docs/source.md', 'assets/out/nested.md',
            'extra/untracked.exe', 'assets/offline_telemetry/KF2OptimizerTelemetry.u',
            'out/ignored.md', '.git/ignored.md', 'third_party/vendor.md', 'hidden.md')) {
        $path = Join-Path $fixture $relative
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force
        $null = New-Item -ItemType File -Path $path
    }
    (Get-Item -LiteralPath (Join-Path $fixture 'hidden.md')).Attributes = 'Hidden'
    $null = New-Item -ItemType Junction -Path (Join-Path $fixture 'linked-builds') `
        -Value (Join-Path $fixture 'out')

    # Execute the real selectors; reject entering excluded trees before filtering.
    function Get-ChildItem {
        [CmdletBinding()]
        param([string]$LiteralPath, [switch]$Recurse, [switch]$File,
              [switch]$Force, [string]$Filter)
        if ($Recurse -and ($LiteralPath -eq $fixture -or
            $LiteralPath -eq (Join-Path $fixture 'out') -or
            $LiteralPath -eq (Join-Path $fixture '.git') -or
            ($variable -eq 'markdownFiles' -and
             $LiteralPath -eq (Join-Path $fixture 'third_party')))) {
            throw 'Validator recurses into a tree that must be excluded first'
        }
        Microsoft.PowerShell.Management\Get-ChildItem @PSBoundParameters
    }
    foreach ($case in @(
        @{Script='validate_documentation'; Variable='markdownFiles';
          Expected=@('README.md', 'docs/source.md', 'assets/out/nested.md')},
        @{Script='validate_publication'; Variable='files'; Expected=@('README.md',
          'docs/source.md', 'assets/out/nested.md', 'extra/untracked.exe',
          'third_party/vendor.md', 'hidden.md')}
    )) {
        $variable = $case.Variable
        $ast = [Management.Automation.Language.Parser]::ParseFile(
            (Join-Path $project "tools/$($case.Script).ps1"), [ref]$null, [ref]$null)
        $selection = $ast.Find({ param($node)
            $node -is [Management.Automation.Language.AssignmentStatementAst] -and
            $node.Left.VariablePath.UserPath -eq $variable
        }, $true)
        if (-not $selection) { throw "Missing file selector: $($case.Script)" }
        $repositoryRoot = $projectRoot = $fixture
        $actual = @(& ([scriptblock]::Create($selection.Right.Extent.Text)) |
            ForEach-Object FullName | Sort-Object)
        $expected = @($case.Expected | ForEach-Object { Join-Path $fixture $_ } | Sort-Object)
        if (Compare-Object $expected $actual) { throw "Wrong selected files: $($case.Script)" }
    }
    Write-Host 'PASS: early exclusions, new/nested/hidden files and directory links'
} finally {
    if (Test-Path -LiteralPath $fixture) {
        Remove-Item -LiteralPath $fixture -Recurse -Force
    }
}
