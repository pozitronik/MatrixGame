# Presentation procedure handoff

Procedure version: **1**.

This supplement prepares P1-P4 / PRES-01 through PRES-04 for [NV-01 through NV-04](native-validation.md) under [#191](https://github.com/pozitronik/MatrixGame/issues/191). It supplies launch/exit recording and the scenario details missing from [the presentation procedure](87-presentation.md#native-procedures); it records no executed native case. Production-linked CPU tests establish their documented input, state, resource and audio-event contracts, while real GPU output, OS focus delivery and audible playback require separate observations. Unperformed, unavailable and failed cases remain separate pending evidence.

## Prepare and bound each record

Use [Windows builds](../BUILD_WINDOWS.md) for the pinned x86 EXE, runtime and locally supplied resources. Build Debug and Release with cheats disabled before changing output copies; a later build stages configuration again. Run P1-P4 in Debug/Packed, Debug/Text, Release/Packed and Release/Text. Use the [configuration matrix](../PLAYTESTING.md#prepare-the-configuration-cases) to select Packed or Text, including removal of output `robots.dat.*` aliases that would defeat Text fallback. Keep `CFG/standalone.txt` separate from game configuration. Preserve its bytes, attributes and the selected output resources before each case; never modify the supplied root-level packages or an installed game.

```powershell
$ErrorActionPreference = 'Stop'
.\tools\build.ps1 -Configuration Debug
if ($LASTEXITCODE -ne 0) { throw 'Debug build failed.' }
.\tools\build.ps1 -Configuration Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
.\tools\setup-directx-runtime.ps1
if ($LASTEXITCODE -ne 0) { throw 'Runtime preparation failed.' }
# Select one already-built profile; change debug to release for the other build.
$game = (Resolve-Path '.\build\mingw-debug-exe\MatrixGame').Path
Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination $game -ErrorAction Stop
$exe = Join-Path $game 'MatrixGame.exe'
Get-FileHash -LiteralPath $exe -Algorithm SHA256
```

Check each helper's exit status before continuing. If `DATA/robots.pkg`, the selected Packed configuration, a required map or a usable device is unavailable, name that requirement and leave the affected case unperformed. Record this procedure's path/version, source/binary identity, compiler, assertions/cheats, configuration/resource hashes, map, bonus/difficulty, RNG, sound mapping, packages, resolution, scaling, adapter and driver using [the presentation evidence record](87-presentation.md#evidence-record). Audio availability changes RNG consumption; compare each audio profile only with its matching reference.

Before each record, close the standalone game and other games sharing its Documents output directory. Keep them closed until restoration completes. Prepare the settings and exact minimap-cache backups below before any fault substitution or launch. Reset process/ownership variables for each record; a failed launch must never reuse a preceding record's process object. Stop on any command error. These blocks are separate operator steps, not one script to paste through the gameplay interval.

Use one process at a time. For an ordinary visible launch, retain the process object in PowerShell:

```powershell
if (-not $settingsReady -or -not $cacheReady) { throw "Complete this record's settings/cache preparation first." }
$process = $null
$launchSucceeded = $false
$launchAttempted = $true
$process = Start-Process -FilePath $exe -WorkingDirectory $game -PassThru -ErrorAction Stop
if ($null -eq $process) { throw 'No process handle; resolve the launch before restoration.' }
$null = $process.Handle
$launchSucceeded = $true
$process.Id
```

Perform one case, then initiate its specified quit route or dismiss its expected error dialog. Only then run this separate exit block:

```powershell
if (-not $launchSucceeded -or $null -eq $process) { throw 'No verified launch; exit evidence is incomplete.' }
if (-not $process.WaitForExit(30000)) { throw 'Exit timed out; retain the PID and diagnose before restoring files.' }
$process.Refresh()
$process.ExitCode
```

Retain the native handle immediately so exit status remains available after termination, as in the public outcome relaunch monitor. A missing exit code is incomplete evidence. Dispose the retained process object after recording its exit and completing restoration.

Allow 60 seconds from launch for the startup menu and 60 seconds after accepting the map for the introduction, matching the [ordinary outcome procedure](85-native-outcomes.md#common-launch-and-scene). Allow 30 seconds for an expected error dialog, each UI transition or short movement/attack observation, and exit after initiating quit/dismissing the dialog. Unless the linked procedure supplies a different bound, observe a sustained state for five seconds. Carrier arrival uses the existing 90-second bound. An unavailable scene is unperformed; an accepted action that exceeds its declared bound is a failed observation. Record the last visible state and a minimal diagnostic/log excerpt. Do not extend a deadline until the case appears to pass.

The commands above measure exit after the operator completes the case; they do not automate the observation deadlines or prove the observations. A timeout can leave the process alive: close the owned window and confirm termination before changing/restoring files. A forced termination is an aborted case, never exit-code evidence for a normal quit. These cases do not select another battle from the map selector; that can create another process and needs the separate [outcome relaunch procedure](85-native-outcomes.md).

After every record, verify no owned game process remains and other games sharing the output directory are closed, then restore the settings, selected cache entry and faulted output files below. Remove only files whose creation this record owns. Restore after a failure too; retain backups if a running process, uncertain failed launch or interrupted cleanup prevents restoration. Prepare a separate record for the fresh recovery launch: start an ordinary battle, issue a short Move, quit with exit 0 and restore that launch's settings/cache too. Recovery does not replace the failed record.

## Preserve settings for each record

Run this preparation after choosing `$game` and before any launch or fault substitution. Use a new backup directory for every record; retain it until all restoration checks pass. `Copy-Item` preserves the original settings in place for the launch, and the hash check must complete before changing them. Keep the recorded variables in the same PowerShell session. A missing settings file is an original state to preserve too.

```powershell
$ErrorActionPreference = 'Stop'
$process = $null
$launchAttempted = $false
$launchSucceeded = $false
$settingsReady = $false
$cacheReady = $false
$faultReady = $false
$caseBackup = Join-Path $game '.playtest-presentation-backup'
if (Test-Path -LiteralPath $caseBackup) { throw 'Reconcile the previous record before proceeding.' }
New-Item -ItemType Directory -Path $caseBackup -ErrorAction Stop | Out-Null
$settings = Join-Path $game 'CFG/standalone.txt'
$savedSettings = Join-Path $caseBackup 'standalone.txt'
$settingsPresent = Test-Path -LiteralPath $settings -PathType Leaf
if ((Test-Path -LiteralPath $settings) -and -not $settingsPresent) { throw 'Settings path is not a regular file.' }
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
```

After the case has exited, restore with the following separate block. It also permits restoration when no launch was attempted, without dereferencing a null process. If launch was attempted but its success or process termination is uncertain, stop: identify and close any owned process before manually resuming restoration. Never clear the launch state merely to bypass that check.

```powershell
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
```

## Preserve the selected minimap cache

The standalone map loader also writes outside the build output. [Map preparation](../../MatrixGame/src/MatrixMapPrepare.cpp) passes its selected map name and `DATA_UNIQID` to [the minimap](../../MatrixGame/src/MatrixMinimap.cpp), whose key retains the map extension and appends `.` plus uppercase, unpadded hexadecimal uniqueness. The default is `0` only when that map omits `DATA_UNIQID`. [Output-path resolution](../../MatrixGame/src/MatrixGamePathUtils.cpp) normally uses the user's Documents folder under `SpaceRangersHD/Cache`, shared with the original game. An existing cache can supply the minimap without rendering a fresh background. Follow the same ownership boundary as [the special-scene cache procedure](85-native-special-scenes.md#preserve-and-restore-the-minimap-cache).

Before the first launch, confirm the exact cache filename from reviewed map metadata or a prior controlled record's `Found minimap cache file at` / `Storing minimap into cache file at` log path. Do not assume `.0`, add `.png`, flatten an unfamiliar path or select a wildcard of cache files. If the exact key/output location is unknown, preparation remains pending. The block below handles a confirmed single leaf key. An unusual path must be reviewed separately rather than normalized into another filename. Documents lookup can fail: production then uses `Cache` relative to the game's current directory, normally `$game`; qualify that actual fallback path separately before proceeding.

Close all games sharing this output location before preparation and keep them closed through restoration. Do not use a concurrent run to identify the key. Set the confirmed key interactively, then park only that entry to force fresh rendering; retain original presence, hash and attributes. No whole-cache removal or global folder redirection is required.

```powershell
$cacheReady = $false
$cacheParked = $false
$documents = [Environment]::GetFolderPath('MyDocuments')
if (-not $documents) { throw 'Qualify the actual output path; cache preparation remains pending.' }
$cacheRoot = Join-Path $documents 'SpaceRangersHD/Cache'
$cacheName = Read-Host 'Confirmed exact map cache filename, including map extension and hexadecimal suffix'
if (-not $cacheName -or [IO.Path]::GetFileName($cacheName) -ne $cacheName -or $cacheName -cnotmatch '\.(0|[1-9A-F][0-9A-F]{0,7})$') {
    throw 'Require a confirmed leaf key; do not infer a filename.'
}
$cached = Join-Path $cacheRoot $cacheName
$savedCache = Join-Path $caseBackup 'selected-minimap-cache'
if (Test-Path -LiteralPath $savedCache) { throw 'Existing cache backup must be reconciled first.' }
$cachePresent = Test-Path -LiteralPath $cached -PathType Leaf
if ((Test-Path -LiteralPath $cached) -and -not $cachePresent) { throw 'Cache entry is not a regular file.' }
$cacheHash = $null
$cacheAttributes = $null
if ($cachePresent) {
    $cacheHash = (Get-FileHash -LiteralPath $cached -Algorithm SHA256 -ErrorAction Stop).Hash
    $cacheAttributes = [IO.File]::GetAttributes($cached)
    Move-Item -LiteralPath $cached -Destination $savedCache -ErrorAction Stop
    $cacheParked = $true
    if ((Get-FileHash -LiteralPath $savedCache -Algorithm SHA256 -ErrorAction Stop).Hash -ne $cacheHash) {
        throw 'Parked cache differs; preserve it and stop.'
    }
}
if (Test-Path -LiteralPath $cached) { throw 'Cache was not parked; do not launch.' }
$cacheReady = $true
```

After the owned process exits, compare the log's actual cache location/key with this record. If another game ran, the path differs or file ownership is uncertain, stop with the backups retained; do not remove another entry. Otherwise restore only this selected entry, even after a failed observation. A no-launch preparation may return its parked original, but must not delete an unexpected newly appearing entry. Leave the shared directories, unrelated caches and screenshots untouched.

```powershell
if (-not $cacheReady) { throw 'No verified cache preparation; inspect the retained files.' }
if ($launchAttempted -and (-not $launchSucceeded -or $null -eq $process -or -not $process.HasExited)) {
    throw 'Resolve the owned launch before restoring cache.'
}
if ($cachePresent -and (-not $cacheParked -or
    (Get-FileHash -LiteralPath $savedCache -Algorithm SHA256 -ErrorAction Stop).Hash -ne $cacheHash)) {
    throw 'Verified original cache backup is unavailable.'
}
if (Test-Path -LiteralPath $cached) {
    if (-not $launchSucceeded -or -not (Test-Path -LiteralPath $cached -PathType Leaf)) {
        throw 'This record does not own the unexpected cache entry.'
    }
    Remove-Item -LiteralPath $cached -ErrorAction Stop
}
if ($cachePresent) {
    Move-Item -LiteralPath $savedCache -Destination $cached -ErrorAction Stop
    [IO.File]::SetAttributes($cached, $cacheAttributes)
    if ((Get-FileHash -LiteralPath $cached -Algorithm SHA256 -ErrorAction Stop).Hash -ne $cacheHash -or
        [IO.File]::GetAttributes($cached) -ne $cacheAttributes) { throw 'Cache restoration differs.' }
} elseif (Test-Path -LiteralPath $cached) { throw 'Original cache absence was not restored.' }
$cacheReady = $false
```

Keep the per-record backup until settings, cache and any faulted files are restored. Delete only that owned backup when its evidence is no longer needed. Do not upload cached images or private settings with the public result. If fallback output, unknown keys or shared-game activity prevented safe qualification/restoration, name that gap in the record.

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

Run the ordinary four profiles first: startup menu, explicit map/bonus selection, introduction, short Move and reachable attack, then menu quit with exit 0. Repeat with Alt+F4 and exit 0. For a fresh release archive, follow [release contents and verification](../RELEASING.md), extract the reviewed ZIP into a new local directory with spaces and non-ASCII characters, verify its supplied checksum, then add only locally supplied resources and the required x86 runtime using [Windows builds](../BUILD_WINDOWS.md). Set `$game` to that extracted directory and `$exe` to its EXE. Keep PowerShell in the repository root and substitute `-WorkingDirectory (Get-Location).Path` in the launch block to check a different working directory; retain the handle, perform the case, then use the separate post-quit exit block. Verify the same playable observations and both exit routes. Record archive/binary hashes; a build-directory launch does not prove archive completeness. The supported release archive is Release; Debug uses its staged build directory and is not an invented Debug release artifact.

The [logger](../../MatrixLib/Base/stupid_logger.hpp) opens `test.log` in append mode before [WinMain](../../MatrixGame/src/MatrixGame.cpp) changes the working directory. Ordinary launches above therefore append beside the EXE; the different-working-directory launch appends in the repository root. Record the caller directory and log length or timestamp before each launch, then identify its latest `Started as EXE` section. Later launches append to the same file; preserve a delimited excerpt rather than attributing an earlier diagnostic to this run. This log location is separate from the map cache's Documents output.

For the following faults, keep a valid required package and selected game configuration except for the named target. Use the built-in sound catalog without a loose `CFG/sounds.txt` override, as in native step 1; park and restore an existing output override too. Change only chosen output copies after the game has exited. Check for an existing backup and refuse to overwrite it. This example prepares invalid packed configuration; its bytes match the synthetic rejected header in the existing startup contracts:

```powershell
$target = Join-Path $game 'CFG/robots.dat'
$backup = Join-Path $game 'CFG/.playtest-robots.dat'
$makeReplacement = $true
$replacementBytes = [byte[]](83,84,82,71,2,0,0,0)
$faultParked = $false
$faultCreated = $false
$faultReady = $false
if (Test-Path -LiteralPath $backup) { throw 'Existing backup must be reconciled first.' }
$hadOriginal = Test-Path -LiteralPath $target -PathType Leaf
if ((Test-Path -LiteralPath $target) -and -not $hadOriginal) { throw 'Fault target is not a regular file.' }
$originalHash = $null
$originalAttributes = $null
if ($hadOriginal) {
    $originalHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256 -ErrorAction Stop).Hash
    $originalAttributes = [IO.File]::GetAttributes($target)
    Move-Item -LiteralPath $target -Destination $backup -ErrorAction Stop
    $faultParked = $true
    if ((Get-FileHash -LiteralPath $backup -Algorithm SHA256 -ErrorAction Stop).Hash -ne $originalHash) {
        throw 'Parked original differs; preserve it and stop.'
    }
}
if (Test-Path -LiteralPath $target) { throw 'Target was not parked; do not write a replacement.' }
$faultHash = $null
if ($makeReplacement) {
    $stream = [IO.File]::Open($target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $faultCreated = $true
    try { $stream.Write($replacementBytes, 0, $replacementBytes.Length) } finally { $stream.Dispose() }
    $faultHash = (Get-FileHash -LiteralPath $target -Algorithm SHA256 -ErrorAction Stop).Hash
}
$faultReady = $true
# Launch and retain $process as above; observe/dismiss the diagnostic and require exit 1.
```

A Text record without an original `robots.dat` instead creates just that synthetic output file, records that no original existed and removes it after exit. Do not rename text data or supplied root configuration for this case. The selected profile before/after failure remains part of the record; this fault deliberately inserts invalid packed data and therefore is not a successful Text-fallback launch.

| Fresh launch | Exact staged substitution | Required observation and exit |
| --- | --- | --- |
| Missing required package | Park output `DATA/robots.pkg` as `DATA/.playtest-robots.pkg`; do not create a replacement | `Game resources required` dialog with `DATA/robots.pkg` placement instruction, no battle, exit 1 after dismissal. Retain the `Missing DATA/robots.pkg` log excerpt. |
| Invalid packed configuration | Use the eight-byte `CFG/robots.dat` above, retaining valid `DATA/robots.pkg` | `Exception:` diagnostic containing `Invalid packed configuration: cfg\robots.dat`, no battle, exit 1. Loose text is not a fallback for a present rejected packed file. |
| Damaged effects package | Park only output `DATA/sound.pkg` as `DATA/.playtest-sound.pkg`, then write one byte `120` to `DATA/sound.pkg` | `test.log` contains `Cannot open package DATA\sound.pkg`; required resources/configuration still load. Reach a playable battle, issue Move/attack, expect missing effects but usable commands and surviving speech where supplied; menu quit exit 0. |
| Damaged speech package | Restore effects first; park only output `DATA/voices.pkg` as `DATA/.playtest-voices.pkg`, then write one byte `120` to `DATA/voices.pkg` | `test.log` contains `Cannot open package DATA\voices.pkg`; playable battle, missing speech but usable commands and surviving effects where supplied; menu quit exit 0. |

For an optional-package fault, set `$target` and `$backup` to that row, keep `$makeReplacement = $true`, and use `$replacementBytes = [byte[]](120)` in the same preparation block; require playable continuation and exit 0. For the missing-required-package row, use its paths and `$makeReplacement = $false` so no synthetic file is created. If an optional package did not exist, record initial absence, create only the synthetic replacement and restore absence afterward. These are malformed archive-open controls, not coverage of arbitrary corruption inside a valid archive. Also execute the existing absent/effects-only/speech-only/both matrix without synthetic corruption; keep its observations distinct.

After termination, restore an invalid-configuration or optional-package substitution with the exact `$target`, `$backup`, `$hadOriginal` and `$originalHash` retained from that record:

```powershell
if (-not $faultReady) { throw 'Staging failed; inspect originals/backups without removing the target.' }
if ($launchAttempted -and (-not $launchSucceeded -or $null -eq $process -or -not $process.HasExited)) {
    throw 'Resolve the owned launch before fault restoration.'
}
if ($hadOriginal -and (-not $faultParked -or
    (Get-FileHash -LiteralPath $backup -Algorithm SHA256 -ErrorAction Stop).Hash -ne $originalHash)) {
    throw 'Verified original backup is unavailable.'
}
if (Test-Path -LiteralPath $target) {
    if (-not $faultCreated -or -not (Test-Path -LiteralPath $target -PathType Leaf) -or
        (Get-FileHash -LiteralPath $target -Algorithm SHA256 -ErrorAction Stop).Hash -ne $faultHash) {
        throw "Target is not this record's unchanged synthetic file; stop."
    }
    Remove-Item -LiteralPath $target -ErrorAction Stop
}
if ($hadOriginal) {
    Move-Item -LiteralPath $backup -Destination $target -ErrorAction Stop
    [IO.File]::SetAttributes($target, $originalAttributes)
    if ((Get-FileHash -LiteralPath $target -Algorithm SHA256 -ErrorAction Stop).Hash -ne $originalHash -or
        [IO.File]::GetAttributes($target) -ne $originalAttributes) {
        throw 'Restored bytes differ from the original.'
    }
} elseif (Test-Path -LiteralPath $target) { throw 'Original absence was not restored.' }
$faultReady = $false
```

The same restoration block returns a missing-required package without deleting an unowned target. If parking, writing or launching failed, stop at the retained state rather than running later blocks blindly; an unsuccessful parking attempt must never cause removal of the still-original target. Restore settings and the selected minimap cache too. Prepare and restore a separate fresh recovery record before moving to the next fault. Keep each launch's exit code and latest appended log section distinct.

The existing `game.startup.native_*` cases independently assert early failure diagnostics and exits, including malformed optional packages followed by deliberately rejected configuration. They do not establish playable continuation after a malformed optional package above. Real-device failure, malformed entries inside otherwise valid packages and unavailable original resources retain their gaps. CalcVis is excluded: follow the [plane-consumer boundary](305-plane-builders.md#observable-consumers-and-native-evidence) and retain NV-03's launch/log/runtime/diagnostic obligations without running the unsafe path here. Original-host lifetime stays [NV-07](native-validation.md).

## P4 / NV-04: settings procedure and rollback boundary

Execute [settings verification](../SETTINGS.md#verification) and [native step 5](87-presentation.md#native-procedures) in each of the four profiles. Existing steps specify menu-click/C entry, keyboard/mouse navigation, Cancel/Apply, separate shadows, volume, restart-only settings, read-only save failure, 640x480 and available English/Russian layouts. Before changing the file's read-only attribute, retain its original attributes and hash; after failed Apply, require the save error with unchanged bytes and pending edits, restore original write permission, retry Apply and verify the intended effective setting. Afterward restore all original bytes/attributes. A missing language configuration remains pending rather than borrowing another language's screenshot.

For modal isolation, establish a moving robot and assigned group before opening the menu. Record selection and positions, then deliver the existing M/group-key/Pause checks while settings are open. The battle must remain paused, commands must not leak, Cancel must discard edits, and leaving the menu must resume prior movement and accept fresh input. Use [controls](../PLAYTESTING.md#controls-and-text) for ordinary input; a cheats-enabled interception check is a separately declared build/profile and does not replace the cheats-disabled four-profile record.

Use [display verification](../DISPLAY.md#verification) for native resolution/mode changes and a supported adapter's rejected-size observation. Keep native display rejection conditional on a real rejected request: a size silently accepted or changed by the driver does not demonstrate rollback. If no safe, reproducible rejected mode is available, leave that native rollback observation pending. The production-linked [settings rollback contract](87-presentation.md#settings-input-pause-and-persistence) supplies deterministic injected-failure state evidence, not a real-GPU rejection. Do not unplug hardware or change production code to force this record. Expect exit 0 through normal menu quit; also record restored input and a fresh ordinary launch.

## Handoff status

Attach each completed record to its NV-01/NV-02/NV-03/NV-04 ledger entry with this procedure's path/version, exact revision/profile, steps, observations, exit and settings/cache/fault restoration result. This document provides procedure preparation only. All unperformed #191 cases remain pending, P5/unsafe CalcVis and original-host NV-07 remain outside this leaf, and neither these procedures nor CPU tests establish complete-baseline acceptance or close #87.
