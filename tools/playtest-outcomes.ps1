# MatrixGame - licensed under GPLv2 or any later version.
# Inspect by default. -Launch explicitly starts an interactive NV-05/NV-06 candidate.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [ValidateSet('MinGW', 'MSVC')][string]$Compiler = 'MinGW',
    [ValidateSet('Off', 'On')][string]$Cheats = 'Off',
    [ValidateSet('Packed', 'Text')][string]$ConfigurationMode = 'Text',
    [ValidateSet('ParkMiller', 'LegacyCRT')][string]$RandomGenerator = 'ParkMiller',
    [string]$BuildDirectory = '',
    [string]$AlternateMapFile = '',
    [ValidateSet('Ordinary', 'Demo')][string]$AlternateMapMode = 'Ordinary',
    [switch]$WatchRelaunches,
    [ValidateRange(1, 8)][int]$ExpectedProcesses = 1,
    [switch]$Launch,
    [switch]$Restore
)
$ErrorActionPreference = 'Stop'
if ($Launch -and $Restore) { throw '-Launch and -Restore cannot be combined.' }
if ($ExpectedProcesses -ne 1 -and -not $WatchRelaunches) { throw '-ExpectedProcesses requires -WatchRelaunches.' }
if ($AlternateMapMode -eq 'Demo' -and -not $AlternateMapFile -and -not $Restore) {
    throw '-AlternateMapMode Demo requires -AlternateMapFile.'
}
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
$interface = Join-Path $configRoot 'robots/iface.txt'
$backup = Join-Path $configRoot '.native-outcome-backup'
$manifest = Join-Path $backup 'state.json'
$exe = Join-Path $gameRoot 'MatrixGame.exe'
$matrixRoot = Join-Path $gameRoot 'Matrix'
$mapRoot = Join-Path $matrixRoot 'Map'
$alternateName = if ($AlternateMapMode -eq 'Demo') { 'native-outcome-demo' } else { 'native-outcome-alternate' }
$alternate = Join-Path $mapRoot ($alternateName + '.cmap')

function Wait-OutcomeProcesses($Processes) {
    $known = @{}
    $reported = @{}
    $first = $Processes[0]
    # Retaining the native handle is necessary to read ExitCode after termination.
    $null = $first.Handle
    $known[('{0}:{1}' -f $first.Id, $first.StartTime.ToUniversalTime().Ticks)] = $true
    $quiet = 0
    $incomplete = $false
    while ($quiet -lt 4) {
        foreach ($candidate in @(Get-Process -Name MatrixGame -ErrorAction SilentlyContinue)) {
            $retained = $false
            try {
                if (-not [string]::Equals($candidate.Path, $exe, [StringComparison]::OrdinalIgnoreCase)) { continue }
                $null = $candidate.Handle
                $key = '{0}:{1}' -f $candidate.Id, $candidate.StartTime.ToUniversalTime().Ticks
                if (-not $known.ContainsKey($key)) {
                    $known[$key] = $true
                    $Processes.Add($candidate)
                    $retained = $true
                    Write-Output "Observed process ID: $($candidate.Id)"
                }
            } catch {
                $incomplete = $true
                Write-Warning 'A process could not be retained; this run has incomplete process evidence.'
            } finally {
                if (-not $retained) { $candidate.Dispose() }
            }
        }
        $active = $false
        foreach ($item in $Processes) {
            if (-not $item.HasExited) { $active = $true; continue }
            if (-not $reported.ContainsKey($item)) {
                if (-not $item.WaitForExit(250)) { $active = $true; continue }
                Write-Output "Process ID: $($item.Id); exit code: $($item.ExitCode)"
                if ($null -eq $item.ExitCode -or $item.ExitCode -ne 0) { $incomplete = $true }
                $reported[$item] = $true
            }
        }
        if ($active) { $quiet = 0 } else { ++$quiet }
        Start-Sleep -Milliseconds 250
    }
    Write-Output "Observed processes: $($Processes.Count); expected: $ExpectedProcesses"
    if ($incomplete -or $Processes.Count -ne $ExpectedProcesses) {
        throw 'Process evidence is incomplete or includes a failure; do not record the relaunch case as passed.'
    }
    Write-Output 'Observed exits match the requested count. Confirm every required battle-readiness observation separately.'
}

function Get-OutcomeSHA256([string]$Path) {
    # Windows PowerShell launched by pwsh may not find Get-FileHash's script module.
    $stream = [IO.File]::OpenRead($Path)
    $sha = $null
    try {
        $sha = [Security.Cryptography.SHA256]::Create()
        return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '')
    } finally {
        $stream.Dispose()
        if ($sha) { $sha.Dispose() }
    }
}

function Restore-OutcomeConfiguration {
    if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw "No complete restoration record at $manifest" }
    $state = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
    if ($state.Version -notin @(1, 2, 3, 4) -or $state.SettingsPresent -isnot [bool] -or $state.PackedRemoved -isnot [bool] -or
        ($state.Version -ge 2 -and ($state.AlternateMapStaged -isnot [bool] -or
            $state.MapDirectoryCreated -isnot [bool] -or $state.MatrixDirectoryCreated -isnot [bool])) -or
        ($state.Version -ge 3 -and $state.InterfaceRemoved -isnot [bool]) -or
        ($state.Version -eq 4 -and $state.AlternateMapMode -notin @('Ordinary', 'Demo'))) {
        throw 'Invalid restoration record; inspect the retained backup manually.'
    }
    # Validate every required backup before touching the current configuration.
    foreach ($name in @('standalone.txt', 'robots.dat', 'iface.txt')) {
        $required = switch ($name) {
            'standalone.txt' { $state.SettingsPresent }
            'robots.dat' { $state.PackedRemoved }
            'iface.txt' { $state.Version -ge 3 -and $state.InterfaceRemoved }
        }
        if ($required -and -not (Test-Path -LiteralPath (Join-Path $backup $name) -PathType Leaf)) {
            throw "Incomplete backup: $name"
        }
    }
    if ($state.SettingsPresent) { Copy-Item -LiteralPath (Join-Path $backup 'standalone.txt') -Destination $settings -Force }
    elseif (Test-Path -LiteralPath $settings) { Remove-Item -LiteralPath $settings }
    if ($state.PackedRemoved) { Copy-Item -LiteralPath (Join-Path $backup 'robots.dat') -Destination $packed -Force }
    if ($state.Version -ge 3 -and $state.InterfaceRemoved) {
        Copy-Item -LiteralPath (Join-Path $backup 'iface.txt') -Destination $interface -Force
        Write-Output ('Restored interface SHA256: ' + (Get-OutcomeSHA256 $interface))
    }
    if ($state.Version -ge 2 -and $state.AlternateMapStaged) {
        # Restore by the recorded mode, even when -Restore uses its default parameters.
        $restoreName = if ($state.Version -eq 4 -and $state.AlternateMapMode -eq 'Demo') { 'native-outcome-demo.cmap' } else { 'native-outcome-alternate.cmap' }
        $restoreMap = Join-Path $mapRoot $restoreName
        if (Test-Path -LiteralPath $restoreMap) { Remove-Item -LiteralPath $restoreMap }
        if ($state.MapDirectoryCreated -and (Test-Path -LiteralPath $mapRoot) -and
            -not @(Get-ChildItem -LiteralPath $mapRoot -Force).Count) { Remove-Item -LiteralPath $mapRoot }
        if ($state.MatrixDirectoryCreated -and (Test-Path -LiteralPath $matrixRoot) -and
            -not @(Get-ChildItem -LiteralPath $matrixRoot -Force).Count) { Remove-Item -LiteralPath $matrixRoot }
    }
    $backupFiles = @('standalone.txt', 'robots.dat', 'state.json')
    if ($state.Version -ge 3 -and $state.InterfaceRemoved) { $backupFiles += 'iface.txt' }
    foreach ($name in $backupFiles) {
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
if ($AlternateMapFile) {
    $AlternateMapFile = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($AlternateMapFile)
    if (-not (Test-Path -LiteralPath $AlternateMapFile -PathType Leaf) -or
        [IO.Path]::GetExtension($AlternateMapFile) -ine '.cmap') { throw '-AlternateMapFile must identify an owned local .cmap file.' }
    if (Test-Path -LiteralPath $alternate) { throw 'The reserved alternate-map output already exists; choose another output directory or inspect it manually.' }
}
# The selector compares basenames, so a higher-priority loose map would hide either fixture.
# Check inspection as well as launch, before backups, staging or configuration changes.
$fixtureNames = @('desert1_3e.cmap')
if ($AlternateMapFile) { $fixtureNames += 'native-outcome-alternate.cmap' }
foreach ($folder in @('maps', 'DATA/maps')) {
    $directory = Join-Path $gameRoot $folder
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) { continue }
    foreach ($file in @(Get-ChildItem -LiteralPath $directory -File)) {
        if ($fixtureNames -icontains $file.Name) {
            throw "A higher-priority map shadows a required selector fixture: $($file.FullName). Move that output copy aside manually or use another build directory."
        }
    }
}
$cachePath = Join-Path $buildRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) { throw 'Build the selected candidate with tools/build.ps1 first.' }
$cache = Get-Content -LiteralPath $cachePath -Raw
$buildType = if ($Compiler -eq 'MSVC') { 'CMAKE_BUILD_TYPE:(?:STRING|UNINITIALIZED)=' } else { 'CMAKE_BUILD_TYPE:STRING=' }
if ($cache -notmatch ('(?m)^' + $buildType + [regex]::Escape($Configuration) + '\r?$')) {
    throw "Build configuration mismatch: expected CMAKE_BUILD_TYPE for $Compiler $Configuration"
}
foreach ($entry in @('MATRIXGAME_BUILD_DLL:BOOL=OFF',
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
Write-Output ('SHA256: ' + (Get-OutcomeSHA256 $exe))
Write-Output "Build cache: $Compiler $Configuration x86 EXE; cheats $Cheats"
Write-Output "Profile: $ConfigurationMode; RNG $RandomGenerator; Windowed 1280x720; bonus None"
Write-Output 'Map: Matrix\Map\desert1_3e.cmap (must be available in the local resource package)'
foreach ($name in @('sound.pkg', 'voices.pkg', 'music.pkg')) {
    Write-Output ("Optional audio {0}: {1}" -f $name, (Test-Path -LiteralPath (Join-Path $gameRoot "DATA/$name")))
}
Write-Output ('Sound mapping override: ' + (Test-Path -LiteralPath (Join-Path $configRoot 'sounds.txt')))
if ($AlternateMapFile) {
    Write-Output ('Alternate map SHA256: ' + (Get-OutcomeSHA256 $AlternateMapFile))
    Write-Output "Alternate selector entry: $alternateName (temporary local copy; original file unchanged)"
    if ($AlternateMapMode -eq 'Demo') {
        Write-Output 'Demo filename activates production automatic/full-auto/flying-camera startup; it does not provide controlled scene contents.'
    }
}
if ($WatchRelaunches) {
    Write-Output "Relaunch monitor: expected $ExpectedProcesses observed processes from this executable; verify battle readiness yourself."
}
if (-not $Launch) { Write-Output 'Inspection only. Add -Launch to stage temporary settings and open the game.'; return }
if (Get-Process -Name MatrixGame -ErrorAction SilentlyContinue) { throw 'Close all MatrixGame processes before starting this case.' }
$state = [ordered]@{
    Version = 4
    SettingsPresent = [bool](Test-Path -LiteralPath $settings -PathType Leaf)
    PackedRemoved = [bool]($ConfigurationMode -eq 'Text' -and (Test-Path -LiteralPath $packed -PathType Leaf))
    AlternateMapStaged = [bool]$AlternateMapFile
    MapDirectoryCreated = [bool](-not (Test-Path -LiteralPath $mapRoot))
    MatrixDirectoryCreated = [bool](-not (Test-Path -LiteralPath $matrixRoot))
    InterfaceRemoved = $false
    AlternateMapMode = $AlternateMapMode
}
New-Item -ItemType Directory -Path $backup | Out-Null
if ($state.SettingsPresent) { Copy-Item -LiteralPath $settings -Destination (Join-Path $backup 'standalone.txt') }
if ($state.PackedRemoved) { Copy-Item -LiteralPath $packed -Destination (Join-Path $backup 'robots.dat') }
$state | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8
$process = $null
$processes = [Collections.Generic.List[object]]::new()
try {
    if ($state.PackedRemoved) { Remove-Item -LiteralPath $packed }
    if ($AlternateMapFile) {
        New-Item -ItemType Directory -Path $mapRoot -Force | Out-Null
        Copy-Item -LiteralPath $AlternateMapFile -Destination $alternate
    }
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
    $processes.Add($process)
    Write-Output "Process ID: $($process.Id)"
    if ($WatchRelaunches) { Wait-OutcomeProcesses $processes }
    else {
        # Yield to PowerShell between bounded waits so Ctrl+C can enter finally.
        while (-not $process.WaitForExit(250)) { }
        Write-Output "Exit code: $($process.ExitCode)"
    }
} finally {
    $cleanupIncomplete = $false
    foreach ($item in $processes) {
        try {
            # Polling does not establish parentage. Only the process we launched is owned.
            if ([object]::ReferenceEquals($item, $process) -and -not $item.HasExited) {
                [void]$item.CloseMainWindow()
                if (-not $item.WaitForExit(5000)) {
                    $item.Kill()
                    if (-not $item.WaitForExit(5000)) {
                        $cleanupIncomplete = $true
                        Write-Warning 'The launched process did not exit after forced closure; backup retained.'
                    }
                    Write-Warning 'Interrupted run required forced closure; record it as aborted, not passed.'
                }
            }
            if (-not $item.HasExited) { $cleanupIncomplete = $true }
        } catch {
            $cleanupIncomplete = $true
            Write-Warning 'Process cleanup could not be verified; backup retained. Close the game and use -Restore.'
        } finally {
            $item.Dispose()
        }
    }
    if ($cleanupIncomplete -or (Get-Process -Name MatrixGame -ErrorAction SilentlyContinue)) {
        Write-Warning 'A MatrixGame process remains; backup retained. Close it and use -Restore before building or launching again.'
    } else { Restore-OutcomeConfiguration }
}
