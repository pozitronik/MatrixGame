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

The ordinary `game.session.demo_assets` check parses all six AI/combat declarations in Text and Packed form, including complete robot columns, the single armed side, stationary/nonlethal component-scene configuration and the natural-fight heat and armor declarations. It preserves the factory-column, transport-model and reinforcement-order checks of the loaded-demo AI suite. It requires no window or device and runs in default DLL test configurations.

```powershell
.\tools\build.ps1 -Configuration Debug -DLL -WithoutResources -TestsOnly
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' -S . -B build/mingw-debug-dll -DMATRIXGAME_NATIVE_SESSION_TESTS=ON
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' --build build/mingw-debug-dll --target matrixgame_native_session_fixture
.\tools\test.ps1 -Configuration Debug -DLL -NoBuild -Jobs 1 -Filter '^game\.session\.(demo_assets|native_demo_(packed_)?natural_.*)$'
```

Repeat with Release and its build directory, then repeat both with `-Cheats` on the build and test helpers. Keep those options matched and restore the ordinary builds without `-Cheats` afterwards. Native combat processes have a 90-second CTest deadline in addition to game-time and callback bounds; registration is opt-in and the checks carry the `native-process` and `current-desktop` labels. They run serially; do not run separate native suites concurrently. They require a usable native Direct3D 9 HAL and legacy DirectX runtime. A selected test fails if setup is unavailable. The shared [containment helper](native-window-containment.md) keeps windows and imported cursor operations test-local, while real rendering and presentation remain enabled. Restore registration with `-DMATRIXGAME_NATIVE_SESSION_TESTS=OFF` after an opted-in run.

These observations do not establish visible artwork, human screen picking, original resource behavior, original-host DLL loading or a standalone child-process transition. Native DLL execution uses ParkMiller selected and seeded by production code; the ordinary fixed-step AUTO/combat tests retain both supported RNG modes and replay comparisons. Native procedures and complete-baseline acceptance retain their separate recorded requirements.
