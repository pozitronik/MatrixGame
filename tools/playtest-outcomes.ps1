# MatrixGame - licensed under GPLv2 or any later version.
# Inspect by default. -Launch explicitly starts the interactive NV-05-2/NV-06-1 candidate.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [ValidateSet('MinGW', 'MSVC')][string]$Compiler = 'MinGW',
    [ValidateSet('Off', 'On')][string]$Cheats = 'Off',
    [ValidateSet('Packed', 'Text')][string]$ConfigurationMode = 'Text',
    [ValidateSet('ParkMiller', 'LegacyCRT')][string]$RandomGenerator = 'ParkMiller',
    [string]$BuildDirectory = '',
    [switch]$Launch,
    [switch]$Restore
)
$ErrorActionPreference = 'Stop'
if ($Launch -and $Restore) { throw '-Launch and -Restore cannot be combined.' }
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path $repoRoot ('build/{0}-{1}-exe' -f $Compiler.ToLowerInvariant(), $Configuration.ToLowerInvariant())
}
$buildRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
$gameRoot = Join-Path $buildRoot 'MatrixGame'
if ($Compiler -eq 'MSVC') { $gameRoot = Join-Path $gameRoot $Configuration }
$configRoot = Join-Path $gameRoot 'CFG'
$settings = Join-Path $configRoot 'standalone.txt'
$packed = Join-Path $configRoot 'robots.dat'
$backup = Join-Path $configRoot '.native-outcome-backup'
$manifest = Join-Path $backup 'state.json'
$exe = Join-Path $gameRoot 'MatrixGame.exe'

function Restore-OutcomeConfiguration {
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "No complete restoration record at $manifest" }
    $state = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if ($state.Version -ne 1 -or $state.SettingsPresent -isnot [bool] -or $state.PackedRemoved -isnot [bool]) {
        throw 'Invalid restoration record; inspect the retained backup manually.'
    }
    # Validate every required backup before touching the current configuration.
    foreach ($name in @('standalone.txt', 'robots.dat')) {
        $required = if ($name -eq 'standalone.txt') { $state.SettingsPresent } else { $state.PackedRemoved }
        if ($required -and -not (Test-Path -LiteralPath (Join-Path $backup $name) -PathType Leaf)) {
            throw "Incomplete backup: $name"
        }
    }
    if ($state.SettingsPresent) { Copy-Item -LiteralPath (Join-Path $backup 'standalone.txt') -Destination $settings -Force }
    elseif (Test-Path -LiteralPath $settings) { Remove-Item -LiteralPath $settings }
    if ($state.PackedRemoved) { Copy-Item -LiteralPath (Join-Path $backup 'robots.dat') -Destination $packed -Force }
    foreach ($name in @('standalone.txt', 'robots.dat', 'state.json')) {
        $path = Join-Path $backup $name
        if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
    }
    # No recursive removal: unexpected files remain available for inspection.
    if (@(Get-ChildItem -LiteralPath $backup -Force).Count) { throw "Unexpected files remain in $backup; inspect them manually." }
    Remove-Item -LiteralPath $backup
    Write-Output 'Restored the staged configuration; supplied source resources were unchanged.'
}

if ($Restore) {
    if (Get-Process -Name MatrixGame -ErrorAction SilentlyContinue) { throw 'Close MatrixGame before restoring configuration.' }
    Restore-OutcomeConfiguration
    return
}
if (Test-Path -LiteralPath $backup) { throw 'A previous backup remains. Close the game and run this command with -Restore.' }
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) { throw 'Build the selected candidate with tools/build.ps1 first.' }
$cache = Get-Content -LiteralPath $cachePath -Raw
foreach ($entry in @("CMAKE_BUILD_TYPE:STRING=$Configuration", 'MATRIXGAME_BUILD_DLL:BOOL=OFF',
    ('MATRIXGAME_CHEATS:BOOL=' + $(if ($Cheats -eq 'On') { 'ON' } else { 'OFF' })))) {
    if ($cache -notmatch ('(?m)^' + [regex]::Escape($entry) + '\r?$')) { throw "Build configuration mismatch: expected $entry" }
}
foreach ($file in @($exe, (Join-Path $gameRoot 'DATA/robots.pkg'),
    (Join-Path $configRoot 'robots/data.txt'), (Join-Path $configRoot 'robots/iface.txt'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required candidate file missing: $file" }
}
$reader = [IO.BinaryReader]::new([IO.File]::OpenRead($exe))
try {
    if ($reader.BaseStream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5a4d) { throw 'The executable has no DOS header.' }
    $reader.BaseStream.Position = 60
    $pe = $reader.ReadUInt32()
    if ($pe -gt $reader.BaseStream.Length - 6) { throw 'Invalid PE header offset.' }
    $reader.BaseStream.Position = $pe
    if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x14c) { throw 'Expected a Windows x86 executable.' }
} finally { $reader.Dispose() }
if ($ConfigurationMode -eq 'Packed' -and -not (Test-Path -LiteralPath $packed -PathType Leaf)) {
    throw 'Packed profile requires the staged CFG/robots.dat. Supply it locally and rebuild.'
}
if ($ConfigurationMode -eq 'Text') {
    $aliases = @(Get-ChildItem -LiteralPath $configRoot -File | Where-Object { $_.Name -like 'robots.dat.*' })
    if ($aliases.Count) { throw 'CFG/robots.dat.* aliases would defeat text fallback; move those output copies aside first.' }
}
Write-Output "Executable: $exe"
Write-Output ('SHA256: ' + (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash)
Write-Output "Build cache: $Compiler $Configuration x86 EXE; cheats $Cheats"
Write-Output "Profile: $ConfigurationMode; RNG $RandomGenerator; Windowed 1280x720; bonus None"
Write-Output 'Map: Matrix\Map\desert1_3e.cmap (must be available in the local resource package)'
foreach ($name in @('sound.pkg', 'voices.pkg', 'music.pkg')) {
    Write-Output ("Optional audio {0}: {1}" -f $name, (Test-Path -LiteralPath (Join-Path $gameRoot "DATA/$name")))
}
Write-Output ('Sound mapping override: ' + (Test-Path -LiteralPath (Join-Path $configRoot 'sounds.txt')))
if (-not $Launch) { Write-Output 'Inspection only. Add -Launch to stage temporary settings and open the game.'; return }
if (Get-Process -Name MatrixGame -ErrorAction SilentlyContinue) { throw 'Close all MatrixGame processes before starting this case.' }
$state = [ordered]@{
    Version = 1
    SettingsPresent = [bool](Test-Path -LiteralPath $settings -PathType Leaf)
    PackedRemoved = [bool]($ConfigurationMode -eq 'Text' -and (Test-Path -LiteralPath $packed -PathType Leaf))
}
New-Item -ItemType Directory -Path $backup | Out-Null
if ($state.SettingsPresent) { Copy-Item -LiteralPath $settings -Destination (Join-Path $backup 'standalone.txt') }
if ($state.PackedRemoved) { Copy-Item -LiteralPath $packed -Destination (Join-Path $backup 'robots.dat') }
$state | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8
$process = $null
try {
    if ($state.PackedRemoved) { Remove-Item -LiteralPath $packed }
    # Replace only the staged options for this run. The original bytes are restored in finally.
    $text = @"
RandomGenerator=$RandomGenerator
ResolutionWidth=1280
ResolutionHeight=720
WindowMode=Windowed
FullscreenMode=Borderless
SoundVolume=100
MusicVolume=100
StencilShadows=On
ProjectedShadows=On
Skybox=High
RobotShadows=On
CameraSpeed=100
DelayedDamage=Corrected
GroupCounts=Corrected
PlaceContainment=Corrected
ZeroXTrace=Corrected
Map=Matrix\Map\desert1_3e.cmap
BattleBonus=None
"@
    [IO.File]::WriteAllText($settings, $text, [Text.UTF8Encoding]::new($false))
    # Deliberately no map argument: positional map arguments select CalcVis, not play.
    $process = Start-Process -FilePath $exe -WorkingDirectory $gameRoot -PassThru
    Write-Output "Process ID: $($process.Id)"
    $process.WaitForExit()
    Write-Output "Exit code: $($process.ExitCode)"
} finally {
    if ($process -and -not $process.HasExited) {
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) {
            $process.Kill()
            $process.WaitForExit()
            Write-Warning 'Interrupted run required forced closure; record it as aborted, not passed.'
        }
    }
    if ($process) { $process.Dispose() }
    if (Get-Process -Name MatrixGame -ErrorAction SilentlyContinue) {
        Write-Warning 'A MatrixGame process remains; backup retained. Close it and use -Restore before building or launching again.'
    } else { Restore-OutcomeConfiguration }
}
