# Presentation procedure handoff

Procedure version: **2**.

This supplement prepares P1-P4 / PRES-01 through PRES-04 for [NV-01 through NV-04](native-validation.md) under [#191](https://github.com/pozitronik/MatrixGame/issues/191). It supplies launch/exit recording and the scenario details missing from [the presentation procedure](87-presentation.md#native-procedures); it records no executed native case. Production-linked CPU tests establish their documented input, state, resource and audio-event contracts, while real GPU output, OS focus delivery and audible playback require separate observations. Unperformed, unavailable and failed cases remain separate pending evidence.

## Prepare and bound each record

Use [Windows builds](../BUILD_WINDOWS.md) for the pinned x86 EXE, runtime and locally supplied resources. Build Debug and Release with cheats disabled before changing output copies; a later build stages configuration again. Run P1-P4 in Debug/Packed, Debug/Text, Release/Packed and Release/Text. Use the [configuration matrix](../PLAYTESTING.md#prepare-the-configuration-cases) to select Packed or Text, including removal of output `robots.dat.*` aliases that would defeat Text fallback. Keep `CFG/standalone.txt` separate from game configuration. Preserve its bytes, attributes and the selected output resources before each case; never modify the supplied root-level packages or an installed game.

```powershell
. {
$ErrorActionPreference = 'Stop'
.\tools\build.ps1 -Configuration Debug
if ($LASTEXITCODE -ne 0) { throw 'Debug build failed.' }
.\tools\build.ps1 -Configuration Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
.\tools\setup-directx-runtime.ps1
if ($LASTEXITCODE -ne 0) { throw 'Runtime preparation failed.' }
# Select one already-built profile; use Release for the other build.
$configuration = 'Debug'
$game = (Resolve-Path ('.\build\mingw-{0}-exe\MatrixGame' -f $configuration.ToLowerInvariant())).Path
Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination $game -ErrorAction Stop
$exe = Join-Path $game 'MatrixGame.exe'
Get-FileHash -LiteralPath $exe -Algorithm SHA256
}
```

Check each helper's exit status before continuing. If `DATA/robots.pkg`, the selected Packed configuration, a required map or a usable device is unavailable, name that requirement and leave the affected case unperformed. Record this procedure's path/version, source/binary identity, compiler, assertions/cheats, configuration/resource hashes, map, bonus/difficulty, RNG, sound mapping, packages, resolution, scaling, adapter and driver using [the presentation evidence record](87-presentation.md#evidence-record). Audio availability changes RNG consumption; compare each audio profile only with its matching reference.

Before each record, close the standalone game and other games sharing its Documents output directory. Keep them closed until restoration completes. Prepare settings and cache protection before any fault substitution or launch. For a new map, use the [Debug key-qualification procedure](87-cache-qualification.md) first; it copies both possible cache trees before allowing the qualification launch. Existing-key records park only that key at both possible locations. Neither route waives native observations.

Run each fenced block whole, from `. {` through its closing `}`, and wait for its result before the next block. Dot-sourcing keeps variables in the current PowerShell session while making the block one statement: a thrown error cannot leave later pasted lines running as independent commands. Do not paste an entire procedure across its observation or input prompts. Each failed block stops that step; do not bypass a ready/ownership guard or reset state to continue.

Resolve the paths after choosing the build, or after choosing an extracted archive. The output directory must be separate from either cache tree so copying a cache cannot recursively include its own backup. The fallback is relative to the game's working directory after `WinMain` changes it, not to the launcher's directory.

```powershell
. {
    $ErrorActionPreference = 'Stop'
    $game = (Resolve-Path -LiteralPath $game).Path
    $exe = Join-Path $game 'MatrixGame.exe'
    $launchDirectory = $game
    $documents = [Environment]::GetFolderPath('MyDocuments')
    if (-not $documents) { throw 'Resolve the actual Documents location before qualification.' }
    $cacheRoots = @(
        [pscustomobject]@{Name='Documents'; Path=[IO.Path]::GetFullPath((Join-Path $documents 'SpaceRangersHD/Cache')).TrimEnd([char[]]'\/')},
        [pscustomobject]@{Name='Fallback'; Path=[IO.Path]::GetFullPath((Join-Path $game 'Cache')).TrimEnd([char[]]'\/')}
    )
    if ($cacheRoots[0].Path -ieq $cacheRoots[1].Path) { throw 'Use an isolated game output directory.' }
    foreach ($root in $cacheRoots) {
        if ($game -ieq $root.Path -or $game.StartsWith($root.Path + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) { throw 'Game output must be outside the cache trees.' }
    }
}
```

Use one process at a time and only one launch attempt per prepared record. A failed or uncertain start still consumes that attempt: resolve it and restore the record before preparing another; do not retry this block or clear its flags. For an ordinary visible launch, retain the process object in PowerShell:

```powershell
. {
if ($launchAttempted) { throw 'One launch attempt per prepared record; resolve and restore it before preparing another.' }
if (-not $settingsReady -or ($cacheReady -eq $cacheQualificationReady)) { throw 'Require exactly one prepared cache route.' }
if ($cacheQualificationReady -and $configuration -ne 'Debug') { throw 'Cache qualification requires Debug.' }
if ($soundOverrideActive -and -not $soundOverrideReady) { throw 'Resolve the unfinished sound-override preparation.' }
if ($faultActive -and -not $faultReady) { throw 'Resolve the unfinished fault preparation.' }
if ($process -and -not $process.HasExited) { throw 'The previous owned process is still running.' }
if (Get-Process -Name MatrixGame -ErrorAction SilentlyContinue) { throw 'Close all MatrixGame processes before starting this record.' }
$currentGame = (Resolve-Path -LiteralPath $game).Path
if ([IO.Path]::GetFullPath($exe) -ine (Join-Path $currentGame 'MatrixGame.exe')) { throw 'Run path setup for the current game before preparing this record.' }
$fallbackRoots = @($cacheRoots | Where-Object Name -eq 'Fallback')
if ($fallbackRoots.Count -ne 1 -or $fallbackRoots[0].Path -ine (Join-Path $currentGame 'Cache')) { throw 'The fallback cache does not belong to this game; run path setup again.' }
if ($settings -ine (Join-Path $currentGame 'CFG/standalone.txt') -or $caseBackup -ine (Join-Path $currentGame '.playtest-presentation-backup')) { throw 'The protected settings record belongs to another game.' }
$launchDirectory = (Resolve-Path -LiteralPath $launchDirectory).Path
$logPath = Join-Path $launchDirectory 'test.log'
$logBeforeLength = if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    (Get-Item -LiteralPath $logPath -ErrorAction Stop).Length
} else { 0L }
$newLog = $null
$newLogProcessId = $null
$gameExitCode = $null
$process = $null
$launchSucceeded = $false
$launchAttempted = $true
$process = Start-Process -FilePath $exe -WorkingDirectory $launchDirectory -PassThru -ErrorAction Stop
if ($null -eq $process) { throw 'No process handle; resolve the launch before restoration.' }
$null = $process.Handle
$launchSucceeded = $true
$process.Id
}
```

Perform one case, then initiate its specified quit route or dismiss its expected error dialog. Only then run this separate exit block:

```powershell
. {
if (-not $launchSucceeded -or $null -eq $process) { throw 'No verified launch; exit evidence is incomplete.' }
$newLog = $null
$newLogProcessId = $null
$gameExitCode = $null
if (-not $process.WaitForExit(30000)) { throw 'Exit timed out; retain the PID and diagnose before restoring files.' }
$process.Refresh()
$observedGameExitCode = $process.ExitCode
if ($null -eq $observedGameExitCode) { throw 'The native exit code is unavailable.' }
if (-not (Test-Path -LiteralPath $logPath -PathType Leaf)) { throw 'The owned launch has no log; evidence is incomplete.' }
if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    $stream = [IO.File]::OpenRead($logPath)
    $reader = $null
    try {
        if ($stream.Length -lt $logBeforeLength) { throw 'The log was truncated/replaced; preserve the evidence.' }
        $null = $stream.Seek($logBeforeLength, [IO.SeekOrigin]::Begin)
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8)
        $newLog = $reader.ReadToEnd()
    } finally {
        if ($reader) { $reader.Dispose() } else { $stream.Dispose() }
    }
}
$gameExitCode = $observedGameExitCode
$newLogProcessId = $process.Id
$gameExitCode
$newLog
}
```

Retain the native handle immediately so exit status remains available after termination, as in the public outcome relaunch monitor. A missing exit code is incomplete evidence. Dispose the retained process object after recording its exit and completing restoration.

Allow 60 seconds from launch for the startup menu and 60 seconds after accepting the map for the introduction, matching the [ordinary outcome procedure](85-native-outcomes.md#common-launch-and-scene). Allow 30 seconds for an expected error dialog, each UI transition or short movement/attack observation, and exit after initiating quit/dismissing the dialog. Unless the linked procedure supplies a different bound, observe a sustained state for five seconds. Carrier arrival uses the existing 90-second bound. An unavailable scene is unperformed; an accepted action that exceeds its declared bound is a failed observation. Record the last visible state and a minimal diagnostic/log excerpt. Do not extend a deadline until the case appears to pass.

The commands above measure exit after the operator completes the case; they do not automate the observation deadlines or prove the observations. A timeout can leave the process alive: close the owned window and confirm termination before changing/restoring files. A forced termination is an aborted case, never exit-code evidence for a normal quit. These cases do not select another battle from the map selector; that can create another process and needs the separate [outcome relaunch procedure](85-native-outcomes.md).

After every record, verify no owned game process remains and other games sharing the output directory are closed, then restore the settings, selected cache entry and faulted output files below. Remove only files whose creation this record owns. Restore after a failure too; retain backups if a running process, uncertain failed launch or interrupted cleanup prevents restoration. Prepare a separate record for the fresh recovery launch: start an ordinary battle, issue a short Move, quit with exit 0 and restore that launch's settings/cache too. Recovery does not replace the failed record.

## Preserve settings for each record

Run this preparation after choosing `$game` and before any launch or fault substitution. Use a new backup directory for every record; retain it until all restoration checks pass. `Copy-Item` preserves the original settings in place for the launch, and the hash check must complete before changing them. Keep the recorded variables in the same PowerShell session. A missing settings file is an original state to preserve too.

```powershell
. {
$ErrorActionPreference = 'Stop'
$nextCaseBackup = Join-Path $game '.playtest-presentation-backup'
if (Test-Path -LiteralPath $nextCaseBackup) { throw 'Reconcile the previous record before proceeding.' }
if ($process -and -not $process.HasExited) { throw 'The previous owned process is still running.' }
if ($settingsReady -or $cacheActive -or $cacheQualificationActive -or $faultActive) {
    throw 'Restore the preceding record before preparing another.'
}
$process = $null
$launchAttempted = $false
$launchSucceeded = $false
$settingsReady = $false
$cacheReady = $false
$cacheActive = $false
$cacheQualificationReady = $false
$cacheQualificationActive = $false
$faultReady = $false
$faultActive = $false
$caseBackup = $nextCaseBackup
New-Item -ItemType Directory -Path $caseBackup -ErrorAction Stop | Out-Null
$caseBackupOwned = $true
$settings = Join-Path $game 'CFG/standalone.txt'
$savedSettings = Join-Path $caseBackup 'standalone.txt'
$settingsPresent = Test-Path -LiteralPath $settings -PathType Leaf
if ((Test-Path -LiteralPath $settings) -and -not $settingsPresent) { throw 'Settings path is not a regular file.' }
if ($settingsPresent -and ([IO.File]::GetAttributes($settings) -band [IO.FileAttributes]::ReparsePoint)) {
    throw 'Settings links need separate preparation.'
}
$settingsHash = $null
$settingsAttributes = $null
if ($settingsPresent) {
    $settingsHash = (Get-FileHash -LiteralPath $settings -Algorithm SHA256 -ErrorAction Stop).Hash
    $settingsAttributes = [IO.File]::GetAttributes($settings)
    Copy-Item -LiteralPath $settings -Destination $savedSettings -ErrorAction Stop
    if ((Get-FileHash -LiteralPath $savedSettings -Algorithm SHA256 -ErrorAction Stop).Hash -ne $settingsHash) {
        throw 'Settings backup differs; stop without editing the original.'
    }
}
$settingsReady = $true
}
```

After the case has exited, restore with the following separate block. It also permits restoration when no launch was attempted, without dereferencing a null process. If launch was attempted but its success or process termination is uncertain, stop: identify and close any owned process before manually resuming restoration. Never clear the launch state merely to bypass that check.

```powershell
. {
if (-not $settingsReady) { throw 'No verified settings record; inspect the retained files.' }
if ($launchAttempted -and (-not $launchSucceeded -or $null -eq $process -or -not $process.HasExited)) {
    throw 'Resolve the owned launch before restoration.'
}
if ($settingsPresent -and (Get-FileHash -LiteralPath $savedSettings -Algorithm SHA256 -ErrorAction Stop).Hash -ne $settingsHash) {
    throw 'Settings backup changed; preserve it for inspection.'
}
if (Test-Path -LiteralPath $settings) {
    if (-not (Test-Path -LiteralPath $settings -PathType Leaf)) { throw 'Unexpected settings directory; stop.' }
    Remove-Item -LiteralPath $settings -Force -ErrorAction Stop
}
if ($settingsPresent) {
    Copy-Item -LiteralPath $savedSettings -Destination $settings -ErrorAction Stop
    [IO.File]::SetAttributes($settings, $settingsAttributes)
    if ((Get-FileHash -LiteralPath $settings -Algorithm SHA256 -ErrorAction Stop).Hash -ne $settingsHash -or
        [IO.File]::GetAttributes($settings) -ne $settingsAttributes) { throw 'Settings restoration differs.' }
} elseif (Test-Path -LiteralPath $settings) { throw 'Original settings absence was not restored.' }
$settingsReady = $false
}
```

## Preserve the selected minimap cache

The standalone map loader also writes outside the build output. [Map preparation](../../MatrixGame/src/MatrixMapPrepare.cpp) passes its selected map name and `DATA_UNIQID` to [the minimap](../../MatrixGame/src/MatrixMinimap.cpp), whose key retains the map extension and appends `.` plus uppercase, unpadded hexadecimal uniqueness. The default is `0` only when that map omits `DATA_UNIQID`. [Output-path resolution](../../MatrixGame/src/MatrixGamePathUtils.cpp) normally uses the user's Documents folder under `SpaceRangersHD/Cache`, shared with the original game. An existing cache can supply the minimap without rendering a fresh background. Follow the same ownership boundary as [the special-scene cache procedure](85-native-special-scenes.md#preserve-and-restore-the-minimap-cache).

For a first record, use [Debug key qualification](87-cache-qualification.md), which copies both possible cache trees before its separate launch and restores only the reported entry. For later records, use the confirmed key from that record and the same map/resource identity. Debug's `Found`/`Storing` messages identify it; Release suppresses those messages. Do not assume `.0`, add `.png`, flatten an unfamiliar path or select a wildcard. The ordinary recipe below accepts a confirmed leaf key. Unusual map paths, unavailable output access or failed qualification remain explicit preparation gaps.

Close all games sharing these output locations and keep them closed through restoration. Supply the confirmed key in an array before preparation. For another procedure that can load several maps in one owned journey, replace the input line with its complete, already confirmed key array; do not park a wildcard. The common block validates every key and protects each at both Documents/fallback roots. Other procedures may reuse these blocks and their settings context; their retained-process/exit proof must satisfy the same restoration boundary.

```powershell
. {
    $cacheNames = @((Read-Host 'Confirmed exact map cache filename, including map extension and hexadecimal suffix'))
}
```

```powershell
. {
if (-not $settingsReady -or $cacheActive -or $cacheQualificationActive) { throw 'Complete or restore the preceding preparation first.' }
$nextCacheBackup = Join-Path $caseBackup 'selected-minimap-cache'
if (Test-Path -LiteralPath $nextCacheBackup) { throw 'Existing cache backup must be reconciled first.' }
if (-not $cacheNames -or @($cacheNames).Count -eq 0) { throw 'Require the confirmed key array.' }
$uniqueKeys = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($name in $cacheNames) {
    if (-not $name -or [IO.Path]::GetFileName($name) -ne $name -or
        $name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0 -or
        $name -cnotmatch '\.(0|[1-9A-F][0-9A-F]{0,7})$' -or -not $uniqueKeys.Add($name)) {
        throw 'Require distinct confirmed leaf keys; do not infer a filename.'
    }
}
$validatedCacheEntries = [Collections.Generic.List[object]]::new()
foreach ($root in $cacheRoots) {
    if (Test-Path -LiteralPath $root.Path) {
        $rootItem = Get-Item -LiteralPath $root.Path -Force -ErrorAction Stop
        if (-not $rootItem.PSIsContainer -or ($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Cache root links or non-directories need separate preparation.'
        }
    }
    foreach ($name in $cacheNames) {
        $cached = Join-Path $root.Path $name
        $entry = [pscustomobject]@{Path=$cached; Saved=(Join-Path $nextCacheBackup ($root.Name + '-' + $validatedCacheEntries.Count))
            Present=(Test-Path -LiteralPath $cached -PathType Leaf); Hash=$null; Attributes=$null; Parked=$false}
        if ((Test-Path -LiteralPath $cached) -and -not $entry.Present) { throw 'Cache entry is not a regular file.' }
        if ($entry.Present) {
            $entry.Attributes = [IO.File]::GetAttributes($cached)
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Cache links need separate preparation.' }
            $entry.Hash = (Get-FileHash -LiteralPath $cached -Algorithm SHA256 -ErrorAction Stop).Hash
        }
        $validatedCacheEntries.Add($entry)
    }
}
$cacheEntries = $validatedCacheEntries
$cacheReady = $false
$cacheActive = $true
New-Item -ItemType Directory -Path $nextCacheBackup -ErrorAction Stop | Out-Null
foreach ($entry in $cacheEntries) {
    if ($entry.Present) {
        Move-Item -LiteralPath $entry.Path -Destination $entry.Saved -ErrorAction Stop
        $entry.Parked = $true
        if ((Get-FileHash -LiteralPath $entry.Saved -Algorithm SHA256 -ErrorAction Stop).Hash -ne $entry.Hash) {
            throw 'Parked cache differs; preserve it and stop.'
        }
    }
    if (Test-Path -LiteralPath $entry.Path) { throw 'Cache was not parked; do not launch.' }
}
$cacheReady = $true
}
```

After exit, record which protected entry now exists. A case that reached its battle must have created the confirmed entry at one of these roots; Release needs no debug log message for that check. A missing entry or warning is a failed/unresolved output observation, not a reason to skip restoration. Early startup-error cases need no cache creation. If another game ran or ownership is uncertain, stop with backups retained. The block restores only the selected key at both roots, even after a failed observation; a no-launch preparation refuses an entry that appeared unexpectedly. Leave shared directories, unrelated caches and screenshots untouched.

```powershell
. {
if (-not $cacheActive -or -not $cacheEntries -or $cacheEntries.Count -eq 0) { throw 'No complete cache ownership record; leave its files unchanged.' }
if ($launchAttempted -and (-not $cacheReady -or -not $launchSucceeded -or $null -eq $process -or -not $process.HasExited)) {
    throw 'Resolve the owned launch before restoring cache.'
}
$createdCachePaths = if ($launchAttempted) { @($cacheEntries | Where-Object { Test-Path -LiteralPath $_.Path -PathType Leaf } | ForEach-Object Path) } else { @() }
$createdCachePaths
foreach ($entry in $cacheEntries) {
    if ($entry.Present -and -not $entry.Parked) {
        if ($launchAttempted -or -not (Test-Path -LiteralPath $entry.Path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256 -ErrorAction Stop).Hash -ne $entry.Hash -or
            [IO.File]::GetAttributes($entry.Path) -ne $entry.Attributes) { throw 'An unparked original differs; preserve the retained state.' }
        continue
    }
    if ($entry.Present -and
        (Get-FileHash -LiteralPath $entry.Saved -Algorithm SHA256 -ErrorAction Stop).Hash -ne $entry.Hash) {
        throw 'Verified original cache backup is unavailable.'
    }
    if (Test-Path -LiteralPath $entry.Path) {
        if (-not (Test-Path -LiteralPath $entry.Path -PathType Leaf) -or
            ([IO.File]::GetAttributes($entry.Path) -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unexpected cache entry type.' }
        if (-not $launchAttempted) {
            if (-not $entry.Present -or
                (Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256 -ErrorAction Stop).Hash -ne $entry.Hash) {
                throw 'No launch owns the unexpected cache entry.'
            }
        } else { Remove-Item -LiteralPath $entry.Path -Force -ErrorAction Stop }
    }
    if ($entry.Present) {
        if (-not (Test-Path -LiteralPath $entry.Path)) { [IO.File]::Copy($entry.Saved, $entry.Path, $false) }
        [IO.File]::SetAttributes($entry.Path, $entry.Attributes)
        if ((Get-FileHash -LiteralPath $entry.Path -Algorithm SHA256 -ErrorAction Stop).Hash -ne $entry.Hash -or
            [IO.File]::GetAttributes($entry.Path) -ne $entry.Attributes) { throw 'Cache restoration differs.' }
    } elseif (Test-Path -LiteralPath $entry.Path) { throw 'Original cache absence was not restored.' }
}
$cacheReady = $false
$cacheActive = $false
}
```

All roots and entries are validated and hashed before parking becomes active. If validation fails before `$cacheActive`, no original moved: restore the prepared settings and use guarded cleanup. If parking fails after activation but no launch was attempted, run the same cache-restoration block: it returns each verified parked original and verifies every unparked original in place, including its attributes. It never deletes a no-launch entry that does not match the recorded original. Keep the backup if those checks fail; do not clear active flags manually.

Keep the per-record backup until settings, cache and any faulted files are restored. Use [guarded cleanup](#complete-a-record) after retaining the evidence and the separately stored qualified-key JSON. Do not upload cached images or private settings. Unknown keys, shared-game activity and unsafe filesystem states remain named gaps requiring inspection.

## Complete a record

Retain its metadata, log excerpt and any qualified key in the native evidence record first. Then remove only the verified owned backup. Active flags include partial preparations, not just ready states: an error after parking an original must never permit deleting its backup. The sound override has a separate P3-series lifetime outside this directory. If a guard fails, keep the backup and recover the recorded originals; do not force the flag false. Shared cache folders and unrelated screenshots remain untouched.

```powershell
. {
    if ($settingsReady -or $cacheActive -or $cacheQualificationActive -or $faultActive) { throw 'Restoration is incomplete.' }
    if ($launchAttempted -and (-not $launchSucceeded -or $null -eq $process -or -not $process.HasExited)) {
        throw 'Resolve the owned launch before cleanup.'
    }
    $expectedBackup = [IO.Path]::GetFullPath((Join-Path $game '.playtest-presentation-backup'))
    $ownedBackup = Get-Item -LiteralPath $caseBackup -Force -ErrorAction Stop
    if (-not $caseBackupOwned -or $ownedBackup.FullName -ine $expectedBackup -or
        -not $ownedBackup.PSIsContainer -or ($ownedBackup.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Backup ownership/path is unverified.'
    }
    Remove-Item -LiteralPath $ownedBackup.FullName -Recurse -Force -ErrorAction Stop
    $caseBackupOwned = $false
    if ($process) { $process.Dispose() }
    $process = $null
    $launchAttempted = $false
    $launchSucceeded = $false
}
```

## P1 / NV-01: reuse input, viewport and focus procedures

Execute [native steps 1-4](87-presentation.md#native-procedures), [selection dragging](../PLAYTESTING.md#selection-dragging), [focus and recovery](../PLAYTESTING.md#focus-and-recovery), and [display verification](../DISPLAY.md#verification). Their exact input sequences cover selection membership, minimap/picking, edge/arrow scrolling, interrupted camera drag, Alt+Enter, supported fullscreen modes and resolution changes. Repeat the minimum 640x480, ordinary 1280x720 and supported fullscreen profiles; record unsupported modes rather than selecting an arbitrary exclusive mode. Keep command outcome and cursor/viewport alignment in the same record. Close with Alt+F4 and expect exit 0.

Use [device recovery](../DEVICE_RECOVERY.md) to distinguish automated injected failures and the explicit native reset check from physical driver loss or monitor events. A fake-device pass, successful initialization or invisible-window presentation status does not establish visible recovery. Hardware-specific loss that was not exercised remains a named limitation; this procedure does not request disconnecting a monitor or inducing a driver failure.

## P2 / NV-02: qualify the active audio scene

First run [sound checks](../AUDIO.md#check-sound), the [four optional-audio profiles](../PLAYTESTING.md#prepare-the-configuration-cases), independent shadow-switch checks in [settings verification](../SETTINGS.md#verification), and the [reinforcement delivery procedure](../PLAYTESTING.md#random-generator-modes). These supply ordinary command sounds, camera-relative listening, restored shadows on existing objects and the carrier's accepted request/90-second deadline. Observe the real carrier and delivery/icon appearance, then select and move a delivered robot. A cooldown or absent request is an unperformed carrier record. Keep package/music choices fixed within each record.

The additional active-loop record needs a locally available scene with (a) a friendly laser-equipped robot, (b) a clear Move route that stays in firing range of an opponent long enough for the observations, (c) an opponent that survives five seconds of firing and can destroy the moving robot during the removal variant, and (d) another surviving friendly robot for the post-removal command. Select and record that exact map, robot chassis/position, opposing actor/position, weapons and route before comparison. The tracked Text sound configuration declares `wlaser` as looping; the production weapon updates its position through `ChangePos` and stops it through `FireEnd`. Qualify the corresponding Packed mapping separately. Verify the laser loop is audibly identifiable while the moving robot actually fires. Ordinary `M` movement alone does not establish a chassis loop, and pneumatic footsteps are repeated one-shots. An unavailable loop, interrupted firing or inability to arrange ordinary destruction leaves the affected segment pending; do not use a Debug destruction key or report source deletion as observed.

1. Start fresh with intact effects and speech, dismiss the introduction and select the qualified emitter. Press `M` and click along the recorded route through the opponent's firing range. Within 30 seconds, require visible movement, actual laser firing/damage and its identifiable loop. Keep the camera on the moving/firing source for five seconds; the sound must follow it without accumulating independent overlapping copies. If ordinary Move does not produce sustained firing in that scene, the scene is not qualified.
2. While it continues moving and firing, use arrow-key camera scrolling to put the source first to one side and then the other, move away until its loop is no longer audible, and return. Allow at most 30 seconds for each camera segment. Record continuing motion/fire, direction/attenuation and audible re-entry. Listening establishes relative output, not an exact distance, pan value or numeric silence threshold. If motion or firing stops before a segment completes, record it unperformed and prepare a fresh repeat rather than claiming an active-loop update.
3. Repeat from fresh startup and use the same route. During audible firing press Alt+Enter once and back, following [display verification](../DISPLAY.md#verification). Require the expected visible mode transition, continuing battle/input and audible firing after return; record any dropout and its duration rather than assuming the display reset preserved the loop. This is a display transition with audio, not an audio-device replacement test.
4. In another fresh launch, order that emitter along the recorded route into the qualified opposing actor's fire. Require actual combat damage and destruction within 30 seconds of the accepted encounter, with its laser loop still audible immediately before destruction. Distinguish that loop from explosion/other actors' audio: after destruction and a five-second tail observation, its continuing loop must be absent. Select the surviving friendly robot, issue a short Move and require usable input. If firing stops before destruction, or the encounter cannot kill the emitter within the bound, record the failure or unmet scene precondition; do not replace it with the CPU stop/destructor tests.
5. Quit through the menu, expect exit 0, and relaunch to confirm new command audio works. Repeat applicable command/scene observations for effects-only, speech-only and neither optional package. Missing categories stay silent while commands and normal exit work. Source-loop listening is applicable only when effects are available; record it as unavailable in speech-only/no-audio profiles, while still performing their playable-startup and command observations.

The [automatic positional and weapon-loop tests](87-audio-continuations.md) supply separate state/event evidence; they do not provide the moving/removed robot, active display reset, carrier/icon or real listening observations above. Missing full-lifecycle automated coverage remains implementation work; a native procedure does not waive it. Physical audio-device loss uses the existing audio procedure and remains distinct from display transitions.

## P3 / NV-03: staged startup failures and successful continuations

Before P3's first record in an output directory, use the sound-override preparation below, with its own variables. Keep it parked through that directory's P3 series, then restore it after all records. Do not rebuild or change output directories while it is parked. Run the ordinary four profiles: startup menu, explicit map/bonus selection, introduction, short Move and reachable attack, then menu quit with exit 0. Repeat with Alt+F4 and exit 0.

For a fresh release archive, follow [release contents and verification](../RELEASING.md), extract the reviewed ZIP into a new local directory with spaces and non-ASCII characters, verify its supplied checksum, then add only locally supplied resources and the required x86 runtime using [Windows builds](../BUILD_WINDOWS.md). Set `$game = (Resolve-Path -LiteralPath '<extracted directory>').Path` and `$configuration = 'Release'`, then run the path setup again to bind its EXE and fallback cache. For the different-working-directory variant, after path setup set `$launchDirectory = (Resolve-Path -LiteralPath '.').Path` while PowerShell is in the repository root. Retain the handle, perform the case, then use the separate post-quit/log-excerpt block. Record archive/binary hashes; a build-directory launch does not prove archive completeness. Debug uses its staged build directory, not an invented Debug release artifact.

The [logger](../../MatrixLib/Base/stupid_logger.hpp) opens `test.log` in append mode before [WinMain](../../MatrixGame/src/MatrixGame.cpp) changes the working directory. Ordinary launches append beside the EXE; the different-working-directory launch appends in the repository root. Debug includes ordinary info/debug messages, including `Started as EXE` and cache paths. Release keeps warning/error/fatal and unconditional `notice` entries while suppressing ordinary info/debug calls. Every launch reaching `CGame::Init` writes the notice `Battle random generator:` at INFO even in Release; an empty excerpt does not establish initialization. Both builds use the byte offset saved immediately before launch and the post-exit `$newLog` bound to that owned process. Required startup-error diagnostics must still occur in their expected cases. Cache qualification uses Debug, and ordinary records verify the confirmed entry's creation rather than requiring Release debug lines. Keep the caller/log path, before/after byte boundaries and case observations separate from older appended output.

The override is independent of `$target`/`$backup` used by fault staging. An accidental rerun refuses an existing backup before changing ownership state. A failed park cannot authorize P3 startup. These blocks do not inspect or modify a root-level supplied `sounds.txt`.

```powershell
. {
    $nextSoundBackup = Join-Path $game 'CFG/.playtest-sounds.txt'
    if (Test-Path -LiteralPath $nextSoundBackup) { throw 'Reconcile the previous sound override first.' }
    if ($soundOverrideActive -or $settingsReady -or $cacheActive -or $cacheQualificationActive -or $faultActive) {
        throw 'Finish the current record/series before parking the override.'
    }
    if ($process -and -not $process.HasExited) { throw 'The previous owned process is still running.' }
    $soundOverridePath = Join-Path $game 'CFG/sounds.txt'
    $soundOverrideBackup = $nextSoundBackup
    $soundOverridePresent = Test-Path -LiteralPath $soundOverridePath -PathType Leaf
    if ((Test-Path -LiteralPath $soundOverridePath) -and -not $soundOverridePresent) { throw 'Unexpected override directory.' }
    if ($soundOverridePresent -and ([IO.File]::GetAttributes($soundOverridePath) -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Override links need separate preparation.'
    }
    $soundOverrideHash = $null
    $soundOverrideAttributes = $null
    if ($soundOverridePresent) {
        $soundOverrideHash = (Get-FileHash -LiteralPath $soundOverridePath -Algorithm SHA256 -ErrorAction Stop).Hash
        $soundOverrideAttributes = [IO.File]::GetAttributes($soundOverridePath)
    }
    $soundOverrideReady = $false
    $soundOverrideParked = $false
    $soundOverrideActive = $true
    if ($soundOverridePresent) {
        Move-Item -LiteralPath $soundOverridePath -Destination $soundOverrideBackup -ErrorAction Stop
        $soundOverrideParked = $true
        if ((Get-FileHash -LiteralPath $soundOverrideBackup -Algorithm SHA256 -ErrorAction Stop).Hash -ne $soundOverrideHash) {
            throw 'Parked override differs; stop.'
        }
    }
    if (Test-Path -LiteralPath $soundOverridePath) { throw 'Override was not parked.' }
    $soundOverrideReady = $true
}
```

After the P3 series, restore the override with this block. It also recovers a failed park before any launch by verifying an original that never moved. It refuses to overwrite an unexpected new mapping; keep the parked original if another build or writer re-created the target.

```powershell
. {
    if (-not $soundOverrideActive -or $settingsReady -or $cacheActive -or $cacheQualificationActive -or $faultActive) {
        throw 'Finish all P3 records before returning the override.'
    }
    if ($launchAttempted -and (-not $launchSucceeded -or $null -eq $process -or -not $process.HasExited)) {
        throw 'Resolve the owned launch first.'
    }
    if ($soundOverridePresent -and -not $soundOverrideParked) {
        if ((Test-Path -LiteralPath $soundOverrideBackup) -or -not (Test-Path -LiteralPath $soundOverridePath -PathType Leaf) -or
            (Get-FileHash -LiteralPath $soundOverridePath -Algorithm SHA256 -ErrorAction Stop).Hash -ne $soundOverrideHash -or
            [IO.File]::GetAttributes($soundOverridePath) -ne $soundOverrideAttributes) { throw 'The unparked override differs; preserve it.' }
    } else {
      if (Test-Path -LiteralPath $soundOverridePath) { throw 'Unexpected override; preserve both files for inspection.' }
      if ($soundOverridePresent) {
        if ((Get-FileHash -LiteralPath $soundOverrideBackup -Algorithm SHA256 -ErrorAction Stop).Hash -ne $soundOverrideHash) {
            throw 'Override backup changed.'
        }
        Move-Item -LiteralPath $soundOverrideBackup -Destination $soundOverridePath -ErrorAction Stop
        $soundOverrideParked = $false
        [IO.File]::SetAttributes($soundOverridePath, $soundOverrideAttributes)
        if ((Get-FileHash -LiteralPath $soundOverridePath -Algorithm SHA256 -ErrorAction Stop).Hash -ne $soundOverrideHash -or
            [IO.File]::GetAttributes($soundOverridePath) -ne $soundOverrideAttributes) { throw 'Override restoration differs.' }
      }
    }
    $soundOverrideReady = $false
    $soundOverrideActive = $false
}
```

For the following faults, keep a valid required package and selected configuration except for the named target, and keep the separately prepared override absent. Change only chosen output copies after the game has exited. This example prepares invalid packed configuration; its bytes match the synthetic rejected header in existing startup contracts. For another row, change the two `next` paths and replacement bytes as described below before running the whole block.

```powershell
. {
$nextFaultTarget = Join-Path $game 'CFG/robots.dat'
$nextFaultBackup = Join-Path $game 'CFG/.playtest-robots.dat'
if (Test-Path -LiteralPath $nextFaultBackup) { throw 'Existing backup must be reconciled first.' }
if ($faultActive -or $launchAttempted -or -not $settingsReady -or -not $cacheReady -or -not $soundOverrideReady) {
    throw 'Require a fresh prepared P3 record before fault staging.'
}
$makeReplacement = $true
$replacementBytes = [byte[]](83,84,82,71,2,0,0,0)
$nextHadOriginal = Test-Path -LiteralPath $nextFaultTarget -PathType Leaf
if ((Test-Path -LiteralPath $nextFaultTarget) -and -not $nextHadOriginal) { throw 'Fault target is not a regular file.' }
$nextOriginalHash = $null
$nextOriginalAttributes = $null
if ($nextHadOriginal) {
    $nextOriginalAttributes = [IO.File]::GetAttributes($nextFaultTarget)
    if ($nextOriginalAttributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Fault-target links need separate preparation.' }
    $nextOriginalHash = (Get-FileHash -LiteralPath $nextFaultTarget -Algorithm SHA256 -ErrorAction Stop).Hash
}
$target = $nextFaultTarget
$backup = $nextFaultBackup
$hadOriginal = $nextHadOriginal
$originalHash = $nextOriginalHash
$originalAttributes = $nextOriginalAttributes
$faultParked = $false
$faultCreated = $false
$faultReady = $false
$faultHash = $null
$faultActive = $true
if ($hadOriginal) {
    Move-Item -LiteralPath $target -Destination $backup -ErrorAction Stop
    $faultParked = $true
    if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256 -ErrorAction Stop).Hash -ne $originalHash) {
        throw 'Parked original differs; preserve it and stop.'
    }
}
if (Test-Path -LiteralPath $target) { throw 'Target was not parked; do not write a replacement.' }
if ($makeReplacement) {
    $stream = [IO.File]::Open($target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $faultCreated = $true
    try { $stream.Write($replacementBytes, 0, $replacementBytes.Length) }
    finally {
        $stream.Dispose()
        $faultHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256 -ErrorAction Stop).Hash
    }
}
$faultReady = $true
# Launch and retain $process as above; observe/dismiss the diagnostic and require exit 1.
}
```

A Text record without an original `robots.dat` instead creates just that synthetic output file, records that no original existed and removes it after exit. Do not rename text data or supplied root configuration for this case. The selected profile before/after failure remains part of the record; this fault deliberately inserts invalid packed data and therefore is not a successful Text-fallback launch.

| Fresh launch | Exact staged substitution | Required observation and exit |
| --- | --- | --- |
| Missing required package | Park output `DATA/robots.pkg` as `DATA/.playtest-robots.pkg`; do not create a replacement | `Game resources required` dialog with `DATA/robots.pkg` placement instruction, no battle, exit 1 after dismissal. Retain the `Missing DATA/robots.pkg` log excerpt. |
| Invalid packed configuration | Use the eight-byte `CFG/robots.dat` above, retaining valid `DATA/robots.pkg` | `Exception:` diagnostic containing `Invalid packed configuration: cfg\robots.dat`, no battle, exit 1. Loose text is not a fallback for a present rejected packed file. |
| Damaged effects package | Park only output `DATA/sound.pkg` as `DATA/.playtest-sound.pkg`, then write one byte `120` to `DATA/sound.pkg` | `test.log` contains `Cannot open package DATA\sound.pkg`; required resources/configuration still load. Reach a playable battle, issue Move/attack, expect missing effects but usable commands and surviving speech where supplied; menu quit exit 0. |
| Damaged speech package | Restore effects first; park only output `DATA/voices.pkg` as `DATA/.playtest-voices.pkg`, then write one byte `120` to `DATA/voices.pkg` | `test.log` contains `Cannot open package DATA\voices.pkg`; playable battle, missing speech but usable commands and surviving effects where supplied; menu quit exit 0. |

For an optional-package fault, set `$nextFaultTarget` and `$nextFaultBackup` to that row, keep `$makeReplacement = $true`, and use `$replacementBytes = [byte[]](120)` in the same preparation block; require playable continuation and exit 0. For the missing-required-package row, use its paths and `$makeReplacement = $false` so no synthetic file is created. If an optional package did not exist, record initial absence, create only the synthetic replacement and restore absence afterward. These are malformed archive-open controls, not coverage of arbitrary corruption inside a valid archive. Also execute the existing absent/effects-only/speech-only/both matrix without synthetic corruption; keep its observations distinct.

After termination, restore an invalid-configuration or optional-package substitution with the exact `$target`, `$backup`, `$hadOriginal` and `$originalHash` retained from that record:

```powershell
. {
if (-not $faultActive) { throw 'No active fault preparation; leave its files unchanged.' }
if ($launchAttempted -and (-not $faultReady -or -not $launchSucceeded -or $null -eq $process -or -not $process.HasExited)) {
    throw 'Resolve the owned launch before fault restoration.'
}
if ($hadOriginal -and -not $faultParked) {
    if (Test-Path -LiteralPath $backup) { throw 'An unclaimed fault backup needs inspection.' }
    if ($launchAttempted -or $faultCreated -or -not (Test-Path -LiteralPath $target -PathType Leaf) -or
        (Get-FileHash -LiteralPath $target -Algorithm SHA256 -ErrorAction Stop).Hash -ne $originalHash -or
        [IO.File]::GetAttributes($target) -ne $originalAttributes) { throw 'The unparked original differs; preserve it.' }
} else {
    if ($hadOriginal -and
        (Get-FileHash -LiteralPath $backup -Algorithm SHA256 -ErrorAction Stop).Hash -ne $originalHash) {
        throw 'Verified original backup is unavailable.'
    }
    if (Test-Path -LiteralPath $target) {
        if (-not (Test-Path -LiteralPath $target -PathType Leaf) -or
            ([IO.File]::GetAttributes($target) -band [IO.FileAttributes]::ReparsePoint)) { throw 'Unexpected fault-target type.' }
        $targetHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256 -ErrorAction Stop).Hash
        if (-not ($hadOriginal -and $targetHash -eq $originalHash)) {
            if (-not $faultCreated -or $null -eq $faultHash -or $targetHash -ne $faultHash) {
                throw "Target is not this record's unchanged synthetic file; stop."
            }
            Remove-Item -LiteralPath $target -ErrorAction Stop
        }
    }
    if ($hadOriginal) {
        if (-not (Test-Path -LiteralPath $target)) { [IO.File]::Copy($backup, $target, $false) }
        [IO.File]::SetAttributes($target, $originalAttributes)
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256 -ErrorAction Stop).Hash -ne $originalHash -or
            [IO.File]::GetAttributes($target) -ne $originalAttributes) { throw 'Restored bytes/attributes differ.' }
        Remove-Item -LiteralPath $backup -Force -ErrorAction Stop
    } elseif (Test-Path -LiteralPath $target) { throw 'Original absence was not restored.' }
}
$faultReady = $false
$faultActive = $false
}
```

The same restoration block returns a missing-required package without deleting an unowned target. Validation and hashing precede `$faultActive`; a rejected target has not moved, so restore the already prepared cache/settings without running fault restoration. After activation, a parking or replacement-write failure before any launch can use the fault-restoration block even while `$faultReady` is false. It verifies the still-unparked original, or the parked backup and any owned partial synthetic file, before returning the original bytes/attributes or absence. Unknown or changed files keep the record active for inspection. An uncertain attempted launch must be resolved before any restoration. Restore settings and the selected minimap cache too. Prepare and restore a separate fresh recovery record before moving to the next fault. Keep each launch's native exit code and bound latest log excerpt distinct.

The existing `game.startup.native_*` cases independently assert early failure diagnostics and exits, including malformed optional packages followed by deliberately rejected configuration. They do not establish playable continuation after a malformed optional package above. Real-device failure, malformed entries inside otherwise valid packages and unavailable original resources retain their gaps. Use the separate [bounded CalcVis supplement](87-calcvis.md) for checked all-land exports, positional-map launch/log/runtime and the opt-in Debug diagnostic/flyer observer. Arbitrary original/custom-map CalcVis remains excluded because the non-land caller limitation is unchanged. Original-host lifetime stays [NV-07](native-validation.md).

## P4 / NV-04: settings procedure and rollback boundary

Execute [settings verification](../SETTINGS.md#verification) and [native step 5](87-presentation.md#native-procedures) in each of the four profiles. Existing steps specify menu-click/C entry, keyboard/mouse navigation, Cancel/Apply, separate shadows, volume, restart-only settings, read-only save failure, 640x480 and available English/Russian layouts. Before changing the file's read-only attribute, retain its original attributes and hash; after failed Apply, require the save error with unchanged bytes and pending edits, restore original write permission, retry Apply and verify the intended effective setting. Afterward restore all original bytes/attributes. A missing language configuration remains pending rather than borrowing another language's screenshot.

For modal isolation, establish a moving robot and assigned group before opening the menu. Record selection and positions, then deliver the existing M/group-key/Pause checks while settings are open. The battle must remain paused, commands must not leak, Cancel must discard edits, and leaving the menu must resume prior movement and accept fresh input. Use [controls](../PLAYTESTING.md#controls-and-text) for ordinary input; a cheats-enabled interception check is a separately declared build/profile and does not replace the cheats-disabled four-profile record.

Use [display verification](../DISPLAY.md#verification) for native resolution/mode changes and a supported adapter's rejected-size observation. Keep native display rejection conditional on a real rejected request: a size silently accepted or changed by the driver does not demonstrate rollback. If no safe, reproducible rejected mode is available, leave that native rollback observation pending. The production-linked [settings rollback contract](87-presentation.md#settings-input-pause-and-persistence) supplies deterministic injected-failure state evidence, not a real-GPU rejection. Do not unplug hardware or change production code to force this record. Expect exit 0 through normal menu quit; also record restored input and a fresh ordinary launch.

## Handoff status

Attach each completed record to its NV-01/NV-02/NV-03/NV-04 ledger entry with this procedure's path/version, exact revision/profile, steps, observations, exit and settings/cache/fault restoration result. This document provides procedure preparation only. All unperformed #191 cases remain pending. The [CalcVis supplement](87-calcvis.md) supplies P5's bounded setup separately; arbitrary unsafe inputs and original-host NV-07 remain outside these procedures. Neither preparation nor CPU tests establish complete-baseline acceptance.
