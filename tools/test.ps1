# MatrixGame - licensed under GPLv2 or any later version.
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('MinGW', 'MSVC')][string]$Compiler = 'MinGW',
    [switch]$DLL,
    [switch]$Cheats,
    [switch]$NoBuild,
    [switch]$Coverage,
    [switch]$WithoutMusic,
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
if ($Coverage -and $Compiler -ne 'MinGW') { throw 'Coverage requires the pinned MinGW toolchain and its gcov.' }

if (-not $NoBuild) {
    $buildParameters = @{
        Configuration = $Configuration
        Compiler = $Compiler
        DLL = $DLL
        Cheats = $Cheats
        WithoutResources = $true
        TestsOnly = $true
        Coverage = $Coverage
        WithoutMusic = $WithoutMusic
        ToolchainRoot = $ToolchainRoot
        CMakeRoot = $CMakeRoot
        Jobs = $Jobs
    }
    & (Join-Path $PSScriptRoot 'build.ps1') @buildParameters
}

$kind = if ($DLL) { 'dll' } else { 'exe' }
$buildName = '{0}-{1}-{2}' -f $Compiler.ToLowerInvariant(), $Configuration.ToLowerInvariant(), $kind
if ($Coverage) { $buildName += '-coverage' }
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

if ($Coverage) {
    if (-not (Select-String -LiteralPath $cache -Pattern '^MATRIXGAME_COVERAGE:BOOL=ON$' -Quiet)) {
        throw 'Coverage is not configured. Run tools/test.ps1 -Coverage without -NoBuild.'
    }
    # Start from empty counters so the report describes only this test selection.
    Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter '*.gcda' -File | Remove-Item
}

# Instrumented processes merge shared counter files, so coverage runs one test at a time.
$testJobs = if ($Coverage) { 1 } else { $Jobs }
$ctestArgs = @(
    '--test-dir', $buildRoot, '-C', $Configuration,
    '--output-on-failure', '--no-tests=error',
    '--parallel', $testJobs, '-L', '^engine$',
    '--output-junit', (Join-Path $buildRoot 'test-results.xml')
)
if ($Filter) { $ctestArgs += @('-R', $Filter) }
# Coverage describes verified contracts; the benchmark smoke pass only checks repeatable work.
if ($Coverage) { $ctestArgs += @('-LE', '^benchmark$') }
& $ctest @ctestArgs
if ($LASTEXITCODE -ne 0) {
    throw "CTest failed (exit code $LASTEXITCODE). See $buildRoot/Testing/Temporary/LastTest.log."
}
Write-Output "Test report: $buildRoot/test-results.xml"

if ($Coverage) {
    # Prefer the Python launcher; skip the Microsoft Store alias, which may open the Store.
    $python = $null
    $candidates = @()
    $launcher = Get-Command py -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($launcher) { $candidates += , @($launcher.Source, '-3') }
    foreach ($command in @(Get-Command python -CommandType Application -All -ErrorAction SilentlyContinue)) {
        if ($command.Source -notmatch '\\WindowsApps\\') { $candidates += , @($command.Source) }
    }
    foreach ($candidate in $candidates) {
        $prefix = @($candidate | Select-Object -Skip 1)
        & $candidate[0] @prefix -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)' 2>$null
        if ($LASTEXITCODE -eq 0) { $python = $candidate; break }
    }
    if (-not $python) { throw 'Python 3.9 or newer is required to summarize coverage.' }
    $reportArgs = @(@($python | Select-Object -Skip 1) + @(
        '-B', (Join-Path $PSScriptRoot 'coverage_report.py'),
        '--build-dir', $buildRoot,
        '--gcov', (Join-Path $ToolchainRoot 'bin/gcov.exe'),
        '--compiler', (Join-Path $ToolchainRoot 'bin/g++.exe')
    ))
    if ($Filter) { $reportArgs += @('--test-filter', $Filter) }
    $reportArgs += @('--excluded-label', 'benchmark')
    & $python[0] @reportArgs
    if ($LASTEXITCODE -ne 0) { throw "Coverage summary failed (exit code $LASTEXITCODE)." }
}
