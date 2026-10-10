# MatrixGame - licensed under GPLv2 or any later version.
[CmdletBinding()]
param(
    [string]$Python = 'python',
    [int]$Jobs = 4,
    [string]$OutputDirectory = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$revision = git -C $repoRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve source revision.' }
if (git -C $repoRoot status --porcelain --untracked-files=no) { throw 'Commit tracked changes before packaging.' }
$extraSources = git -C $repoRoot ls-files --others --exclude-standard -- MatrixGame/src MatrixLib ThirdParty cmake
if ($LASTEXITCODE -ne 0 -or $extraSources) { throw 'Untracked production source prevents source identification.' }
& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release -WithoutResources -Jobs $Jobs
$buildRoot = Join-Path $repoRoot 'build/mingw-release-exe'
$binary = Join-Path $buildRoot 'MatrixGame/MatrixGame.exe'
$receipt = @{
    revision = $revision
    binary_sha256 = (Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash.ToLowerInvariant()
    cache_sha256 = (Get-FileHash -LiteralPath (Join-Path $buildRoot 'CMakeCache.txt') -Algorithm SHA256).Hash.ToLowerInvariant()
}
$receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $buildRoot 'packaging-build.json') -Encoding UTF8
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $repoRoot 'build/packages' }
$objdump = Join-Path $repoRoot '.tools/winlibs-13.2.0/mingw32/bin/objdump.exe'
& $Python -B (Join-Path $PSScriptRoot 'package_game.py') --root $repoRoot --build $buildRoot --output $OutputDirectory --objdump $objdump
if ($LASTEXITCODE -ne 0) { throw 'Standalone packaging failed.' }
