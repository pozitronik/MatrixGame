# Native standalone lifecycle checks

The `game.session.exe_*` cases link the production `WinMain` into a separate test executable and run its startup menu, battlefield initialization, message loop, selectors, cleanup and map-switch subprocesses. They use generated resources from [native_session_assets.cpp](../../tests/native_session_assets.cpp); no original game package, extracted map or user configuration is read. These checks complement the deterministic callback and map-loop fixtures in [the outcome completion plan](85-outcome-completion.md).

## Running the checks

A Windows x86 standalone build, the x86 D3DX9 runtime and a working Direct3D 9 HAL device are required. The graphics checks are opt-in because ordinary CI workers need not provide that device. A missing device or a failed initialization fails the test; there is no visible-window fallback and no silent skip.

Configure the usual Debug and Release standalone test builds with the [build helper](../BUILD_WINDOWS.md). From the repository root, enable the native cases in those existing build directories, build their targets and run them serially:

```powershell
$cmake = '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe'
foreach ($configuration in @('Debug', 'Release')) {
    $build = "build/mingw-$($configuration.ToLowerInvariant())-exe"
    & $cmake -S . -B $build -DMATRIXGAME_NATIVE_EXE_TESTS=ON
    if ($LASTEXITCODE -ne 0) { throw 'Native test configuration failed.' }
    & $cmake --build $build --target matrixgame_standalone_session_tests --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Native test build failed.' }
    .\tools\test.ps1 -Configuration $configuration -NoBuild -Jobs 1 -Filter '^game\.session\.exe_'
    if ($LASTEXITCODE -ne 0) { throw 'Native executable lifecycle check failed.' }
}
```

The test helper stages the verified D3DX9 runtime. Use builds with cheats disabled for this suite; no developer command is used. The fixtures load text configuration, use windowed mode and disable shadows. General cases render at 640x480; active-order cases use 1280x720 to keep the destination clear of the HUD. The general lifecycle cases use two unarmed robots on flat generated terrain; the natural-result cases below arm one side. Both installed map entries initially contain the same scene under different filenames. The empty synthetic `robots.pkg` satisfies the standalone package preflight; all actual images, models and map records are generated loose files. The test deployment lives below CTest's working directory and is removed on normal completion or failure. Source configuration and supplied resources remain untouched.

The normal build still compiles the controller and probe when the option is off. Each optional case has the `native-process` and `current-desktop` labels and runs serially within its CTest invocation; do not run separate native suites concurrently. To remove these native cases from subsequent CTest selections, reconfigure each build with `-DMATRIXGAME_NATIVE_EXE_TESTS=OFF`.

## Cases and assertions

Every name below has the prefix `game.session.exe_`.

| Case | Production route and observed result |
| --- | --- |
| `startup_selector_close` | WM_CLOSE while the startup selector is open exits with code 0, without initializing a map, creating a cache or starting a child. |
| `startup_cancel` | N opens the startup selector; Escape returns to the menu before a battlefield exists; a second Escape exits with code 0. Exactly one process. |
| `startup_battle` | Enter starts the first map, two live robots and the introductory dialog are observed, and simulation advances after confirmation. The battle menu's selector cancels back to the paused menu; Continue resumes the same battle. Closing the window exits with code 0. |
| `same_map` | A running battle selects its current map again. The old process exits with code 0; a distinct child loads that map, completes the loading handshake, advances simulation after introduction confirmation and exits with code 0. |
| `different_map` | The same path selects the second installed map. The child reports the second map's actual loaded filename. Both process exits and child readiness are checked. |
| `settings_relaunch` | Keyboard input in the real Settings window saves camera speed 125%, Low skybox and LegacyCRT RNG. Camera speed changes immediately; skybox and RNG retain their current values and report a pending restart. The selected-map child applies all saved values during its new initialization and clears the pending flag. Persisted settings are also read back from disk. |
| `failed_child_recovery` | After opening the battle selector, the fixture temporarily renames only its generated `iface.txt`. The selected-map child reports that missing file and exits with code 1; the old parent exits with code 0. The failed remembered map is cleared. Restoring the generated file permits a fresh startup, battle and exit with code 0. |
| `surrender_close` | WM_CLOSE in the battle-end selector after Surrender statistics exits with code 0 and creates no child. |
| `surrender_cancel` | Surrender confirmation reaches statistics with internal exit state 4; confirming statistics opens the battle-end selector. Cancel terminates the old process without a child. |
| `surrender_next` | The same Surrender route accepts the second map, producing a ready child with two live robots, advancing simulation and successful exits. |
| `menu_exit` | Exit confirmation reaches statistics with internal exit state 1. Confirming statistics terminates the process with code 0; there is no selector interaction or child. This distinguishes Exit from Surrender. |
| `unreadable_map_rejected` | After discovery, the second map's generated storage header becomes invalid. Accepting the listed file leaves the selector open and the old battle paused, without a child. Restoring the file permits a ready second-map child, advancing simulation and successful exits. |
| `failed_map_recovery` | After discovery, the second map retains a valid storage container and property tables but loses its mandatory width property. The catalog accepts it; `PrepareMap` rejects it before terrain allocation. The child reports `Unable to load map`, exits with code 1 and clears the remembered map. Restoring the original bytes permits a fresh process to load that second map and exit successfully. |

Each awaited transition has a 20-second wall-clock deadline, and CTest limits each case to 90 seconds. These are native operating-system and graphics-driver journeys, not fixed 10/16 ms simulation schedules. Production `WinMain` chooses its usual time-based seed; the assertions do not depend on a random draw sequence. The settings case checks both running RNG implementations across initialization without claiming deterministic combat coverage.

## Active orders through selection and relaunch

The four `active_order_{same|different}_{park_miller|legacy_crt}` cases use a 1280x720 window so that the real HUD leaves the target terrain exposed. The controller posts the next-robot key (`.`), `M`, mouse movement and a left click. It projects an open-ground destination through the actual camera, converts it to client coordinates, and requires both a landscape hit and no interface focus before clicking. The accepted `ROT_MOVE_TO` and actual robot displacement establish that the command reached the normal input, picking and movement paths.

Opening the battle menu clears the current selection while preserving the running Move order. The selector remains paused for at least 300 milliseconds of wall time. Cancel must retain that menu state, simulation time, robot position and exact order coordinates; it must preserve the saved map and bonus, leave relaunch unrequested and create no child. Continue resumes displacement toward the same destination. Reopening the selector and accepting the same or different map must create a ready child with the chosen map and RNG, two robots at their initial positions, no inherited selection or Move order, and no pending result. After introduction confirmation, the child accepts a fresh Move and exits with code 0 while that order is active. The parent also exits with code 0.

Omitting the left-button-down message in a temporary controller negative control must fail at accepted Move and actual displacement: terrain aiming alone is insufficient. These cases use text configuration with cheats disabled. Each has a 90-second CTest limit. They cover posted input within the contained client; physical pointer tracking is outside this fixture's contract.

## Natural results and pending-result interruption

The additional natural-result cases use the same generated assets, with two ordinary Monostack robots placed 100 world units apart. Only the intended winner has a laser; the other robot remains unarmed. The map writer changes the serialized scene before initialization. It does not change weapon damage, health, simulation speed or live battle state. Each case observes the expected robot and weapon counts, a real reduction in the victim's health and its eventual removal through production combat. Both `park_miller` and `legacy_crt` suffixes select and verify the configured RNG implementation.

| Case before the RNG suffix | Result and continuation |
| --- | --- |
| `natural_win`, `natural_loss` | Real combat leaves one side alive, the normal delayed Win or Loose dialog appears, confirmation reaches statistics with exit state 3 or 2, and statistics opens the selector. Win chooses the different map; Loss chooses the same map. Each new process loads two full-health robots, retains the configured RNG, completes the real presentation handshake and advances simulation; both processes exit with code 0. Surrender supplies the separate battle-end Cancel route. |
| `natural_win_restart`, `natural_loss_restart` | While the genuine result is pending, Pause freezes the simulation for at least 1.2 seconds of wall time without opening the result dialog. Escape, R and Enter use the ordinary restart confirmation. The same process reloads both full-health robots, clears the pending countdown and retains its status-scan timestamp. It then advances past the observed old result deadline and completes a new status scan without displaying the old result. |
| `natural_win_exit`, `natural_loss_exit` | The same pending-result pause leads through Escape, E and Enter to statistics with exit state 1. Confirming statistics exits with code 0 and creates no child. |

Pending result means that no dialog is open and the production result countdown is 1. The observer reads the countdown and the side-status scan timestamp through test-only derived-class member-pointer accessors; it applies those pointers to the genuine map object without downcasting or changing production visibility. The normal scan checks `time - timestamp > 1001`, then stores `time + 1001`, so successive scans are more than 2,002 simulation milliseconds apart. The pending observation records the old result deadline as `timestamp + 1001`. Restart must preserve the timestamp and reset the countdown to 0; advancing past that exact deadline must produce a newer scan timestamp and leave the countdown at 0. Pending, restart and continuation values are printed in the test output. Omitting the countdown reset in a temporary negative-control build must fail this case.

All observations run on the game thread; the controller then posts ordinary Pause input. No live state is changed by the observer and no immediate-result cheat is used. This avoids a human having to act during a short countdown.

Natural combat has a 60-second wall-clock deadline; the other transitions retain their 20-second deadlines. CTest limits each natural-result case to 150 seconds. These assertions describe outcomes and lifetime ordering, rather than an exact native frame schedule or random draw trace. The tests use text configuration with cheats disabled; they do not establish equivalent packed-configuration or cheats-enabled coverage.

For the pending-result portion of NV-06-3, the exact automated procedure is to prepare the two native builds above, then run `tools/test.ps1 -Configuration Debug -NoBuild -Jobs 1 -Filter '^game\.session\.exe_natural_(win|loss)_(restart|exit)_'` and repeat with `Release`. This selects eight cases per build. A pass requires all eight, no timeout, and exit code 0 from the helper. The console failure report retains observed health, simulation time, native diagnostics and process exits. The controller prepares and removes its own synthetic maps, configuration and processes; no manual resource substitution or timing-sensitive keyboard action is required. This procedure supplies the pending-result trigger and execution evidence under the stated containment limits; visible result presentation remains a separate observation.

## Containment and observations

The controller starts the probe suspended, assigns it to a kill-on-close Windows job and resumes it under `DEBUG_PROCESS`. Windows debug events enumerate descendants and retain their exit codes even if a failed child is short-lived. Assertions require the expected process count and the new battle's observations; a successful old parent alone never establishes success. A successful exit also requires no reported diagnostic, including one emitted during final cleanup after the window has gone. Deliberate initialization failures instead require exit code 1 and their exact diagnostic count and text. Closing the job terminates any remaining owned descendants on failure, and the controller drains bounded cleanup events before deleting the deployment.

The probe cannot run by itself: it requires the controller's named shared-memory protocol before entering production startup. Its containment adapter changes only its own USER32 import slots and one SHELL32 folder lookup. `CreateWindowExA/W` places the game and loading-window classes beneath a permanently invisible parent and adds `WS_EX_NOACTIVATE`; `SetWindowLongA/W` preserves child containment when production code changes window styles. Window-procedure changes pass through unchanged. `ShowWindow` verifies containment before allowing a nonactivating show. `ClipCursor` is a no-op so the test cannot alter desktop confinement. `TrackMouseEvent` accepts only the contained game window's `TME_LEAVE` request without registering physical pointer tracking: otherwise the real desktop cursor, which is outside this invisible child, would immediately cancel posted movement. Other tracking requests pass through; explicitly posted `WM_MOUSELEAVE` still reaches the unchanged window procedure. Imported `SetCursor` and `ShowCursor` use test-local presentation state: setting a cursor returns the previous opaque handle, and each thread maintains its own display counter. They make no USER32 cursor calls. The always-registered `game.session.cursor_previous_handle`, `cursor_display_balance` and `cursor_thread_isolation` contracts check those return values, balancing loops and thread isolation without a graphics device or native cursor access. Cursor shape and visibility are excluded presentation properties; the Direct3D device still belongs to the invisible window. `MessageBoxA/W` records an OK-only diagnostic and returns OK; another dialog button contract terminates the probe instead of opening an unexpected prompt.

Before production startup, the adapter also intercepts `SHGetSpecialFolderLocation(CSIDL_PERSONAL)` and reports that Documents is unavailable. Other folder queries pass through. Production `PathToOutputFiles` then uses its existing relative-path fallback, placing minimap caches and any requested screenshots below the disposable executable directory. The tests assert that loaded maps produce cache files there and that startup cancellation does not. This covers the fallback output path; normal Documents resolution is not tested.

The actual game window procedure and message loop receive posted keyboard, mouse, activation and close messages directed only to a window owned by the recorded process. No desktop input is synthesized. Thread-local message hooks observe the current form, loaded map, simulation clock, dialog, settings and live robot count on the game thread. Numbered requests and sequence-guarded snapshots distinguish a completed observation from a stale or partially written response. The observer does not mutate gameplay state.

For a successful relaunch, the child must have a live initialized scene, have recognized the selected-battle launch arguments, clear its parent-handshake handle after the real successful `Present`, and subsequently advance simulation. The failure case separately checks the diagnostic and exit code, because the production failure handler also signals the parent event. Suppressing `BattleLoading::presented` in a temporary negative-control build must make the successful relaunch case time out at the readiness assertion.

## Coverage limits

The permanently invisible parent deliberately changes window presentation. The suite does not prove visible startup, loading-screen continuity, focus/minimization, cursor confinement, fullscreen transitions, text readability or a driver's visible output. In particular, production `BattleLoading::show` does not create its visible loading screen for the contained window. Its child-process readiness handshake is exercised, while the loading-screen display and Escape-to-hide behavior remain native presentation checks.

Packed configuration, cheats-enabled behavior and a host's DLL lifetime require their own coverage. These cases do not replace those requirements or complete the whole-game baseline. Their accepted execution and remaining native presentation requirements belong in the [native validation ledger](native-validation.md).
