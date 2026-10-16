# Restart failure boundaries and owned-session cleanup

`game.restart.game_owned_cleanup` in [battle_restart_failures.cpp](../../tests/battle_restart_failures.cpp) establishes the owned fixture needed for O6 failure-path work. It allocates the heap, configuration, map, sides, interface and robots with the same ownership contract consumed by `CGame::Deinit`. Unlike the ordinary battle scenario fixture, none of those objects is borrowed stack storage.

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.restart\.game_owned_cleanup$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.restart\.game_owned_cleanup$'
```

The fixture uses a message-only Win32 window with the real `L3G_WndProc`. `CGame::RunGameLoop` enters the real `CFormMatrixGame`, runs the ordinary `L3GRun` and dispatches a queued WM_CLOSE. `Session::cleanup_standalone` then destroys the form and game, clears the cache and graphics aliases, and completes the base cleanup. Assertions check the empty global owners, logic-object list and surrogate-device resource counts. The sequence repeats twice in each RNG mode, checking Debug allocations after every complete lifetime. No original assets or visible game window are used.

The storage/state device supports the form's directional light and texture-unbinding initialization. It still rejects native rendering and device reset. After checking that production cleanup cleared the borrowed device alias, the fixture restores only that alias so the existing surrogate owner can perform its own final reference check and destruction.

This establishes real game-owned teardown, not successful `CGame::Init`, native rendering, a fresh standalone process or a second host DLL session. The two failure probes below are intentionally unregistered and must not be counted as passing baseline contracts.

## Discovered failure paths

Build `matrixgame_battle_restart_failures`, then run either opt-in case directly from its test output directory:

```text
matrixgame_battle_restart_failures.exe probe.restart.missing_file_cleanup
matrixgame_battle_restart_failures.exe probe.restart.corrupt_file_cleanup
```

Both probes queue Escape, R and Enter with `PostMessageW`; the real menu/confirmation callbacks attempt Restart during a pending Move. The missing-file case leaves the active map path absent. The corrupt-file case writes a 12-byte invalid storage header. A queued WM_CLOSE bounds a path that unexpectedly returns normally; faulting probes terminate their own process. Run them separately from passing checks.

On the pinned MinGW x86 toolchain, ordinary Debug and Release runs of the missing-file probe terminate with status `0xc0000409`. The file-open exception reaches `std::terminate` while unwinding from the game window callback through USER32, before `CGame::RunGameLoop` can catch it. The corrupt-header probe terminates with `0xc0000005`: Restart ignores `CStorage::Load` returning false and passes an empty container to `ReloadDynamics`, which dereferences its absent properties buffer.

These are correction prerequisites. Their failures are not accepted gameplay behavior. Converting the corrupt-header access violation to a C++ exception is insufficient until the callback boundary also contains that exception. The probes should become passing regressions only after separately reviewed corrections establish the safe result and cleanup contract. MSVC behavior needs its own evidence; these local observations apply to MinGW.

## Successful initialization remains separate

The O7 fixture must additionally satisfy the real initialization sequence. `CGame::Init` loads configuration and the full fixed robot-module catalog, prepares serialized terrain/movement/group/surface data, initializes interface graphics and hint bitmaps, creates effect and water resources, and opens the introduction. Pneumatic chassis preload requires moving/backward animation frames with both foot attachments. Existing synthetic mesh generation can provide these structures, but its eager cache-loading helpers currently remove the files before initialization could reopen them.

Initialization also requires a native graphics profile: `ApplyVideoParams` queries `IDirect3D9`, resets the device and installs lights; `CTransition::RenderToPrimaryScreen` unconditionally begins a scene and presents. A no-draw surrogate returning success for these operations would not establish the native initialization contract. A future synthetic native-device fixture must use a private desktop or another explicitly bounded invisible host arrangement, retain its generated files for both sessions and verify host callbacks and borrowed graphics references separately.
