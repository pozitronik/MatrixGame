# Successful synthetic DLL sessions

This fixture covers the DLL initialization, loop and cleanup portion of O7 in the [outcome completion plan](85-outcome-completion.md). It calls the production `GetRobotInterface()->m_Run` entry, compiled in DLL mode, with a borrowed native Direct3D 9 device and a synthetic host callback table. Initialization, simulation, rendering, result export and teardown are production implementations. The test executable links the DLL-mode engine directly; loading the exported DLL binary and returning to the original game remain separate host-integration checks.

## Scene and host

`synthetic-session-v1` is generated at runtime by [native_session_assets.cpp](../../tests/native_session_assets.cpp). It contains a 32 by 32 flat landscape, sixteen terrain groups, a texture union, one navigation zone/region with four connected places, and two unarmed robots on opposing sides. Module and debris meshes are synthetic animated boxes. Generated images supply the texture and bitmap paths in the repository's tracked text configuration; no original resource packages or extracted assets are used. The pneumatic module includes the foot matrices and animation frames required by production preloading, even though the moving actors use wheels.

The test-only `write_native_session_assets()` materializer writes the resources and `synthetic.map` into the caller's isolated current directory. `write_native_session_map(path)` writes another map with the same scene under a chosen path. Callers own directory cleanup and must invoke generation before starting an engine session. These helpers provide fixture data, not a replacement initializer or host callback implementation.

The host creates an invisible parent window and a child rendering window. The game can show its borrowed child without revealing a window on the desktop. The native device performs real resets, minimap and frame rendering and presentation. The test does not emulate successful graphics calls. Text callbacks return a small owned bitmap and verify its release; audio callbacks report unavailable sounds. These callbacks test the host interface lifetime, not original text appearance or audible playback. The existing `PathToOutputFiles` function is replaced at test link time solely to keep generated cache files inside the fixture directory.

The movement command uses the production side selection and right-button command handlers with a supplied ground trace. This is a session/reentry check, not a new picking or mouse-input reference. The production host tick callback observes the introduction, two live robots, fresh starting coordinates and statistics, dismisses the introduction through the form, submits Move, and waits for more than five world units of actual movement before posting `WM_CLOSE`.

## Checked sequence

`game.session.native_run_lifetimes` runs three lifetimes in one process using the same borrowed window and device:

1. Successful initialization, introduction dismissal, a fresh movement command, movement and normal close. Run returns zero and exports the battle statistics.
2. Initialization with the synthetic interface file temporarily absent. Progress callbacks establish that terrain loading has begun before the error. Run returns 100, does not overwrite the host's result buffer, and cleans up partial state.
3. The interface file is restored. A second successful initialization starts both robots at their original positions, accepts a fresh movement command, advances the actor and returns zero.

After every lifetime the test checks that game/map/configuration/cache/graphics aliases, form lists and live-object lists are empty, the borrowed window still exists and its original ANSI procedure is restored, the device reference count matches its pre-session count, and the host can still clear and present using that device. Debug also checks that the engine allocation tracker is empty. Both configurations keep all checks active.

The fixture records its version, lifetime number, result, movement observation, tick count and the interval containing the DLL's time-based seed. The DLL contract chooses `ParkMiller` and seeds from `time(nullptr)` inside Run; this test does not replace that clock or claim a fixed-seed golden trace. Movement is checked as a bounded invariant. CTest imposes a 30-second process limit, and the host callback rejects 5,000 ticks without completing the journey.

The [packed-configuration and pneumatic cleanup cases](85-native-packed-sessions.md) extend this fixture with isolated packed loading, invalid-header recovery and repeated animation-data retirement. They retain the same borrowed-host ownership checks.

## Running

A usable native Windows Direct3D 9 HAL device and the legacy DirectX runtime are required. The executable is compiled with DLL tests, but native execution is opt-in: ordinary asset-free CI does not assume an interactive graphics adapter. A missing adapter is an explicit failure when the native test is selected, never a successful skip.

From an initialized checkout, build the DLL test targets, enable the native registration, and run the selected check:

```powershell
.\tools\build.ps1 -Configuration Debug -DLL -WithoutResources -TestsOnly
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' -S . -B build/mingw-debug-dll -DMATRIXGAME_NATIVE_SESSION_TESTS=ON
.\tools\test.ps1 -Configuration Debug -DLL -NoBuild -Filter '^game.session.native_run_lifetimes$'
```

Repeat with `Release` and `build/mingw-release-dll`. The registration remains enabled in that build directory until configured with `-DMATRIXGAME_NATIVE_SESSION_TESTS=OFF`. Its `native-process` label permits the documented `ctest -LE '^native-process$'` exclusion even when registration remains enabled. For MSVC, use the corresponding compiler option and build directory from [the Windows build guide](../BUILD_WINDOWS.md); native success on that toolchain must be recorded separately.

A crash after the test host's first progress callback terminates without the engine's interactive exception reporter. Debug allocation leaks have a different path: `CGame::SafeFree` invokes the engine's modal leak report inside `Run`, so such a regression can display a dialog on the current desktop until the 30-second CTest timeout terminates the process. The fixture's final cleanup guard avoids opening a second report when tracked allocations remain after the test returns. The host and child windows are hidden on the current desktop, rather than isolated on a private desktop; select this native check explicitly when that environment is acceptable. No input is injected into the user's desktop. Normal completion removes the generated scene and images; a cleanup error reports the directory and system diagnostic. A forcibly terminated process can leave its `native-session-<pid>` directory in the selected build directory for diagnosis.

## Remaining obligations

`game.session.held_input_retirement` repeats the successful/failing/successful lifecycle while holding F12 through each normal close, with no matching key release. Production `CGame::Deinit` clears the engine's held-key table; the native regression checks both the state after Run returns and the next battle's first callback. It does not synthesize a host focus event or clear input in the fixture. The asset-free `game.lifecycle.held_input_retirement` also checks all 256 entries across repeated partial-state cleanup, with ordinary key release as a control. Run the native regression with the same opt-in registration and a filter of `'^game.session.held_input_retirement$'`.

This slice does not close O7 or #85. Standalone selector/relaunch lifetimes, settings persistence across transitions, full demo startup/continuation, original-host reentry, historical comparison and final same-revision compiler evidence remain their separately tracked obligations. Native Direct3D execution here establishes neither original asset fidelity nor success on a headless CI runner.
