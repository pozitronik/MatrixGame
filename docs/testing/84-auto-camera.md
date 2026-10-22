# Flying-camera fight selection

`game.ai.auto_camera_fights` covers A8's changing-fight camera behavior using the real AUTO and FLYCAM parsers, registered robot damage events and full map frames. It complements [ordinary AUTO activation](84-auto-scenes.md). It does not load a demo map or assign demo startup flags.

## Scene and observations

Four stationary robots occupy the ordinary synthetic open-map positions: player robots at (100, 200)/(100, 260), enemy robots at (500, 500)/(540, 540). Their real geometry centers have z = 10. Both sides retain valid team membership and execute high-level decisions; a long initial maintenance cooldown prevents an unrelated carrier request. Each camera profile uses seed 84, ParkMiller or LegacyCRT, and a 10 or 16 ms fixed map step. A fresh session repeats each profile.

The test submits AUTO and FLYCAM through `Cheats::processInput`. A full map frame creates the production camera's automatic-flight state. It then delivers a nonlethal laser event to the first enemy through `CMatrixRobotAI::Damage`, identifying the first player robot as attacker. This is an explicit component-fed hit, not a projectile collision test. The receiver registers the war pair through the real camera and creates a real impact effect using synthetic effect assets. Each declared damage event consumes 28 RNG draws in this fixture.

The first fight midpoint is (300, 350, 10). At 2000 ms the camera is within 32 units of it and still owns the pair's object-core references, although its pending-pair TTL is only 1000 ms. The selected fight is retained separately from the pending list. A second hit at 2000 ms registers the other enemy with the same attacker; its midpoint is (320, 370, 10). While the original participants remain alive, the second pair stays pending and expires at 3010/3008 ms. The current pair remains referenced and the camera is within 10 units of its midpoint by 3200 ms.

At 3200 ms another hit registers the expired alternative again. The test then removes the current target through the map's normal deletion route. On the next frame the camera releases that retired target's retained core and follows the replacement pair sharing the surviving attacker. By 6000 ms it is within two units of the new midpoint and more than 26 units from the old midpoint. Both RNG modes have consumed 342 draws. The map remains unpaused with no result dialog. Turning FLYCAM off and advancing one frame releases all camera-owned pair references while leaving AUTO enabled.

The registered test compares exact repeated robot/RNG state, automatic flags, camera link-point and angle bits, and core presence/reference counts from initial setup through the disable frame. It records each command and component intervention as an explicit boundary. Health, pair lifetime, pending expiration, midpoint approach and cleanup also have independent assertions. Native rendering and keyboard cheat-gate dispatch are outside this case; the latter is covered by `game.player.registered_cheat_effects`.

## Flight history and repeated sessions

The original `SAutoFlyData::Takt` stored `prev_da` and `last_da` as function-static yaw-smoothing values. They survived camera destruction and flight toggles: the initial ParkMiller 10 ms yaw was about -0.0152568 in a fresh process and -0.0147999 after another session. Both values now belong to the existing automatic-flight allocation, which is cleared when a flight begins and freed when flying-camera mode ends. The within-flight recurrence is unchanged; a subsequent flight no longer inherits another flight's smoothing history. These internal fields are neither serialized nor part of the host interface.

`game.ai.auto_camera_replay` extends the encounter with another FLYCAM on/off cycle, including 50 intervening map frames, then compares all observations and both camera angles against a fresh complete repetition. `game.ai.auto_camera_overview_replay` separately exercises the smoothing recurrence through 20 ordinary flight updates and 20 overview updates at 10/16 ms. It explicitly initializes the overview timer after accumulating nonzero yaw motion, retains real war-pair references, and requires positive, decreasing yaw increments during overview. A new component flight also starts directly in overview, before its first Takt: yaw must remain at its initial angle. Its complete trace must equal a fresh-session control even after another nonzero flight has retired in the same session. Both RNG modes and 10/16 ms steps cover these observations without additional draws. This component case does not establish the random 15-second overview trigger. Restoring the original static-history implementation makes all three camera regression cases fail in Debug and Release.

First-flight compatibility can be checked without storing compiler-specific golden values in the source tree. The unregistered `probe.ai.camera_first_pm10`, `pm16`, `legacy10` and `legacy16` commands (each with the full `probe.ai.camera_first_` prefix) emit `camera-reference` rows containing angle bits and the existing complete encounter observations. Run each in a fresh process on the original and corrected implementation with the same toolchain and compare those rows. The `overview10` and `overview16` variants emit the corresponding component trajectory. Local MinGW Debug/Release comparisons matched all 608/383 encounter snapshots per 10/16 ms profile and all 40 snapshots per overview profile. This comparison establishes the selected uninterrupted-flight cases, not all possible camera trajectories or the accepted whole-game historical baseline.

## Running

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.ai\.auto_camera_'
.\tools\test.ps1 -Configuration Release -Filter '^game\.ai\.auto_camera_'
```

After building the same target, a first-flight reference trace can be recorded separately:

```powershell
& '.\build\mingw-debug-exe\tests\matrixgame_ai_auto_scenarios.exe' probe.ai.camera_first_pm10
& '.\build\mingw-release-exe\tests\matrixgame_ai_auto_scenarios.exe' probe.ai.camera_first_pm10
```

The [native mode-reentry cases](85-native-mode-reentry.md) separately cover demo-named `CGame::Init`, its introduction/full-auto result rules and following ordinary sessions. The [loaded-demo AI continuation](85-native-demo-ai.md) combines that startup with eligible/ineligible reinforcement, side decisions and changing-fight camera observations. The native loop retains its time-derived ParkMiller seed and bounded observations; the parser-level scenes here retain fixed-step, both-RNG replay and reference traces. Neither substitutes for the other's timing or lifecycle boundary.

## Accepted effect on later simulation draws

Per-flight history reset is the accepted correction, including its possible effect on the shared RNG stream; the [contract decision](https://github.com/pozitronik/MatrixGame/issues/243#issuecomment-6247709670) is limited to this behavior. A later flight can begin in overview after map time exceeds 15 seconds. The overview tests a landscape ray along the camera direction; changed yaw can alter the step on which the ray hits terrain and ends overview. The next target search can then draw IRND/FRND at a different step, shifting subsequent simulation draws and potentially battle outcomes. Equal RNG traces in the bounded flat scenes do not rule out this terrain-dependent path.

The registered overview components deliberately set their timer and use unobstructed geometry. They establish flight-local history and the unchanged uninterrupted recurrence, not the random overview trigger or a rendered terrain-hit scene. Resource formats, host interfaces and damage rules are unchanged; complete gameplay-reference acceptance remains separate.
