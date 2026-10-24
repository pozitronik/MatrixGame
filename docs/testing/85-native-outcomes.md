# Native timing and session procedures

Procedure version: **2**. This handoff prepares NV-05-2 and NV-06-1 from the [outcome completion plan](85-outcome-completion.md). Execution remains pending in the [native validation ledger](native-validation.md). Preparation and synthetic helper tests establish neither a successful native playtest nor complete-baseline acceptance.

## Build and preserve the candidate

Use the pinned Windows x86 toolchain and owned local resources from [Windows builds](../BUILD_WINDOWS.md). The selected scene is `Matrix\Map\desert1_3e.cmap`, the tracked configuration's ordinary starting map. It must be available in the supplied `robots.pkg` or as an equivalent local loose map. This procedure does not extract or distribute it. A missing map, missing friendly robots or no reachable open ground is a setup blocker; do not silently substitute another scene. Record the local resource/map identity without uploading its contents.

Build the cheats-disabled candidate first. Run from the repository root:

```powershell
. {
$ErrorActionPreference = 'Stop'
.\tools\build.ps1 -Configuration Release
if ($LASTEXITCODE -ne 0) { throw 'Candidate build failed; do not continue with an older executable.' }
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats Off -ConfigurationMode Text
}
```

The second command only inspects the candidate. It must report the expected build cache, x86 executable, SHA-256, configuration mode, RNG and audio availability. It does not launch or change files. Build immediately before testing: matching cache entries are not proof that an arbitrary old executable matches the checkout. Record the source revision and successful build command alongside the binary hash. The helper rejects DLL, non-x86 and mismatched cheats/configuration candidates.

For the enabled-code checks, finish the disabled candidate's runs, then rebuild and inspect:

```powershell
. {
$ErrorActionPreference = 'Stop'
.\tools\build.ps1 -Configuration Release -Cheats
if ($LASTEXITCODE -ne 0) { throw 'Candidate build failed; do not continue with an older executable.' }
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats On -ConfigurationMode Text
}
```

Both builds reuse the same output directory. Finish a run and wait for configuration restoration before rebuilding, changing profiles or starting another game. For Debug, replace `Release` with `Debug` in both commands. For MSVC, add `-Compiler MSVC` to both; the helper accepts its multi-configuration cache and selects the matching `MatrixGame/<Configuration>` executable directory. `-ConfigurationMode Packed` requires a locally supplied `robots.dat` staged by the build; `Text` temporarily hides the output copy without touching the source package. `-RandomGenerator LegacyCRT` selects the second RNG profile; the default is ParkMiller. Do not rename a packed backup to `robots.dat.backup`: resource lookup also matches that name, and the helper rejects such aliases in a text run.

Use the relevant Debug/Packed, Debug/Text, Release/Packed and Release/Text profiles from the ledger, recording each required RNG/audio variant independently. These are prepared variants, not claims that earlier playtests covered them. An unavailable variant stays pending until the maintainer explicitly disposes of it. The helper reports installed audio packages and any `sounds.txt` override; it does not silently remove them to manufacture an audio profile.

## Protect each record before launch

Reuse the shared [settings context](87-native-procedures.md#preserve-settings-for-each-record), [cache protection](87-native-procedures.md#preserve-the-selected-minimap-cache) and [guarded completion](87-native-procedures.md#complete-a-record) blocks unchanged. Run every complete `. { ... }` block as one statement and wait for success before the next. Permit only one launch attempt per prepared record, including a failed or uncertain attempt without a retained handle. An exited successful process also needs completed restoration/guarded cleanup and a new record before another launch. Do not rebuild, start another game, change a profile or reset an active flag while a record remains unresolved.

Select the already-built configuration, set `$game` to its absolute executable directory and `$exe` to its `MatrixGame.exe`; for a synthetic export, use its directory. Run the shared path-resolution block for the two distinct absolute `$cacheRoots`, then its settings preparation. Keep the shared `.playtest-presentation-backup` name and cleanup contract unchanged; it is separate from the launch helper's `CFG/.native-outcome-backup`. Close other games sharing either cache root and keep them closed through restoration.

For an unknown key, perform the separate [Debug qualification](87-cache-qualification.md#copy-the-pre-launch-state): copy both cache trees before launch, load only the intended map, quit and [restore only its logged entry](87-cache-qualification.md#perform-the-separate-debug-qualification). Use the owned helper adapter below instead of #87's direct launch when qualification requires `-AlternateMapFile`; it stages the same alias as the real case. Qualify the installed filename (`native-outcome-alternate.cmap` or `native-outcome-demo.cmap`) and map/resource identity, not only its payload. A different alias needs its own qualification. Release cannot discover a key from Debug-only minimap messages. The completion block below binds only the current retained helper PID and its actual native exit result after the full appended log is read. Qualification requires one native code-0 lifetime, not just helper exit 0. If a closed launch reports no cache path, use the shared [no-path recovery](87-cache-qualification.md#recover-a-launch-with-no-cache-path); do not delete its snapshot or guess a key. Failed copies, ambiguous logs or unfamiliar paths leave preparation incomplete; never discover a key by an unprotected first play.

For subsequent records, supply the complete confirmed `$cacheNames` array and run the common known-key protection block; every selected leaf is protected at both roots. Synthetic guides may supply exact keys from their generated manifests. Protect all maps that a relaunch can load before its first process starts. Do not infer a suffix for an arbitrary map or remove wildcards. Retain the key record, input hashes and log excerpt before shared cleanup.

### Capture the log boundary

After shared settings/cache preparation and before either launch adapter, run this block. It records this backup directory's lifetime and the candidate hash, plus the existing log's length, creation time and complete-prefix hash. It reads no old log text into the new evidence. Another build, replaced/truncated log or stale record cannot supply this launch's evidence.

```powershell
. {
$ErrorActionPreference = 'Stop'
if ($launchAttempted) { throw 'One launch attempt per prepared record; resolve and restore it before preparing another.' }
if (-not $settingsReady -or -not $caseBackupOwned) { throw 'Prepare the shared owned record first.' }
$currentGame = (Resolve-Path -LiteralPath $game -ErrorAction Stop).Path
if ([IO.Path]::GetFullPath($exe) -ine (Join-Path $currentGame 'MatrixGame.exe')) { throw 'The executable does not belong to the protected game.' }
$fallbackRoots = @($cacheRoots | Where-Object Name -eq 'Fallback')
if ($fallbackRoots.Count -ne 1 -or $fallbackRoots[0].Path -ine (Join-Path $currentGame 'Cache')) { throw 'Run shared path setup for this game before preparing its cache.' }
if ($settings -ine (Join-Path $currentGame 'CFG/standalone.txt') -or $caseBackup -ine (Join-Path $currentGame '.playtest-presentation-backup')) { throw 'The settings record belongs to another game.' }
$boundaryFile = Join-Path $caseBackup 'log-boundary.json'
if (Test-Path -LiteralPath $boundaryFile) { throw 'Do not overwrite a preceding launch boundary.' }
$logBoundaryReady = $false
$logBoundaryOwner = (Get-Item -LiteralPath $caseBackup -ErrorAction Stop).CreationTimeUtc
$recordedExeHash = (Get-FileHash -LiteralPath $exe -Algorithm SHA256 -ErrorAction Stop).Hash
$logPath = Join-Path $game 'test.log'
$logPresentBefore = Test-Path -LiteralPath $logPath -PathType Leaf
if ((Test-Path -LiteralPath $logPath) -and -not $logPresentBefore) { throw 'The log path is not a regular file.' }
if ($logPresentBefore -and ((Get-Item -LiteralPath $logPath -Force -ErrorAction Stop).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Do not follow a log link.' }
$logBeforeLength = if ($logPresentBefore) { (Get-Item -LiteralPath $logPath -ErrorAction Stop).Length } else { 0L }
$logCreationBefore = if ($logPresentBefore) { (Get-Item -LiteralPath $logPath -ErrorAction Stop).CreationTimeUtc } else { $null }
$logHashBefore = if ($logPresentBefore) { (Get-FileHash -LiteralPath $logPath -Algorithm SHA256 -ErrorAction Stop).Hash } else { $null }
[IO.File]::WriteAllText($boundaryFile, (@{Executable=$exe; Hash=$recordedExeHash; Log=$logPath; Length=$logBeforeLength; OriginalHash=$logHashBefore} | ConvertTo-Json), [Text.UTF8Encoding]::new($false))
$logBoundaryReady = $true
}
```

### Owned helper launch adapter

Case snippets set a `$caseArguments` hashtable without launching. Apply that exact configuration/cheats/Text/Packed/RNG/alias selection to the candidate and protection record first, with the same `$configuration`. The adapter retains a real child PowerShell running the existing launcher; game handles are owned and checked inside that launcher. It never fabricates a process object or treats a PID as lifetime proof. The helper console is hidden; its explicitly requested game remains visible. Output/error and the exact caught exception message stay in the owned record; expected failures are matched against that message rather than formatted console text. Run this block from the repository root after exactly one shared cache route is ready.

```powershell
. {
$ErrorActionPreference = 'Stop'
if ($launchAttempted) { throw 'One launch attempt per prepared record; resolve and restore it before preparing another.' }
if (-not $settingsReady -or ($cacheReady -eq $cacheQualificationReady)) { throw 'Require exactly one verified cache route.' }
if ($cacheQualificationReady -and $configuration -ne 'Debug') { throw 'Qualification requires Debug.' }
if ($cacheQualificationReady -and $caseArguments.ExpectedProcesses -ne 1) { throw 'Qualification requires exactly one native lifetime.' }
if (($cacheActive -and -not $cacheReady) -or ($cacheQualificationActive -and -not $cacheQualificationReady) -or ($faultActive -and -not $faultReady)) { throw 'Resolve unfinished shared preparation before launch.' }
$currentGame = (Resolve-Path -LiteralPath $game -ErrorAction Stop).Path
if ([IO.Path]::GetFullPath($exe) -ine (Join-Path $currentGame 'MatrixGame.exe')) { throw 'The executable does not belong to the protected game.' }
$fallbackRoots = @($cacheRoots | Where-Object Name -eq 'Fallback')
if ($fallbackRoots.Count -ne 1 -or $fallbackRoots[0].Path -ine (Join-Path $currentGame 'Cache')) { throw 'Run shared path setup for this game before preparing its cache.' }
if ($settings -ine (Join-Path $currentGame 'CFG/standalone.txt') -or $caseBackup -ine (Join-Path $currentGame '.playtest-presentation-backup')) { throw 'The settings record belongs to another game.' }
if ($process -and -not $process.HasExited) { throw 'The preceding retained process is still running.' }
if (@(Get-Process -ErrorAction Stop | Where-Object ProcessName -eq 'MatrixGame').Count) { throw 'Close all MatrixGame processes first.' }
if (-not $caseArguments.Launch -or $caseArguments.Restore -or $caseArguments.Configuration -ne $configuration) { throw 'Case/profile arguments are inconsistent.' }
if (-not $caseArguments.WatchRelaunches) { throw 'Require the retained-process monitor.' }
if ($expectedHelperExit -eq 1) {
    if ($cacheQualificationReady -or $outcomeCase -notin @('R1','I1') -or -not $caseArguments.AlternateMapFile) { throw 'Only predeclared known-key R1/I1 failures may expect helper exit 1.' }
    $requiredCount = if ($outcomeCase -eq 'R1') { 1 } else { 2 }
    if ($caseArguments.ExpectedProcesses -ne $requiredCount -or ($outcomeCase -eq 'I1' -and $caseArguments.ConfigurationMode -ne 'Text')) { throw 'The negative-case profile/count differs.' }
} elseif ($expectedHelperExit -ne 0 -or $outcomeCase -ne 'Normal') { throw 'Declare a normal success or the exact existing R1/I1 contract before launch.' }

if ($caseArguments.AlternateMapFile) { $caseArguments.AlternateMapFile = (Resolve-Path -LiteralPath $caseArguments.AlternateMapFile -ErrorAction Stop).Path }
$helper = (Resolve-Path -LiteralPath '.\tools\playtest-outcomes.ps1' -ErrorAction Stop).Path
$recordedHelperHash = (Get-FileHash -LiteralPath $helper -Algorithm SHA256 -ErrorAction Stop).Hash
$inspectionArguments = $caseArguments.Clone()
$inspectionArguments.Launch = $false
$inspection = @(& $helper @inspectionArguments)
$reportedExecutable = @($inspection | Where-Object { $_ -like 'Executable: *' })
if ($reportedExecutable.Count -ne 1 -or [IO.Path]::GetFullPath($reportedExecutable[0].Substring(12)) -ine $exe) { throw 'The helper candidate differs from the protected game directory.' }
$inspection | Set-Content -LiteralPath (Join-Path $caseBackup 'candidate-inspection.txt') -Encoding UTF8 -ErrorAction Stop
$helperBackup = Join-Path $game 'CFG/.native-outcome-backup'
if (Test-Path -LiteralPath $helperBackup) { throw 'Resolve the preceding helper backup first.' }
$helperOutput = Join-Path $caseBackup 'helper.stdout.txt'
$helperError = Join-Path $caseBackup 'helper.stderr.txt'
$helperException = Join-Path $caseBackup 'helper.exception.txt'
if ((Test-Path -LiteralPath $helperOutput) -or (Test-Path -LiteralPath $helperError) -or (Test-Path -LiteralPath $helperException)) { throw 'Do not overwrite another launch record.' }
if (-not $logBoundaryReady -or (Get-Item -LiteralPath $caseBackup -ErrorAction Stop).CreationTimeUtc -ne $logBoundaryOwner) { throw 'Capture this record''s log boundary first.' }
$argumentJson = ConvertTo-Json -InputObject $caseArguments -Compress
# Escape single quotes so paths/JSON remain data in the child command.
$command = '$ErrorActionPreference = ''Stop''; [Console]::OutputEncoding = [Text.UTF8Encoding]::new($false); try { $values = ConvertFrom-Json ''' + $argumentJson.Replace("'", "''") + '''; $arguments = @{}; $values.PSObject.Properties | ForEach-Object { $arguments[$_.Name] = $_.Value }; & ''' + $helper.Replace("'", "''") + ''' @arguments; exit 0 } catch { [IO.File]::WriteAllText(''' + $helperException.Replace("'", "''") + ''', $_.Exception.Message, [Text.UTF8Encoding]::new($false)); Write-Error $_ -ErrorAction Continue; exit 1 }'
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
$launchMode = 'OutcomeHelper'
$newLog = $null
$newLogProcessId = $null
$gameExitCode = $null
$process = $null
$launchSucceeded = $false
$launchAttempted = $true
$process = Start-Process -FilePath (Get-Command powershell.exe -CommandType Application -ErrorAction Stop).Source -ArgumentList @('-NoProfile', '-NonInteractive', '-EncodedCommand', $encoded) -WorkingDirectory (Get-Location).Path -WindowStyle Hidden -RedirectStandardOutput $helperOutput -RedirectStandardError $helperError -PassThru -ErrorAction Stop
if ($null -eq $process) { throw 'No retained helper handle; preserve uncertain launch state.' }
$null = $process.Handle
$retainedProcessStart = $process.StartTime.ToUniversalTime()
$process.Id
}
```

Perform only the declared case and quit route, then use the separate completion/evidence block below. Do not call it before quit: its 30-second exit bound is not the gameplay-observation duration. An interrupted/failed helper, missing handle, remaining game or retained helper backup leaves restoration unverified. Preserve the record and resolve that lifetime; do not kill a merely polled same-path process or force a ready flag. The [R1/I1 call sites](85-native-failures.md) predeclare helper exit 1 only for their existing negative contracts. R1 requires one native exit 1; I1 requires the initial native exit 0 and observed replacement exit 1. Exact diagnostics and subsequent successful recovery remain mandatory. Neither is allowed for first-map qualification, which must complete a normal Debug battle/quit. Unknown failures retain backups; no arbitrary exit 1 becomes a pass.

## Common launch and scene

After protection is ready, select these arguments and run the [owned helper adapter](#owned-helper-launch-adapter) when ready for a visible game window:

```powershell
. {
$ErrorActionPreference = 'Stop'
$outcomeCase = 'Normal'
$expectedHelperExit = 0
$caseArguments = @{ Configuration = 'Release'; Cheats = 'Off'; ConfigurationMode = 'Text'; Launch = $true; WatchRelaunches = $true; ExpectedProcesses = 1 }
}
```

Use `Cheats = 'On'` in the arguments for an enabled-code run and the matching build/profile options described above. The helper temporarily sets Windowed 1280x720, camera speed 100%, bonus None, the selected RNG and the current corrected compatibility options. It preserves the original staged `CFG/standalone.txt` bytes, temporarily removes `CFG/robots.dat` only for Text, and restores both after process exit. It launches without arguments: a positional map argument selects visibility calculation rather than interactive play. Root-level supplied files and packages remain unchanged.

1. Open a small unrelated window, such as Notepad, beside the game so that a focus-loss observation does not hide the battlefield. Record Windows scaling and the actual client size.
2. Within 60 seconds of launch, the startup menu should appear before any battlefield. Choose **New battle**. The remembered `desert1_3e.cmap` entry should be selected; choose bonus **None** and press Enter once to start. Allow up to 60 seconds for the introduction. If a different map is selected, the expected entry is unavailable, or loading fails, stop and record the setup failure.
3. Confirm the log's `Starting the remembered map` path and `Battle random generator` line against this profile. `test.log` is in the executable directory because the helper uses that working directory. Localized catalog names may differ from the filename; the logged path identifies the loaded map.
4. Before dismissing the introduction, note the starting camera view, friendly base and friendly robot positions. Dismiss its checkmark. Select one friendly robot near the base, press `M`, and left-click unobstructed ground at least ten robot lengths away. Movement should begin within two seconds; allow up to 30 seconds for arrival. If the destination is unreachable, choose another open point and record the change. A stationary or absent robot does not establish a running battle.
5. Camera scrolling uses **arrow keys** in tactical view. `W/A/S/D` controls a robot in manual mode and is not the camera-scroll check. Remapped bindings require an explicitly recorded equivalent.

For checks that require a pending order, choose a longer reachable route and perform the action while the robot is visibly moving. If it arrives first, repeat with another destination; do not record an idle-order case as a pending-order pass. After focus return, allow two seconds for drawing/input recovery. Allow 30 seconds for a menu, confirmation, restart, result/selector transition or normal process exit unless a step gives a shorter observation interval.

Do not start another map from a battle-end selector in these two cases. Cancel that selector instead. These commands record the launched process's ID and exit code. The [selector/relaunch procedures](85-native-selector.md) separately use the helper's observed-process monitor with an expected count and explicit destination-readiness checks; polling cannot prove every short-lived failure was observed. If a further MatrixGame process remains, restoration is deferred and a warning names the recovery action.

## NV-05-2: speed, keep-alive and focus

Catalog: MODE-01 and OUT-03. Preparation: ready for the specified ordinary-map scene; native execution pending. Run the disabled gate first, then the two enabled launches. Keep the developer console closed, use an English keyboard layout and release keys between codes. Before typing a code, deselect robots by clicking bare terrain and keep the pointer over bare terrain: individual letters can also reach ordinary shortcuts, and Debug has a separate `K` damage handler over objects.

### Disabled gate

1. With `-Cheats Off`, type `NEED4SPEED` once, then `KEEPALIVE` once in the ordinary battle view. No true/false diagnostic should appear and neither developer mode should activate. Clear any ordinary pending command with `X` before selecting a robot for the next step. Do not use absence of a diagnostic alone as the focus result.
2. Issue a long Move order. While the robot moves, hold Up Arrow for one second, Alt+Tab to the adjacent window, then release Up Arrow there. Observe the inactive battlefield for five seconds. Simulation should suspend and the cursor should be free; a windowed game should remain visible. Compare the robot against fixed terrain landmarks, not camera displacement.
3. Return without holding keys. The robot should continue its accepted order; the camera must not keep scrolling. Press and release Up Arrow once to confirm fresh scrolling, then issue a fresh Move. If the robot arrived before focus loss, repeat the pending-order setup.
4. Close with Alt+F4 while another Move is pending. Expect process exit code 0, released cursor and no exception. This also supplies the matching NV-06-1 close observation when recorded under both IDs.

### Enabled launch A: toggle off/on and restart

1. With `-Cheats On`, perform the five-second inactive observation once before any code. It should suspend as in the disabled gate, confirming the fresh process starts with KEEPALIVE off.
2. Type `NEED4SPEED`. Watch for `4SPEED flag is set to true` immediately after the final key; the diagnostic lasts about one second. Issue a long Move and observe accelerated movement. Type the code again and observe `false`, then issue a comparable Move at normal speed. This is a visible mode check, not a stopwatch proof of an exact 4:1 ratio: the scheduler caps each delivered step at 100 ms.
3. Type `KEEPALIVE` and observe `true`. Issue a long Move, hold Up Arrow, Alt+Tab away and release the arrow outside the game. During five inactive seconds, the visible robot should continue its route and the cursor should be free. Return with no held keys: the camera must remain stopped until a fresh arrow press. Repeat with the middle mouse button: rotate, switch away while holding it, release outside, return, and move the pointer with no button. There should be no continued rotation; a fresh middle-button drag should work.
4. Type `KEEPALIVE` again and observe `false`. Repeat the five-second inactive Move check: simulation should suspend again and resume on return. Verify fresh scrolling and movement.
5. Turn **both** modes on again, observing `true` for each. Start a Move, then press Escape, `R`, Enter to confirm Restart while movement remains pending. Allow 30 seconds. The introduction and starting situation should return, old selection/orders should be cleared, and fresh movement should work after dismissal.
6. A UI restart reloads the map; it does not start a new process or clear these application flags. Type `NEED4SPEED` once and require `false`; type `KEEPALIVE` once and require `false`. Repeat the five-second suspension check and a fresh Move. This observes retained flags through restart and then leaves them off.
7. While a Move is pending, press Escape, `E`, Enter. Within 30 seconds, statistics should appear. Inspect them and press Enter once. **Exit** must terminate with code 0 and must not open the map selector or launch another process. Expect no exception or cursor confinement after exit.

### Enabled launches B/C: quit with toggles on, then fresh defaults

1. Make a fresh enabled launch. Turn both modes on, observing `true`, issue a Move and close with Alt+F4 during movement. Expect code 0 and a released cursor.
2. Make another fresh enabled launch with the same helper command. Type each code once: both must report `true`, showing that the previous process's enabled flags did not carry into this process. Type each once more to observe `false`. Verify one fresh Move, then close through Escape, `E`, Enter, inspect statistics and Enter. Expect code 0 with no selector.

Record the diagnostic values, inactive movement/suspension, input release, restart and every exit separately. A missing diagnostic, unclear observation or forced closure is not a pass. No AUTO or tester preset is used in this case.

## NV-06-1: restart twice and active-order quit routes

Catalog: OUT-02/03. Preparation: ready for the specified ordinary-map scene; native execution pending. Use `-Cheats Off`. Keep each launch separate so that Alt+F4, menu Exit and Surrender cannot mask one another's result. Shared NV-05-2 observations may be linked when their exact profile and steps match.

### Launch 1: Alt+F4

Start the common scene and issue a long Move. While the robot is visibly moving, press Alt+F4. Within 30 seconds, the process should terminate with code 0, no error dialog and a free desktop cursor. Closing a startup menu or idle battle does not satisfy this row.

### Launch 2: cancel, restart twice, then Exit

1. Start the common scene and move a robot away from its starting position. While it is still moving, press Escape, `R`. Press Escape to cancel confirmation, then Escape again to resume. The same battle should remain, with the robot at its progressed position rather than its initial spawn. It should resume the pending order. Issue another fresh Move.
2. During that Move, press Escape, `R`, Enter. Within 30 seconds the introduction should return. Compare the initial view, base and starting robots with the pre-dismissal notes. The earlier moved robot must not remain at the old destination as an additional unit, and old selection markers, movement destinations or action effects must not persist. Dismiss the introduction, select a starting robot and verify a new Move.
3. Repeat the confirmed restart during the fresh Move. Check the introduction, starting situation, cleared selection/orders and a new successful Move again. Both restarts must occur in the same launched process; the helper should not print an intermediate exit. This tests map reload, not two `CGame::Init` lifetimes.
4. During another Move, press Escape, `E`, Enter. Inspect statistics and press Enter. Expect code 0 within 30 seconds, no selector, no additional game process and a released cursor. If a confirmation does not respond to Enter, use its visible checkmark only to recover and record the keyboard failure.

If no action effect existed before restart, record that visual observation as not applicable; this ordinary movement scene does not establish clearing every weapon/debris effect. Synthetic restart evidence owns those object/effect lifetime assertions. An unexpected old unit or marker is a failure even when a fresh command still works.

### Launch 3: Surrender, statistics, selector, Cancel

Start the common scene and issue a Move. While it remains pending, press Escape, `S`, Enter to confirm surrender. Statistics should appear within 30 seconds. Inspect them and press Enter once. The **map selector must appear**; surrender does not follow menu Exit's direct-termination rule. Do not choose a map. Press Escape once or click Cancel. Expect process exit code 0 within 30 seconds, no new game process and a free cursor. This is surrender/result routing, not natural defeat or victory detection.

## Restoration and evidence

Run this only after initiating the declared quit route or acknowledging its expected negative-case diagnostic. For helper cases it verifies the retained helper's lifetime/exit, its settings restoration and absence of every game before enabling shared restoration. For a direct synthetic export, the same guard checks its retained root process after all recorded replacement processes have exited. A polled process is never a termination target. R1/I1 require the exact recorded native diagnostic before invoking expected-failure completion; a wrong/unknown diagnostic is a failed record with retained backups. The code checks process/cleanup evidence, not the visible error dialog or the later recovery observations. Direct owned-game failure may permit verified restoration after closure but remains a failed case. Unknown/interrupted helper failures retain backups; do not reset flags to pass this guard.

```powershell
. {
$ErrorActionPreference = 'Stop'
$newLog = $null
$newLogProcessId = $null
$gameExitCode = $null
$observedGameExitCode = $null
$launchSucceeded = $false
if (-not $launchAttempted -or $null -eq $process -or $launchMode -notin @('OutcomeHelper','DirectExport')) { throw 'No verified launch context.' }
if (-not $process.WaitForExit(30000)) { throw 'Retained process has not exited; preserve all backups.' }
$process.Refresh()
if ($process.StartTime.ToUniversalTime() -ne $retainedProcessStart) { throw 'Process lifetime differs; preserve the record.' }
if (@(Get-Process -ErrorAction Stop | Where-Object ProcessName -eq 'MatrixGame').Count) { throw 'A game remains; retain the record.' }
if ($launchMode -eq 'OutcomeHelper' -and (Test-Path -LiteralPath $helperBackup)) { throw 'The helper retained its backup; resolve it before shared restoration.' }
if ($launchMode -eq 'OutcomeHelper') {
    if ($process.ExitCode -ne $expectedHelperExit -or (Get-FileHash -LiteralPath $helper -Algorithm SHA256 -ErrorAction Stop).Hash -ne $recordedHelperHash) { throw 'Unexpected helper result or changed helper; keep backups.' }
    $helperText = [IO.File]::ReadAllText($helperOutput, [Text.Encoding]::UTF8)
    $helperErrors = [IO.File]::ReadAllText($helperError, [Text.Encoding]::UTF8)
    $initialRows = [regex]::Matches($helperText, '(?m)^Process ID: ([0-9]+)\r?$')
    $observedRows = [regex]::Matches($helperText, '(?m)^Observed process ID: ([0-9]+)\r?$')
    $exitRows = [regex]::Matches($helperText, '(?m)^Process ID: ([0-9]+); exit code: ([0-9]+)\r?$')
    $countRows = [regex]::Matches($helperText, '(?m)^Observed processes: ([0-9]+); expected: ([0-9]+)\r?$')
    $requiredCount = [int]$caseArguments.ExpectedProcesses
    if ($initialRows.Count -ne 1 -or $observedRows.Count -ne $requiredCount - 1 -or $exitRows.Count -ne $requiredCount -or $countRows.Count -ne 1 -or [int]$countRows[0].Groups[1].Value -ne $requiredCount -or [int]$countRows[0].Groups[2].Value -ne $requiredCount) { throw 'Incomplete or extra process evidence; keep backups.' }
    $declaredIds = @($initialRows[0].Groups[1].Value) + @($observedRows | ForEach-Object { $_.Groups[1].Value })
    $exitIds = @($exitRows | ForEach-Object { $_.Groups[1].Value })
    if (@($declaredIds | Select-Object -Unique).Count -ne $requiredCount -or @($exitIds | Select-Object -Unique).Count -ne $requiredCount -or @(Compare-Object $declaredIds $exitIds).Count) { throw 'Native lifetime identities do not match.' }
    if (-not $helperText.Contains('Restored the staged configuration; supplied source resources were unchanged.') -or ($helperText + $helperErrors) -match 'A process could not be retained|forced closure|cleanup could not be verified|backup retained') { throw 'Helper cleanup evidence is incomplete.' }
    if ($expectedHelperExit -eq 0) {
        if ((Test-Path -LiteralPath $helperException) -or @($exitRows | Where-Object { $_.Groups[2].Value -ne '0' }).Count) { throw 'A normal native lifetime failed.' }
        $observedGameExitCode = 0
    } else {
        $expectedException = 'Process evidence is incomplete or includes a failure; do not record the relaunch case as passed.'
        if (-not (Test-Path -LiteralPath $helperException -PathType Leaf) -or [IO.File]::ReadAllText($helperException, [Text.Encoding]::UTF8) -cne $expectedException) { throw 'Helper failure is not the declared negative-case report.' }
        $rootExit = @($exitRows | Where-Object { $_.Groups[1].Value -eq $initialRows[0].Groups[1].Value })
        if ($outcomeCase -eq 'R1') {
            if ($requiredCount -ne 1 -or $rootExit[0].Groups[2].Value -ne '1') { throw 'R1 requires exactly its one native exit 1.' }
            $observedGameExitCode = 1
        } elseif ($outcomeCase -eq 'I1') {
            $childExit = @($exitRows | Where-Object { $_.Groups[1].Value -ne $initialRows[0].Groups[1].Value })
            if ($requiredCount -ne 2 -or $rootExit[0].Groups[2].Value -ne '0' -or $childExit.Count -ne 1 -or $childExit[0].Groups[2].Value -ne '1') { throw 'I1 requires the original exit 0 and observed child exit 1.' }
            $observedGameExitCode = 1
        } else { throw 'Undeclared negative case.' }
    }
} else {
    $observedGameExitCode = $process.ExitCode
    if ($null -eq $observedGameExitCode) { throw 'The native exit code is unavailable; retain backups.' }
    if ($observedGameExitCode -ne 0) { Write-Warning 'The owned game failed. Verified restoration may proceed, but this is not a passing native case.' }
}

if ((Get-FileHash -LiteralPath $exe -Algorithm SHA256 -ErrorAction Stop).Hash -ne $recordedExeHash) { throw 'The candidate changed during this record.' }
if (-not $logBoundaryReady -or (Get-Item -LiteralPath $caseBackup -ErrorAction Stop).CreationTimeUtc -ne $logBoundaryOwner) { throw 'The log boundary belongs to another record.' }
$newLog = ''
if (Test-Path -LiteralPath $logPath -PathType Leaf) {
    $item = Get-Item -LiteralPath $logPath -Force -ErrorAction Stop
    if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -or ($logPresentBefore -and $item.CreationTimeUtc -ne $logCreationBefore)) { throw 'The log was replaced; retain the evidence.' }
    $stream = [IO.File]::OpenRead($logPath)
    $hash = [Security.Cryptography.SHA256]::Create()
    $reader = $null
    try {
        if ($stream.Length -lt $logBeforeLength) { throw 'The log was truncated.' }
        $remaining = $logBeforeLength
        $buffer = [byte[]]::new(65536)
        while ($remaining -gt 0) {
            $read = $stream.Read($buffer, 0, [int][Math]::Min($remaining, $buffer.Length))
            if ($read -le 0) { throw 'The recorded prefix is incomplete.' }
            $null = $hash.TransformBlock($buffer, 0, $read, $buffer, 0)
            $remaining -= $read
        }
        $null = $hash.TransformFinalBlock([byte[]]@(), 0, 0)
        $prefixHash = ([BitConverter]::ToString($hash.Hash)).Replace('-', '')
        if ($logPresentBefore -and $prefixHash -ne $logHashBefore) { throw 'Earlier log bytes changed.' }
        $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8)
        $newLog = $reader.ReadToEnd()
    } finally {
        if ($reader) { $reader.Dispose() } else { $stream.Dispose() }
        $hash.Dispose()
    }
} elseif ($logPresentBefore) { throw 'The original log disappeared.' }
[IO.File]::WriteAllText((Join-Path $caseBackup 'new-log.txt'), $newLog, [Text.UTF8Encoding]::new($false))
if ($launchMode -eq 'OutcomeHelper') { Get-Content -LiteralPath $helperOutput; Get-Content -LiteralPath $helperError }
$process.ExitCode
$gameExitCode = $observedGameExitCode
$newLogProcessId = $process.Id
$launchSucceeded = $true
}
```

Now complete the shared cache route (known-key restoration or Debug qualification restoration), restore the shared settings backup, preserve the record's evidence and use [guarded record cleanup](87-native-procedures.md#complete-a-record). Cache methods are linked rather than copied here. A no-launch partial preparation uses the shared [cache recovery](87-native-procedures.md#preserve-the-selected-minimap-cache) or [qualification-copy abort](87-cache-qualification.md#recover-a-launch-with-no-cache-path), which verify parked and unparked originals. A failed or uncertain launch does not authorize deleting its backups. The helper's own explicit `-Restore` remains separate from this outer record; complete that guarded recovery before enabling outer restoration.

Wait for `Exit code` and `Restored the staged configuration` before the next launch. The helper intentionally discards settings/map preferences saved during this procedure and restores the original output bytes. It does not restore changes made by a concurrent build or another process; keep those stopped. A normal game error still runs restoration. The helper checks for exit every 250 ms so that Ctrl+C can interrupt its wait. Its cleanup attempts to close only its launched process; a forced closure is reported as an aborted run.

If the shell itself was terminated or another MatrixGame process remained, close the game and use the same configuration/compiler/build-directory selection with `-Restore`, without `-Launch`:

```powershell
. {
$ErrorActionPreference = 'Stop'
.\tools\playtest-outcomes.ps1 -Configuration Release -Restore
}
```

The backup is under the selected executable's `CFG/.native-outcome-backup`. A later ordinary inspection or launch refuses to proceed while that backup exists; it does not overwrite independently edited settings. Explicit `-Restore` replaces the current staged settings with the saved originals, so preserve any later edits separately before requesting recovery. Restoration also refuses while a MatrixGame process is running and verifies its record before using it. If the record is incomplete or unexpected files remain, inspect that directory manually; do not rebuild over the backup. Supplied packages at the repository root are never part of this restoration.

Record procedure version, case/launch/step, source revision, binary hash, actual build/profile, map/resource identity, compatibility modes, audio availability, Windows/device/scaling, observable result, timeout or exception, process ID and exit code. Capture only the minimal diagnostic/evidence needed; do not publish packages, extracted assets or configuration dumps. The logger appends to `test.log`. Record its byte length immediately before the launch and preserve only the bytes appended by that completed launch, using the guarded evidence block above; reject truncation or replacement. `Started as EXE` uses the Debug-only `info` level and is not a Release delimiter. Release retains warning/error/fatal and unconditional `notice` entries. The RNG and remembered-map `notice` lines are available in both builds; an empty excerpt does not prove initialization. Match those lines with the retained executable/profile, selected map and visible battlefield; a remembered choice alone is not successful loading evidence. A completed command or code 0 alone does not establish every observation above.

The helper's automated checks use synthetic files and a mocked process. They verify inspection without writes/launch, packed/text selection, exact-byte restoration, absent settings, launch failure, cheats/x86 mismatch, suffix aliases, retained backups with a remaining process and explicit recovery. They also cover MSVC multi-configuration cache/output selection, manifest-before-removal ordering, refusal of an incomplete packed backup, preservation of unexpected backup files, repeated bounded waits and cleanup after a simulated wait exception. They launch no game and do not establish delivery of a real Ctrl+C event or native execution.

## Remaining native preparation

| Case / shared handoff | Ready portion | Missing input or procedure |
| --- | --- | --- |
| NV-05-1: ordinary AUTO versus demo | [Ordinary/demo procedures](85-native-demo.md) prepare same-payload ordinary AUTO gate/toggle, actual demo-name startup, introduction/camera/Escape observations and a following ordinary process | The [six-profile standalone exporter](85-native-demo-export.md) also supplies eligible/no-eligible reinforcement, changing fights and natural Win/Loss with following ordinary-process procedures. Native execution and applicability review remain pending; shared with #84 A8/O9. |
| NV-05-2 | Procedures above, explicit build/launch profiles and restoration helper | Native execution and review; no pending-result or demo coverage is implied. |
| NV-06-1 | Three ordinary-map launches above; toggle/restart extension in NV-05-2 | Native execution and review; weapon/effect-specific native scenes are not manufactured by the movement setup. |
| NV-06-2: both selector origins | [Versioned selector procedures](85-native-selector.md) prepare menu Cancel/resume, Surrender and direct cheat-victory dialog routes, same/different-map acceptance, Exit controls and observed-process monitoring | The [contained lifecycle suite](85-native-exe-lifecycle.md) supplies natural-result triggers, dialogs, selector/relaunch, active-Move Cancel/resume/relaunch and retained failure-process exits with Text/Packed cheats-off setup. Native execution of both profiles, visible presentation and cheats-enabled variants remain pending. Visible different-map cases require the declared owned alternate map and shared resources. |
| NV-06-3: pending result and failure/recovery | [Native failure procedures](85-native-failures.md) prepare guarded failed Restart and Text-only late interface initialization, expected diagnostics, restoration and next-launch recovery; [trigger investigation](85-native-selector.md#nv-06-3-trigger-investigation) separates natural pending results | The [contained lifecycle suite](85-native-exe-lifecycle.md) observes the natural pending counter before Restart/Exit and supplies Text/Packed selected-payload failure/recovery, Text late-interface failure and Packed early-configuration failure setup. Native execution of both profiles, visible R1/I1 execution, Packed late-initialization setup, cheats-enabled variants and complete short-lived tracing through the polling launcher remain open. IAMLOOSER directly opens victory and does not supply the pending state. |
| NV-06-4: mixed special objectives and Terron | [Versioned synthetic exporter and scene procedures](85-native-special-scenes.md) supply exact map/object identities, mixed-objective ordering, collateral expectations, Text/Packed isolation, cache restoration and the following-battle sequence | The [contained outcome suite](85-native-special-outcomes.md) also follows destruction, result callbacks and a following session. Visible execution and review of A/B/C remain pending; contained outcomes do not establish those observations. |
| P3/NV-03 and P4/NV-04 | [Presentation guide](87-presentation.md), [settings verification](../SETTINGS.md#verification) and [map verification](../MAPS.md#verification) | The [prepared P1-P4 procedures](87-native-procedures.md) supply staged failure, actual audio-scene qualification and reset/rollback continuations; native execution and profile applicability remain pending. Their records remain separate from these outcome procedures. |
| NV-07 | [Original host integration](../ORIGINAL_HOST.md) | Supported local x86 host/resource identity and the separately reviewed return/reentry record; standalone restart cannot establish it. |

O10 remains incomplete until all selected cases have usable inputs and procedures. Only execution of prepared cases transfers to native validation; missing preparation stays implementation work.
