# MatrixGame - licensed under GPLv2 or any later version.
# Inspect by default. Apply only to the disposable files of an active outcome playtest.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [Parameter(Mandatory = $true)][string]$SourceMapFile,
    [Parameter(Mandatory = $true)][ValidateSet('RestartHeader', 'MissingInterface')][string]$Fault,
    [ValidateSet('MinGW', 'MSVC')][string]$Compiler = 'MinGW',
    [string]$BuildDirectory = '',
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
if (-not $BuildDirectory) {
    $BuildDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) ('build/{0}-{1}-exe' -f $Compiler.ToLowerInvariant(), $Configuration.ToLowerInvariant())
}
$buildRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
$gameRoot = Join-Path $buildRoot 'MatrixGame'
if ($Compiler -eq 'MSVC') { $gameRoot = Join-Path $gameRoot $Configuration }
$configRoot = Join-Path $gameRoot 'CFG'
$backup = Join-Path $configRoot '.native-outcome-backup'
$manifest = Join-Path $backup 'state.json'
$target = [IO.Path]::GetFullPath((Join-Path $gameRoot 'Matrix/Map/native-outcome-alternate.cmap'))
$interface = Join-Path $configRoot 'robots/iface.txt'
$exe = Join-Path $gameRoot 'MatrixGame.exe'
$SourceMapFile = [IO.Path]::GetFullPath($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($SourceMapFile))
if (-not (Test-Path -LiteralPath $SourceMapFile -PathType Leaf) -or
    [IO.Path]::GetExtension($SourceMapFile) -ine '.cmap' -or
    [string]::Equals($SourceMapFile, $target, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'SourceMapFile must be an existing owned .cmap outside the disposable target.'
}
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { throw 'Start a staged tools/playtest-outcomes.ps1 run first.' }
$state = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if ($state.Version -ne 3 -or $state.AlternateMapStaged -isnot [bool] -or -not $state.AlternateMapStaged -or
    $state.InterfaceRemoved -isnot [bool] -or $state.InterfaceRemoved -or
    $state.SettingsPresent -isnot [bool] -or $state.PackedRemoved -isnot [bool] -or
    $state.MapDirectoryCreated -isnot [bool] -or $state.MatrixDirectoryCreated -isnot [bool]) {
    throw 'Expected a current alternate-map playtest backup with no fault already applied.'
}
foreach ($name in @('standalone.txt', 'robots.dat')) {
    $required = if ($name -eq 'standalone.txt') { $state.SettingsPresent } else { $state.PackedRemoved }
    if ($required -and -not (Test-Path -LiteralPath (Join-Path $backup $name) -PathType Leaf)) {
        throw "Incomplete playtest backup: $name"
    }
}
if (-not (Test-Path -LiteralPath $target -PathType Leaf)) { throw 'Disposable alternate map is missing.' }
if ((Get-Item -LiteralPath $target).Attributes -band [IO.FileAttributes]::ReparsePoint) {
    throw 'The disposable map must be a normal copied file.'
}
function Get-FaultSHA256([string]$Path) {
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
$sourceHash = Get-FaultSHA256 $SourceMapFile
if ((Get-FaultSHA256 $target) -ne $sourceHash) { throw 'Disposable map differs from SourceMapFile; no file was changed.' }
$reader = [IO.BinaryReader]::new([IO.File]::OpenRead($target))
try {
    if ($reader.BaseStream.Length -lt 12 -or $reader.ReadUInt32() -ne 0x47525453 -or $reader.ReadUInt32() -gt 1) {
        throw 'Expected a complete storage header in the unchanged map copy.'
    }
} finally { $reader.Dispose() }
if ($Fault -eq 'MissingInterface') {
    if ((Test-Path -LiteralPath (Join-Path $configRoot 'robots.dat')) -or
        @(Get-ChildItem -LiteralPath $configRoot -File | Where-Object { $_.Name -like 'robots.dat.*' }).Count) {
        throw 'MissingInterface requires the Text profile without packed configuration aliases.'
    }
    if (-not (Test-Path -LiteralPath $interface -PathType Leaf)) { throw 'The staged interface file is already absent; no fault was applied.' }
    if (Test-Path -LiteralPath (Join-Path $backup 'iface.txt')) { throw 'An interface backup already exists; recover the previous run first.' }
    Write-Output ('Original interface SHA256: ' + (Get-FaultSHA256 $interface))
}
Write-Output "Fault: $Fault"
Write-Output "Source map SHA256: $sourceHash"
Write-Output 'Target: Matrix/Map/native-outcome-alternate.cmap; supplied source remains unchanged.'
if (-not $Apply) { Write-Output 'Inspection only. Add -Apply after the required first battle is ready.'; return }
$running = @(Get-Process -Name MatrixGame -ErrorAction SilentlyContinue)
try {
    if ($running.Count -ne 1 -or -not [string]::Equals($running[0].Path, $exe, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Exactly one MatrixGame process from this staged executable must be running.'
    }
} finally { foreach ($item in $running) { $item.Dispose() } }
if ($Fault -eq 'RestartHeader') {
    # Replace the directory entry rather than editing an existing file in place:
    # even an unexpected hard link must not let this fault change the source map.
    $faultCopy = Join-Path $backup 'restart-header.tmp'
    $copyCreated = $false
    try {
        [IO.File]::Copy($target, $faultCopy, $false)
        $copyCreated = $true
        $stream = [IO.File]::Open($faultCopy, [IO.FileMode]::Open, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $stream.Write([byte[]]@(0, 0, 0, 0), 0, 4) } finally { $stream.Dispose() }
        # Preserve a null backup name under Windows PowerShell 5.1 string binding.
        [IO.File]::Replace($faultCopy, $target, [NullString]::Value)
    } finally {
        if ($copyCreated -and (Test-Path -LiteralPath $faultCopy)) { Remove-Item -LiteralPath $faultCopy }
    }
    Write-Output ('Faulted map SHA256: ' + (Get-FaultSHA256 $target))
    Write-Output 'Invalidated only the copied storage tag. Return to that loaded map and confirm Restart.'
} else {
    [IO.File]::Copy($interface, (Join-Path $backup 'iface.txt'), $false)
    # Record recovery before removal; a failed removal still leaves a complete backup.
    $state.InterfaceRemoved = $true
    $state | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8
    Remove-Item -LiteralPath $interface
    Write-Output 'Temporarily removed staged CFG/robots/iface.txt. Accept the copied map from the battle-menu selector.'
}
Write-Output 'Do not rebuild or start another game while the fault is active. The launch helper restores its staged files after exit.'
