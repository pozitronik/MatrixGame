# Autonomous combat in a loaded demo

The six `game.session.native_demo_(packed_)?natural_*` checks extend the [loaded-demo AI continuation](85-native-demo-ai.md) with autonomous weapons and natural result scheduling. They load synthetic maps through the actual DLL-mode `Run` and `CGame::Init`, with the native Direct3D loop and the filename-driven automatic/flying-camera/full-auto flags. They do not submit Damage, delete actors or assign result state from the host callback.

## Win and loss

`natural_win` and `natural_loss` use the shared native outcome map: one player and one opposing wheel-chassis robot at (250, 250) and (350, 250). Only the intended winner carries a laser. The resource declaration sets laser damage to `1000,0,0`; the running simulation retains ordinary perception, planning, firing, weapon traces, robot damage and death handling.

The host records the losing robot's starting health before any observed damage. It must observe reduced health while that actor is still alive, then exactly one surviving robot, a missing live victim and one credited kill for the winner. Natural Win sets the map's win flag; natural Loss leaves that flag clear and retires the player side. These underlying outcomes must occur before the loop's exit request. The full-auto result handling subsequently returns result 1 for either outcome. A 20,000 ms game-time bound rejects a battle that never reaches that observation; no fallback close manufactures a successful result.

The exported player result must contain one kill for Win and zero for Loss. Both continue through a deliberately failed initialization returning 100 and a fresh ordinary battle returning zero after an accepted movement order. Text and isolated Packed cases use the same declarations and ownership checks.

## Changing natural fights

`natural_fights` loads a stationary friendly laser attacker, a nearby unarmed opponent and a more durable second opponent. The generated resource declaration sets wheel movement speed and laser heat to zero, armor-6 structure to 30,000 and armor-2 structure to 200,000. Those values define this scene before loading; no live health or order is rewritten by the observer.

Production AI must damage the first target while the flying camera retains its fight reference and approaches the first midpoint within 32 world units. The weapon must naturally kill that target, credit one player kill and start damaging the second opponent. The camera must then approach the surviving pair's midpoint within two units while retaining that target; the first target's core must have no object and only its test observation reference. A 25,000 ms game-time bound applies. The observer sends Escape only after those conditions have been established, releases its own core references before game teardown and requires result 1. Failure paths also release observation references before the owning heap retires.

This case distinguishes an incidental general flyover from a retained fight by checking both camera position and object-core references alongside actual health changes. The [component-fed camera journey](85-native-demo-ai.md) remains the tighter pending-pair expiration/replacement check. Native combat uses a time-derived seed and variable frame intervals, so its event timing and total RNG count are bounded observations rather than a fixed-step golden trace.

## Running and evidence

The ordinary `game.session.demo_assets` check parses all six AI/combat declarations in Text and Packed form, including complete robot columns, side arrays `{1, 2}` or `{1, 2, 2}`, the single armed side, stationary/nonlethal component-scene configuration, natural-fight heat and armor structure values, and the third fight robot's Diploid armor. It preserves the factory-column, transport-model and reinforcement-order checks of the loaded-demo AI suite. It requires no window or device and runs in default DLL test configurations.

Run the complete fenced block as one statement, including `. {` and the final `}`. It stops on a failed preparation/configure/build/test step and restores ordinary native registration in `finally`, then rebuilds the DLL and fixture even after failure. A failed restoration leaves the build unverified; do not continue to ordinary tests. Finish each native process before changing build options.

```powershell
. {
$ErrorActionPreference = 'Stop'
$configuration = 'Debug'
$cheats = $false
$build = "build/mingw-$($configuration.ToLowerInvariant())-dll"
$cmake = '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe'
try {
    .\tools\build.ps1 -Configuration $configuration -DLL -WithoutMusic -TestsOnly -WithoutResources -Jobs 2 -Cheats:$cheats
    if ($LASTEXITCODE -ne 0) { throw 'Native test build preparation failed.' }
    & $cmake -S . -B $build -DMATRIXGAME_NATIVE_SESSION_TESTS=ON
    if ($LASTEXITCODE -ne 0) { throw 'Native test registration failed.' }
    & $cmake --build $build --target MatrixGame matrixgame_native_session_fixture --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Native fixture build failed.' }
    .\tools\setup-directx-runtime.ps1
    if ($LASTEXITCODE -ne 0) { throw 'DirectX runtime preparation failed.' }
    Copy-Item -LiteralPath '.\.tools\directx-x86\d3dx9_43.dll' -Destination (Join-Path $build 'tests') -ErrorAction Stop
    .\tools\test.ps1 -Configuration $configuration -DLL -WithoutMusic -NoBuild -Jobs 1 -Cheats:$cheats -Filter '^game\.session\.(demo_assets|native_demo_(packed_)?natural_.*)$'
    if ($LASTEXITCODE -ne 0) { throw 'Native fixture checks failed.' }
} finally {
    & $cmake -S . -B $build -DMATRIXGAME_NATIVE_SESSION_TESTS=OFF
    if ($LASTEXITCODE -ne 0) { throw 'Restoration failed: native registration remains unverified; do not run ordinary tests.' }
    & $cmake --build $build --target MatrixGame matrixgame_native_session_fixture --parallel 2
    if ($LASTEXITCODE -ne 0) { throw 'Restoration failed: default fixture binaries remain unverified; rebuild before ordinary tests.' }
}
}
```

Repeat the whole block with `$configuration = 'Release'`, then repeat both configurations with `$cheats = $true`. The same value reaches build and test helpers. Finish those records, then restore the ordinary cheats-disabled builds; each block restores native registration to OFF and rebuilds in `finally`. The host also enforces a frame-count backstop of fewer than 5,000 callbacks for natural Win/Loss and fewer than 20,000 for natural fights. The unthrottled native loop truncates frame durations to whole milliseconds, so that backstop can reject a stalled scene before its game-time deadline. Native combat processes have a 90-second CTest deadline in addition to game-time and frame-count bounds; registration is opt-in and the checks carry the `native-process` and `current-desktop` labels. They run serially; do not run separate native suites concurrently. They require a usable native Direct3D 9 HAL and legacy DirectX runtime. A selected test fails if setup is unavailable. The shared [containment helper](native-window-containment.md) keeps windows and imported cursor operations test-local, while real rendering and presentation remain enabled. Restore registration with `-DMATRIXGAME_NATIVE_SESSION_TESTS=OFF` after an opted-in run.

These observations do not establish visible artwork, human screen picking, original resource behavior, original-host DLL loading or a standalone child-process transition. Native DLL execution uses ParkMiller selected and seeded by production code; the ordinary fixed-step AUTO/combat tests retain both supported RNG modes and replay comparisons. Native procedures and complete-baseline acceptance retain their separate recorded requirements.

Sensitivity controls omit the production laser-dispatch `Damage` call or the damage-driven `AddWarPair` call, without changing the test expectations. The first must reject all four Text/Packed natural Win/Loss cases at either their 20,000 ms game-time deadline or the 5,000-callback frame-count backstop, whichever is reached first. The second must reject both natural-fight cases at the first damaged target's retained-core assertion. Restore the exact source bytes, rebuild and rerun the positive cases after these experiments; the deliberate failures are not alternate rules or passing baseline cases.
