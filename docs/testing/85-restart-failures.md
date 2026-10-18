# Restart failure boundaries and owned-session cleanup

`game.restart.game_owned_cleanup` in [battle_restart_failures.cpp](../../tests/battle_restart_failures.cpp) establishes the owned fixture needed for O6 failure-path work. It allocates the heap, configuration, map, sides, interface and robots with the same ownership contract consumed by `CGame::Deinit`. Unlike the ordinary battle scenario fixture, none of those objects is borrowed stack storage.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.restart\.game_owned_cleanup$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.restart\.game_owned_cleanup$'
```

The fixture uses a message-only Win32 window with the real `L3G_WndProc`. `CGame::RunGameLoop` enters the real `CFormMatrixGame`, runs the ordinary `L3GRun` and dispatches a queued WM_CLOSE. `Session::cleanup_standalone` then destroys the form and game, clears the cache and graphics aliases, and completes the base cleanup. Assertions check the empty global owners, logic-object list and surrogate-device resource counts. The sequence repeats twice in each RNG mode, checking Debug allocations after every complete lifetime. No original assets or visible game window are used.

The storage/state device supports the form's directional light and texture-unbinding initialization. It still rejects native rendering; this fixture leaves device reset unscripted, so Reset is rejected too. The fixture records whether production cleared the borrowed device alias, restores the fixture-owned alias, and only then asserts the recorded cleanup state. This preserves the alias assertion's diagnostic during unwinding through the surrogate owner; it does not intercept the production leak reports described below. Destructor fallback cleanup makes no assertions and contains cleanup exceptions. Each setup initializes skin state as production `static_init` does.

This establishes real game-owned teardown, not successful `CGame::Init`, native rendering, a fresh standalone process or a second host DLL session. The missing-file failure is now a registered regression; corrupt storage remains an unregistered defect probe.

## Discovered failure paths

The test suppresses the engine's exception reporter, but production Debug cleanup has a separate modal allocation-leak report inside `BaseDeInit`. A leak regression can therefore show that report on the current desktop and wait for the CTest timeout. The final test-process allocation guard cannot intercept a leak report already raised inside a lifetime. This limitation is distinct from the fixture's no-draw device and message-only window; it does not establish unattended leak diagnostics on a private desktop.


Build `matrixgame_battle_restart_failures`, then run the registered missing-file case or the opt-in corrupt-file probe directly from its test output directory:

```text
matrixgame_battle_restart_failures.exe game.restart.missing_file_cleanup
matrixgame_battle_restart_failures.exe probe.restart.corrupt_file_cleanup
```

Both cases queue Escape, R and Enter with `PostMessageW`; the real menu/confirmation callbacks attempt Restart during a pending Move. The missing-file case leaves the active map path absent. The corrupt-file case writes a 12-byte invalid storage header. A queued WM_CLOSE bounds a path that unexpectedly returns normally; faulting probes terminate their own process. Run the corrupt-file probe separately from passing checks.

Before callback containment, ordinary Debug and Release runs of the missing-file case terminate with status `0xc0000409` on the pinned MinGW x86 toolchain. The file-open exception reaches `std::terminate` while unwinding from the game window callback through USER32, before `CGame::RunGameLoop` can catch it. The corrupt-header probe terminates with `0xc0000005`: Restart ignores `CStorage::Load` returning false and passes an empty container to `ReloadDynamics`, which dereferences its absent properties buffer.

`L3G_WndProc` now retains a caught exception and suppresses subsequent game input until graphics/session teardown. `L3GRun` rethrows the original exception after native message dispatch has returned, so the existing `RunGameLoop` catch produces result 100. It checks sent-message delivery during `PeekMessage`, errors pending before loop entry and synchronous callbacks during simulation/drawing. `L3GDeinit` clears both the pending exception and input suppression for the next lifetime. Each missing-file regression completes real cleanup four times in each configuration. `game.restart.missing_file_mouse_cleanup` opens the same confirmation, supplies synthetic two-pixel button geometry and focus, and queues real Win32 left-button press/release messages. Production hit testing, texture-alpha lookup, button actions, the Restart callback and loop recovery remain active; no native cursor input or visible window is used.

`game.restart.callback_before_loop` and `game.restart.callback_during_tick` use a bounded form that throws an identifiable exception through a synchronous `SendMessageW`. They require the same exception diagnostic at the C++ loop boundary, no later input and no draw after failure. `game.restart.callback_abandoned` leaves the exception pending until teardown and verifies that the next lifetime receives input and closes normally. These timing checks exercise generic callback containment; the missing-file regression additionally executes the actual game confirmation and Restart.

Corrupt storage remains a separate correction prerequisite. Its access violation is not a C++ exception and is not intercepted by this change. The failed probe is not accepted gameplay behavior. MSVC behavior needs its own evidence; the observed pre-correction process failures above apply to MinGW.

## Successful initialization remains separate

The O7 fixture must additionally satisfy the real initialization sequence. `CGame::Init` loads configuration and the full fixed robot-module catalog, prepares serialized terrain/movement/group/surface data, initializes interface graphics and hint bitmaps, creates effect and water resources, and opens the introduction. Pneumatic chassis preload requires moving/backward animation frames with both foot attachments. Existing synthetic mesh generation can provide these structures, but its eager cache-loading helpers currently remove the files before initialization could reopen them.

Initialization also requires a native graphics profile: `ApplyVideoParams` queries `IDirect3D9`, resets the device and installs lights; `CTransition::RenderToPrimaryScreen` unconditionally begins a scene and presents. A no-draw surrogate returning success for these operations would not establish the native initialization contract. A future synthetic native-device fixture must use a private desktop or another explicitly bounded invisible host arrangement, retain its generated files for both sessions and verify host callbacks and borrowed graphics references separately.
