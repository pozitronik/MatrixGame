# Native special-object outcomes

These optional native tests extend the [synthetic special-scene handoff](85-native-special-scenes.md) through objective destruction and the DLL result contract. They consume the exported map/configuration format through `CGame::Init`, use a real Direct3D 9 device under an invisible parent window and advance the normal game loop. The post-conversion Terron owner validation correction is part of the current integration baseline. Native execution also needs the successful-presentation-status correction tracked in [Rendering: preserve successful presentation statuses](https://github.com/pozitronik/MatrixGame/issues/270); successful occlusion is not a game-loop failure.

## Input boundary and outcome paths

The fixture selects the already loaded laser robot through the production side selection methods. Enter passes through the form to enter direct control, and left-button down/up passes through the form to fire and stop. Each host callback supplies the resolved world target and clears interface focus, as the existing component input fixtures do. Actual robot aiming, weapon timing, weapon trace, object damage, special-object retirement, side status scans and result dialogs remain production paths. There is no assignment to hit points, objective count, result flags, side status or game time. Screen picking and interface hit testing are outside this boundary.

| Registered case | Decisive sequence | Negative/control observations |
| --- | --- | --- |
| `game.session.native_break_anim` | Fire at red Break, wait, then fire at green Anim. | The first retirement leaves one objective and one building kill; a 2200 ms pause preserves active status without a pending win flag or result. Ordinary blue Break remains intact. Final retirement produces victory and two building kills. |
| `game.session.native_anim_break` | Fire at green Anim, wait, then fire at red Break. | The same intermediate gate and final credit must hold in the reversed order. |
| `game.session.native_terron` | Fire at red Terron until its death sequence starts, release fire and leave direct control. | The shooter remains inside the blast area. The designated near robot dies, the distant robot retains 190 displayed health, and the blue ordinary object becomes Static. The green special Break stays intact with 300 internal health and one objective still registered, while Terron nevertheless produces victory and exactly one building kill. |

Each case runs first with text configuration, then with the equivalent packed configuration. After the real victory dialog, Enter invokes its callback; the subsequent real statistics dialog must appear before a second Enter completes the DLL run. The DLL returns result code 3 and the expected building-kill statistic. No robot kill is credited to the player, including Terron's collateral. A new ordinary synthetic map is then initialized through the same DLL interface and device and exits normally. Its objective/result flags, building credit, direct-control state and fire-button state must start clear, and its returned kill counters remain zero. This is host-controlled following-session initialization, not the standalone selector or EXE relaunch.

The shared `NativeWindowContainment` helper is installed before device creation and DLL initialization. It adapts this test executable's imported window, cursor and diagnostic functions: game windows remain children of an invisible parent without activation, and cursor selection, visibility counts and clipping use fixture-owned state. Required cursor imports must be present or initialization fails. This fixture owns the borrowed `MatrixGame_wc` class and verifies its child, parent and original window procedure survive each run. Window-procedure comparisons use the same ANSI API as the production DLL host path. No global hook, desktop input or visible window is used. Any native diagnostic delivered through the contained message-box imports fails the scenario, including a diagnostic from an otherwise successful run. Each successful lifetime checks balanced text-buffer callbacks, cleared engine globals and logic lists, unchanged device reference count, and reuse of the borrowed device. A successful but occluded `Present` establishes device reuse, without claiming visible pixels.

`game.session.native_special_diagnostic` supplies the diagnostic negative control. During a real ordinary-map run it calls the imported `MessageBoxW` with a unique marker. The helper must capture it without displaying a dialog, and the normal post-run diagnostic check must reject the run with that marker. The exact diagnostic title, text and terminating newline must match one message; an additional diagnostic or unrelated exception cannot satisfy this test.

The normal DLL seed remains time-derived ParkMiller. Native frame lengths are not forced to 10 or 16 ms, and these tests do not replace the deterministic component traces for both RNG modes. Game-time deadlines bound each sequence; the CTest timeout also bounds initialization and paused-dialog failures. Human observation of rendered transitions, screen picking, original resources/audio and the standalone next-battle process remains in NV-06-4. Automated success here must not be recorded as completion of those manual rows.

## Running

Build the DLL-configured tests using the pinned toolchain and enable `MATRIXGAME_NATIVE_SESSION_TESTS=ON`. These cases need a working legacy DirectX runtime and graphics device; an unavailable device fails rather than silently skipping the test. Run native cases serially to avoid unnecessary graphics contention. They retain `native-process` and `current-desktop` labels and `RUN_SERIAL`; the normal default test selection excludes these optional native lifetimes.

```powershell
.\tools\build.ps1 -Configuration Debug -DLL -WithoutMusic -TestsOnly -WithoutResources -Jobs 2
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' -S . -B '.\build\mingw-debug-dll' -DMATRIXGAME_NATIVE_SESSION_TESTS=ON
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' --build '.\build\mingw-debug-dll' --target MatrixGame matrixgame_native_special_fixture --parallel 2
.\tools\setup-directx-runtime.ps1
Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination '.\build\mingw-debug-dll\tests'
.\tools\test.ps1 -Configuration Debug -DLL -WithoutMusic -NoBuild -Jobs 1 -Filter '^game\.session\.native_(special_scenes|break_anim|anim_break|terron|special_diagnostic)$'
```

Repeat with Release, including `release` in the build paths. The initialization case covers six DLL lifetimes; the three outcome cases cover another twelve, followed by one intentionally rejected ordinary-map lifetime in the diagnostic control. Generated inputs live in a unique test-owned directory and are removed after the run; no original package or normal game configuration is changed. The special-scene exporter and its asset-free layout/ownership tests remain available in both EXE and DLL builds; native outcome tests register only for the opted-in DLL role.

After native checks, configure each affected DLL build directory with `-DMATRIXGAME_NATIVE_SESSION_TESTS=OFF` to restore ordinary test registration. Keep the build's cheats and music options matched to the test-helper arguments throughout validation.

The missing-Terron-win-request negative control disables only that production status transition: the real death sequence still starts, but the registered Terron test fails its result deadline. Use this only as a temporary local mutation and restore the production source before accepting passing evidence. This guide defines the control; recorded run results identify which revision and profiles were actually checked.
