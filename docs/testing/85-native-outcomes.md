# Native timing and session procedures

Procedure version: **1**. This handoff prepares NV-05-2 and NV-06-1 from the [outcome completion plan](85-outcome-completion.md). Execution remains pending in the [native validation ledger](native-validation.md). Preparation and synthetic helper tests establish neither a successful native playtest nor complete-baseline acceptance.

## Build and preserve the candidate

Use the pinned Windows x86 toolchain and owned local resources from [Windows builds](../BUILD_WINDOWS.md). The selected scene is `Matrix\Map\desert1_3e.cmap`, the tracked configuration's ordinary starting map. It must be available in the supplied `robots.pkg` or as an equivalent local loose map. This procedure does not extract or distribute it. A missing map, missing friendly robots or no reachable open ground is a setup blocker; do not silently substitute another scene. Record the local resource/map identity without uploading its contents.

Build the cheats-disabled candidate first. Run from the repository root:

```powershell
.\tools\build.ps1 -Configuration Release
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats Off -ConfigurationMode Text
```

The second command only inspects the candidate. It must report the expected build cache, x86 executable, SHA-256, configuration mode, RNG and audio availability. It does not launch or change files. Build immediately before testing: matching cache entries are not proof that an arbitrary old executable matches the checkout. Record the source revision and successful build command alongside the binary hash. The helper rejects DLL, non-x86 and mismatched cheats/configuration candidates.

For the enabled-code checks, finish the disabled candidate's runs, then rebuild and inspect:

```powershell
.\tools\build.ps1 -Configuration Release -Cheats
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats On -ConfigurationMode Text
```

Both builds reuse the same output directory. Finish a run and wait for configuration restoration before rebuilding, changing profiles or starting another game. For Debug, replace `Release` with `Debug` in both commands. For MSVC, add `-Compiler MSVC` to both; the helper accepts its multi-configuration cache and selects the matching `MatrixGame/<Configuration>` executable directory. `-ConfigurationMode Packed` requires a locally supplied `robots.dat` staged by the build; `Text` temporarily hides the output copy without touching the source package. `-RandomGenerator LegacyCRT` selects the second RNG profile; the default is ParkMiller. Do not rename a packed backup to `robots.dat.backup`: resource lookup also matches that name, and the helper rejects such aliases in a text run.

Use the relevant Debug/Packed, Debug/Text, Release/Packed and Release/Text profiles from the ledger, recording each required RNG/audio variant independently. These are prepared variants, not claims that earlier playtests covered them. An unavailable variant stays pending until the maintainer explicitly disposes of it. The helper reports installed audio packages and any `sounds.txt` override; it does not silently remove them to manufacture an audio profile.

## Common launch and scene

Add `-Launch` only when ready to operate a visible game window:

```powershell
.\tools\playtest-outcomes.ps1 -Configuration Release -Cheats Off -ConfigurationMode Text -Launch
```

Use `-Cheats On` for an enabled-code run and the matching build/profile options described above. The helper temporarily sets Windowed 1280x720, camera speed 100%, bonus None, the selected RNG and the current corrected compatibility options. It preserves the original staged `CFG/standalone.txt` bytes, temporarily removes `CFG/robots.dat` only for Text, and restores both after process exit. It launches without arguments: a positional map argument selects visibility calculation rather than interactive play. Root-level supplied files and packages remain unchanged.

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

Wait for `Exit code` and `Restored the staged configuration` before the next launch. The helper intentionally discards settings/map preferences saved during this procedure and restores the original output bytes. It does not restore changes made by a concurrent build or another process; keep those stopped. A normal game error still runs restoration. The helper checks for exit every 250 ms so that Ctrl+C can interrupt its wait. Its cleanup attempts to close only its launched process; a forced closure is reported as an aborted run.

If the shell itself was terminated or another MatrixGame process remained, close the game and use the same configuration/compiler/build-directory selection with `-Restore`, without `-Launch`:

```powershell
.\tools\playtest-outcomes.ps1 -Configuration Release -Restore
```

The backup is under the selected executable's `CFG/.native-outcome-backup`. A later ordinary inspection or launch refuses to proceed while that backup exists; it does not overwrite independently edited settings. Explicit `-Restore` replaces the current staged settings with the saved originals, so preserve any later edits separately before requesting recovery. Restoration also refuses while a MatrixGame process is running and verifies its record before using it. If the record is incomplete or unexpected files remain, inspect that directory manually; do not rebuild over the backup. Supplied packages at the repository root are never part of this restoration.

Record procedure version, case/launch/step, source revision, binary hash, actual build/profile, map/resource identity, compatibility modes, audio availability, Windows/device/scaling, observable result, timeout or exception, process ID and exit code. Capture only the minimal diagnostic/evidence needed; do not publish packages, extracted assets or configuration dumps. The logger appends to `test.log`; identify the latest `Started as EXE` section and preserve the relevant excerpt before another run adds to it. A completed command or code 0 alone does not establish every observation above.

The helper's automated checks use synthetic files and a mocked process. They verify inspection without writes/launch, packed/text selection, exact-byte restoration, absent settings, launch failure, cheats/x86 mismatch, suffix aliases, retained backups with a remaining process and explicit recovery. They also cover MSVC multi-configuration cache/output selection, manifest-before-removal ordering, refusal of an incomplete packed backup, preservation of unexpected backup files, repeated bounded waits and cleanup after a simulated wait exception. They launch no game and do not establish delivery of a real Ctrl+C event or native execution.

## Remaining native preparation

| Case / shared handoff | Ready portion | Missing input or procedure |
| --- | --- | --- |
| NV-05-1: ordinary AUTO versus demo | Existing code/flag observations and the selected inventory | Exact ordinary/demo local map identities, repeatable eligible/no-eligible reinforcement and camera-fight setup, result/Escape continuation and following ordinary launch; shared with #84 A8/O9. |
| NV-05-2 | Procedures above, explicit build/launch profiles and restoration helper | Native execution and review; no pending-result or demo coverage is implied. |
| NV-06-1 | Three ordinary-map launches above; toggle/restart extension in NV-05-2 | Native execution and review; weapon/effect-specific native scenes are not manufactured by the movement setup. |
| NV-06-2: both selector origins | [Versioned selector procedures](85-native-selector.md) prepare menu Cancel/resume, Surrender and direct cheat-victory dialog routes, same/different-map acceptance, Exit controls and observed-process monitoring | Natural win/loss inputs and complete short-lived failure evidence remain missing. Different-map cases require the declared owned alternate map and shared resources. Do not call the whole row ready. |
| NV-06-3: pending result and failure/recovery | Existing initialization-failure checks, [failure guidance](../PLAYTESTING.md#map-arguments-and-failure-cases) and [trigger investigation](85-native-selector.md#nv-06-3-trigger-investigation) | A reviewed reproducible pending-result trigger that leaves time to restart/quit, exact failed-reload/selected-map input, expected diagnostic and restoration, then a successful launch. IAMLOOSER directly opens victory and does not supply the pending state; human reaction speed during a narrow countdown is insufficient. |
| NV-06-4: mixed special objectives and Terron | Synthetic O4 evidence is developed separately | Exact locally available map/object identities, deterministic setup for mixed objective ordering and the collateral blast, expected visible transition and following battle. A generic instruction to win a battle is insufficient. |
| P3/NV-03 and P4/NV-04 | [Presentation guide](87-presentation.md), [settings verification](../SETTINGS.md#verification) and [map verification](../MAPS.md#verification) | Their owners still need to reconcile exact failure/reset/rollback inputs and profile applicability; these procedures do not substitute for them. |
| NV-07 | [Original host integration](../ORIGINAL_HOST.md) | Supported local x86 host/resource identity and the separately reviewed return/reentry record; standalone restart cannot establish it. |

O10 remains incomplete until all selected cases have usable inputs and procedures. Only execution of prepared cases transfers to native validation; missing preparation stays implementation work.
