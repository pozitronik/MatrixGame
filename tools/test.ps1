# MatrixGame - licensed under GPLv2 or any later version.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('MinGW', 'MSVC')][string]$Compiler = 'MinGW',
    [switch]$DLL,
    [switch]$Cheats,
    [switch]$NoBuild,
    [string]$Filter = '',
    [string]$ToolchainRoot = '',
    [string]$CMakeRoot = '',
    [int]$Jobs = 4
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $ToolchainRoot) { $ToolchainRoot = Join-Path $repoRoot '.tools/winlibs-13.2.0/mingw32' }
$ToolchainRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ToolchainRoot)
if (-not $CMakeRoot) { $CMakeRoot = Join-Path $repoRoot '.tools/cmake-4.4.4-windows-i386' }
$CMakeRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CMakeRoot)
$ctest = Join-Path $CMakeRoot 'bin/ctest.exe'
if (-not (Test-Path -LiteralPath $ctest)) {
    throw 'Pinned CTest is missing. Run tools/setup-toolchain.ps1 first.'
}
if ($Jobs -lt 1) { throw 'Jobs must be a positive number.' }

if (-not $NoBuild) {
    $buildParameters = @{
        Configuration = $Configuration
        Compiler = $Compiler
        DLL = $DLL
        Cheats = $Cheats
        WithoutResources = $true
        TestsOnly = $true
        ToolchainRoot = $ToolchainRoot
        CMakeRoot = $CMakeRoot
        Jobs = $Jobs
    }
    & (Join-Path $PSScriptRoot 'build.ps1') @buildParameters
}

$kind = if ($DLL) { 'dll' } else { 'exe' }
$buildName = '{0}-{1}-{2}' -f $Compiler.ToLowerInvariant(), $Configuration.ToLowerInvariant(), $kind
$buildRoot = Join-Path $repoRoot "build/$buildName"
$cache = Join-Path $buildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cache) -or
    -not (Select-String -LiteralPath $cache -Pattern '^BUILD_TESTING:BOOL=ON$' -Quiet)) {
    throw 'Tests are not configured. Run tools/test.ps1 without -NoBuild.'
}

$testBinaryRoot = Join-Path $buildRoot 'tests'
if ($Compiler -eq 'MSVC') { $testBinaryRoot = Join-Path $testBinaryRoot $Configuration }
if (Test-Path -LiteralPath (Join-Path $testBinaryRoot 'matrixgame_bomb_tests.exe')) {
    & (Join-Path $PSScriptRoot 'setup-directx-runtime.ps1')
    Copy-Item -LiteralPath (Join-Path $repoRoot '.tools/directx-x86/d3dx9_43.dll') -Destination $testBinaryRoot -Force
}

$ctestArgs = @(
    '--test-dir', $buildRoot, '-C', $Configuration,
    '--output-on-failure', '--no-tests=error',
    '--parallel', $Jobs, '-L', '^engine$',
    '--output-junit', (Join-Path $buildRoot 'test-results.xml')
)
if ($Filter) { $ctestArgs += @('-R', $Filter) }
& $ctest @ctestArgs
if ($LASTEXITCODE -ne 0) {
    throw "CTest failed (exit code $LASTEXITCODE). See $buildRoot/Testing/Temporary/LastTest.log."
}
Write-Output "Test report: $buildRoot/test-results.xml"
