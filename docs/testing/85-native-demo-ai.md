# Loaded-demo AI and camera continuation

These checks extend the [native demo lifecycle](85-native-mode-reentry.md) with loaded synthetic reinforcement and changing-fight scenes. They enter the production DLL-mode `Run`, load a mixed-case `synthetic-DeMo.map` filename through `CGame::Init`, execute the native loop and leave through Escape. They do not assign automatic-control flags to a running map. All three startup flags, the absent introduction, ParkMiller selection and finite camera state are asserted at the host callback boundary.

Each scene has Text and isolated Packed variants. After the demo exits with result 1, the fixture restores the ordinary resource declaration, deliberately fails the next initialization and starts an ordinary battle with a fresh movement command. The Packed case rebuilds the valid archive from the restored declaration before corrupting its header for the failure/recovery step; parked loose files cannot satisfy the loader. This restoration matters for the camera scene, whose declared chassis cannot move.

## Reinforcement and side decisions

`native_demo_reinforcement` loads two unarmed opposing robots and one friendly titanium factory. The resource configuration declares a 20,000 ms maintenance interval; the map's one-percent maintenance property produces an initial 200 ms countdown. The test neither assigns the live timer nor calls the reinforcement routine directly. Full production frames must request one transport, attach a friendly robot with 150 displayed health and renew the countdown. The existing player robot must have an automatic Defence action. The observation exits shortly after the request; carrier delivery and combat are separate contracts.

`native_demo_no_reinforcement` gives the same factory to the opponent. At least 1,500 ms of game time must pass without a requested transport, the countdown must remain at zero and the friendly robot must have an automatic Capture action. Both scenes assert RNG activity without prescribing an exact count for a time-seeded native loop. A 10,000 ms game-time bound prevents an unobserved request or decision from being accepted as success.

## Changing fights

`native_demo_camera_fights` loads three stationary unarmed actors: one friendly robot at (100, 100) and opponents at (300, 100) and (500, 300). The configuration declares zero wheel-chassis movement speed and nonlethal laser damage. Resolved laser hits are delivered explicitly through production `CMatrixRobotAI::Damage`, identifying the friendly attacker. This is a component-fed event, rather than a projectile or screen-picking test; damage, impact effects, fight registration, camera updates and reference ownership remain production behavior.

After the first hit the camera must approach that pair's midpoint within 32 world units in two seconds. A second hit registers the other opponent. After another 1,200 ms its pending pair must expire while the selected first pair remains referenced and the camera is within ten units of its midpoint. A new hit registers the alternative again, and the fixture explicitly deletes the current target through the map's normal deletion route. The camera must retire that target's reference and approach the surviving pair's midpoint within two units after at least three further seconds. A 12,000 ms game-time bound applies to the complete journey.

Reference counts independently distinguish pending, selected, expired and retired participants. Observation references are released before Escape and on callback exceptions, while the owning game heap still exists. Production teardown then owns the remaining camera references; empty engine aliases/lists, retained borrowed-device ownership and Debug allocation checks verify the session boundary. The host observation alias is cleared before its stack object is destroyed. The completed camera journey retains no test-owned engine allocation across `Run` cleanup.

The new demo scenes require balanced release of every requested host text bitmap; a short reinforcement run need not request one. The existing demo controls and each ordinary successful reentry retain their additional requirement that at least one bitmap was requested. This distinguishes callback lifetime from an assumption about when a particular scene first draws text.

## Running and limits

`game.session.demo_assets` validates all three synthetic map declarations in both resource profiles without opening a window or device. It checks column lengths and actor descriptors, factory ownership, the transport model, the camera's stationary/nonlethal configuration and packed-file isolation. It runs with the ordinary DLL test suite.

Native execution uses the [test-window containment helper](native-window-containment.md). The borrowed rendering child remains under an invisible parent; imported cursor operations are test-local and unexpected diagnostic dialogs fail the scenario. Actual Direct3D creation, reset, rendering and presentation remain enabled. Native cursor appearance, focus delivery, visible loading, audible playback and original-host binary loading are excluded.

The driver's successful presentation statuses, including an occluded window, establish continued device use rather than visible output. The host's post-session presentation check therefore uses HRESULT success. The engine's [presentation classification](../DEVICE_RECOVERY.md) also accepts successful statuses while keeping the loading handshake restricted to `S_OK`.

```powershell
.\tools\build.ps1 -Configuration Debug -DLL -WithoutResources -TestsOnly
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' -S . -B build/mingw-debug-dll -DMATRIXGAME_NATIVE_SESSION_TESTS=ON
& '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe' --build build/mingw-debug-dll --target matrixgame_native_session_fixture
.\tools\test.ps1 -Configuration Debug -DLL -NoBuild -Jobs 1 -Filter '^game\.session\.native_demo_(packed_)?(reinforcement|no_reinforcement|camera_fights)$'
```

Repeat with Release and its build directory, then repeat both with `-Cheats` on the build helper and `-Cheats` on the test helper. Keep build and test options matched; restore the ordinary builds without `-Cheats` afterwards. The demo profile is unchanged by this build option, while the existing toggle controls verify enabled/disabled dispatch. The six checks carry the `native-process` and `current-desktop` labels, run serially within CTest and require a usable Direct3D 9 HAL and legacy DirectX runtime. Do not run separate native suites concurrently. A missing adapter fails the selected test rather than skipping it. Each test has the existing 30-second process limit in addition to its game-time/tick bounds. Native registration persists until the build directory is configured with `-DMATRIXGAME_NATIVE_SESSION_TESTS=OFF`.

The production DLL chooses ParkMiller and seeds from wall time. For initialized sessions, logs bracket that seed and report request time, damage-related RNG draws, host ticks and results. An invalid packed header fails before the first progress callback and has no upper seed observation. These checks use bounded invariants; they do not claim fixed 10/16 ms schedules, LegacyCRT native execution, identical camera-angle bits or historical equivalence. Existing asset-free AUTO/camera scenes retain both RNG modes, fixed-step replays and their separate reference traces. These native continuations establish neither complete #84/#85 closure nor the accepted whole-game baseline.

Test-strength controls temporarily omit the automatic `Maintenance` request in `CMatrixSideUnit::LogicTakt` or the damage-driven `AddWarPair` call in `CMatrixRobotAI::Damage`. Both Text and Packed reinforcement cases must reject the first mutation at their request deadline; both camera cases must reject the second at their reference-count assertion. A separate test-only control throws an engine exception after the first camera hit: callback handling must release observation references before production cleanup and report a failed case without a leak diagnostic. Restore every mutation and rebuild before recording a successful suite; these deliberate failures are not alternate game rules or registered passing cases.

The pending-pair expiry boundary checks both selected participants at reference count three as well as the expired participant at two. Deleting the selected opponent then leaves its retained core at two until camera replacement releases the selected-pair reference. A sensitivity control releases the selected pair when an unrelated pending pair expires; the new expiry-boundary reference assertion must reject it. Camera proximity alone does not establish reference retention.

The [autonomous native combat scenes](85-native-demo-combat.md) separately observe real weapon-driven wins/losses and fight switching without component-fed damage or target deletion. They complement the tighter lifetime and pending-pair controls described here.
