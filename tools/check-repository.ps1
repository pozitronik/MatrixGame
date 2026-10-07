[CmdletBinding()]
param(
    [string]$BaseRef = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$trackedFiles = @(git -C $repoRoot ls-files)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect tracked files.' }
$forbidden = @($trackedFiles | Where-Object {
    $_ -match '(?i)\.pkg$' -or $_ -match '(?i)^robots\.dat$' -or
    $_ -match '^(?:\.tools|\.local|\.codex|\.claude)/' -or
    $_ -match '(^|/)YOUR\.GITHUB\.NAME$'
})
if ($forbidden.Count) {
    throw "Local-only content is tracked: $($forbidden -join ', ')"
}
foreach ($file in @('AGENTS.md', 'CONTRIBUTING.md', 'docs/ROADMAP.md', 'docs/BUILD_WINDOWS.md', 'docs/TESTING.md')) {
    if (-not (Test-Path -LiteralPath (Join-Path $repoRoot $file))) { throw "Missing project document: $file" }
}
foreach ($resource in @('robots.pkg', 'robots.dat')) {
    git -C $repoRoot check-ignore --quiet --no-index $resource
    if ($LASTEXITCODE -ne 0) { throw "$resource must be covered by .gitignore." }
}
if ($BaseRef) {
    git -C $repoRoot diff --check "$BaseRef...HEAD"
    if ($LASTEXITCODE -ne 0) { throw 'Committed changes contain whitespace errors.' }
}
git -C $repoRoot diff --check
if ($LASTEXITCODE -ne 0) { throw 'Working-tree changes contain whitespace errors.' }
git -C $repoRoot diff --cached --check
if ($LASTEXITCODE -ne 0) { throw 'Staged changes contain whitespace errors.' }
Write-Output 'Repository checks passed; local resources and tools are excluded.'
