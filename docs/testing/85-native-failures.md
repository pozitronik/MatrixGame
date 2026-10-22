# Native reload and selected-map failure procedures

These cases prepare the failed reload and late selected-map initialization portions of NV-06-3. Restart/Quit while a natural result is pending has separate automated setup in the [contained standalone lifecycle suite](85-native-exe-lifecycle.md). Preparation of R1 and I1 is complete for the profiles declared below; their execution and review belong to [native validation #191](https://github.com/pozitronik/MatrixGame/issues/191). No native execution is recorded here, and the remaining NV-06-3 variants stay with implementation #85.

## Candidate and source requirements

Use the [native outcome build and identity procedure](85-native-outcomes.md#build-and-preserve-the-candidate). R1 requires the integrated restart-storage guard in `CMatrixMap::Restart`: a false `CStorage::Load` result must throw `Invalid restart map storage: <map>`. Do not run R1 on an older binary that ignores that result. Build the current candidate containing that correction; keep the source revision and executable hash with the report. I1 exercises the existing text-interface initialization boundary and does not require malformed map storage.

Supply an owned, already extracted ordinary playable map and its shared resources. An original `desert2_2e.cmap` is a suitable input. A storage header alone cannot establish that an arbitrary map is playable: first load the selected copy successfully as required below. Neither script extracts or distributes a map. From a PowerShell session in the repository root, set:

```powershell
$source = Read-Host 'Full path to your owned, playable ordinary .cmap'
```

The launcher stages that input as `Matrix/Map/native-outcome-alternate.cmap`, refuses an existing file at that reserved output path and reports the source SHA-256. The fault helper requires `-SourceMapFile`, matches the complete copied file against that source and refuses to use the copy itself as its source. Fault injection changes the staged copy or text interface, updates the recovery record `CFG/.native-outcome-backup/state.json` and creates the declared temporary map or interface backup in that directory. It validates the complete playtest restoration record and required backups before applying a fault. For a header fault, it prepares a separate temporary copy and replaces the reserved map entry, preserving the source even if an unexpected hard link exists. An existing `CFG/.native-outcome-backup/restart-header.tmp` is never replaced; inspect an abandoned temporary manually before continuing. It never edits the supplied map or any package.

Keep two PowerShell windows open in the repository root: A runs `playtest-outcomes.ps1` and reports processes; B runs `set-outcome-fault.ps1`. Both windows must use the same configuration, compiler/build-directory overrides and source path. Assign `$source` in both. Commands below show Debug with cheats disabled. Run the same case separately with Release by changing `-Configuration` in **both** windows. For the enabled profile, build again with `tools/build.ps1 -Configuration <configuration> -Cheats` and pass `-Cheats On` to the launch commands. Use no developer code during these failure cases. The launcher rejects a cheats/cache mismatch.

R1 supports Text and Packed when the candidate has the declared configuration resources; pass the same `-ConfigurationMode` throughout one profile. I1 is deliberately **Text only**: packed configuration restores interface data from `CFG/robots.dat`, so hiding the text file would not exercise the selected failure boundary. A packed-profile late-initialization failure remains a separate preparation gap.

The fault script inspects by default. `-Apply` requires exactly one running process from this candidate executable. Do not apply a fault until the first battle is ready. The launcher may close only the original process it started when interrupted. It never terminates polled same-path processes: close any remaining game yourself and use explicit recovery. Once a fault is active, do not rebuild, launch a second copy, edit the staged settings or manually replace the staged files. Close the running case and let its launcher restore them first. If the launcher is interrupted, close every game process and use its explicit `-Restore` command; ordinary inspection/launch refuses a leftover backup without replacing later independent edits. Preserve such edits before explicitly restoring.

## R1: failed Restart followed by a successful launch

In window A:

```powershell
.\tools\playtest-outcomes.ps1 -Configuration Debug -Cheats Off -ConfigurationMode Text -AlternateMapFile $source -WatchRelaunches -ExpectedProcesses 1 -Launch
```

At the startup menu choose New battle, select **native-outcome-alternate**, bonus None and Start. Dismiss the introduction. Select a friendly robot, press `M`, click reachable ground and confirm movement. This successful first load establishes the supplied map's compatibility with this resource set. Press Escape to pause while a Move remains active.

In window B inspect, then apply:

```powershell
.\tools\set-outcome-fault.ps1 -Configuration Debug -SourceMapFile $source -Fault RestartHeader
.\tools\set-outcome-fault.ps1 -Configuration Debug -SourceMapFile $source -Fault RestartHeader -Apply
```

Confirm the source hash matches the launcher and record the faulted-copy hash. The change replaces only the copied storage tag with four zero bytes; `CStorage::Load` returns false before reading a version, compressed payload or table. Return to the paused battle menu, press `R` and confirm Restart with Enter. Within 30 seconds expect an exception dialog containing `Invalid restart map storage:` and `Matrix\Map\native-outcome-alternate.cmap`. Source-file line numbers and stack text may differ by build. Capture the diagnostic before acknowledging it. Restart has already cleared selection references, dynamics, effects and statistics before attempting this load. The exception crosses the contained window callback and returns to the standalone `WinMain` catch through `L3GRun`; standalone cleanup follows. The battle must not resume in a partially reset state, create a selector or launch a child. This procedure observes the native EXE boundary, not the DLL `RunGameLoop` error result 100.

After acknowledging the dialog, window A must report exactly the original process and exit code **1**. Its relaunch monitor intentionally reports an observed failure and returns an error; that is expected for this injected-failure launch, not an unexplained test failure. Missing exit evidence, a crash code, another process, a different diagnostic or a hang is a failure of this case. Require completion/restoration within 30 seconds after acknowledgement. Verify the helper reports restoration and its `.native-outcome-backup` directory is gone.

Run the same launch command again, without applying a fault. Select **native-outcome-alternate** again, dismiss the introduction and repeat a fresh Move. Confirm a playable battlefield within 60 seconds. Use Escape, `E`, Enter, inspect statistics, then Enter to exit. This second launch must report one process with exit code **0** and normal restoration. The untouched source is copied anew; do not repair the intentionally damaged copy by hand. Record both binary identities, the expected diagnostic, the failed launch's code 1 and the recovery launch's code 0.

## I1: late text-interface initialization failure and recovery

In window A use the Text profile and expect two processes:

```powershell
.\tools\playtest-outcomes.ps1 -Configuration Debug -Cheats Off -ConfigurationMode Text -AlternateMapFile $source -WatchRelaunches -ExpectedProcesses 2 -Launch
```

First load **native-outcome-alternate**, bonus None, dismiss the introduction and confirm a fresh Move. Press Escape to pause. This preflight proves that the complete map and current interface/resources can initialize before any fault is applied.

In window B:

```powershell
.\tools\set-outcome-fault.ps1 -Configuration Debug -SourceMapFile $source -Fault MissingInterface
.\tools\set-outcome-fault.ps1 -Configuration Debug -SourceMapFile $source -Fault MissingInterface -Apply
```

The helper backs up only the staged `CFG/robots/iface.txt`, records restoration before removing it and leaves the validated map intact. If the file was already absent, the helper refuses the fault and leaves that absence unchanged; this is a setup failure, not a successful I1 run. Packed configuration or `robots.dat.*` aliases are rejected. If this resource set supplies an unexpected package fallback for that same interface path and no diagnostic appears, record an unsuitable setup rather than claiming failure-path coverage.

Return to the battle menu, press `N`, select **native-outcome-alternate**, keep None and accept Start. The selector's storage/properties check must accept the unchanged map. The original process exits and the new process attempts full battlefield initialization. Within 60 seconds expect the new process's exception dialog containing `Error open file: cfg\robots\iface.txt`; letter case and source/stack details may vary. The dialog should also say the chosen map could not be started and was forgotten. Do not accept an early selector validation error as equivalent: no child initialization occurred in that case.

**Keep the exception dialog open until window A prints the new `Observed process ID`.** Note the original and child PIDs. If no child PID is reported within 30 seconds, the process evidence is incomplete; preserve the diagnostic and treat the run as incomplete. Only after observing the child, acknowledge the dialog. The original process must have exited with **0**, and the failing child with **1**. No playable replacement battle or third process is expected. The helper reports an error for this intentional child failure and then restores the staged interface and configuration.

An observed count of one, a parent-only code 0 or an unavailable child exit is not a passing result. Retaining the visible exception until observation makes this particular path inspectable; it does not turn the polling monitor into proof that every short-lived failure in other cases was observed. Require exit/restoration within 30 seconds after acknowledgement. Keep the game log's map-loading/error excerpt with the two PIDs to distinguish the late initialization boundary from selector rejection. The launcher's restored-interface SHA-256 must match the fault helper's original-interface SHA-256.

Finally launch again with `-ExpectedProcesses 1`, the same configuration and the same source. Select the copied map, dismiss its introduction, confirm a fresh Move and exit through menu Exit/statistics. Require a playable battlefield within 60 seconds, one observed code-0 exit and normal restoration. The launcher restores the previous standalone settings after each run, so this recovery does not prove persistence of the failed-map forgetting across an unmodified ordinary launch; I1 covers its diagnostic and subsequent successful initialization only.

## Evidence and remaining scope

For each run record configuration/compiler, cheats/cache state, Text/Packed profile, source revision, binary and source-map hashes, case ID, fault action, map readiness before injection, exact diagnostic, every observed PID/exit, restoration and recovery Move. A planned code-1 failure is successful only when the declared diagnostic and subsequent code-0 recovery both occur. The synthetic helper contracts cover exact source preservation, header replacement, read-only inspection, required backups and manifest fields, pre-existing temporary and unclaimed-backup protection, failed replacement cleanup, MSVC output layout and exact-byte interface recovery. Neither the fault helper nor its mocked-process tests establish native gameplay results.

The contained lifecycle suite supplies synthetic natural-result scenes, observes the armed counter before Restart/Exit, and retains complete child exits for its selected-payload and late-interface failures with successful recovery. `IAMLOOSER` opens a dialog synchronously and is not that trigger. Those automated Text/cheats-off cases do not establish the visible R1/I1 observations. Packed-profile late initialization, other dependency failures and complete short-lived failure tracing through the visible polling launcher remain outside these procedures; keep their declared implementation or execution obligations open.
