# Navigation through loaded terrain

`game.navigation.loaded_map_assets` and the four opt-in `game.navigation.native_loaded_masks.*` cases connect a generated CStorage map to the real `CGame::Init`/`PrepareMap` loader, production pathfinding and a wheel robot's context Move. They extend N1's loading/preparation boundary and N3's impassable destination in the [completion checklist](baseline-completion.md#navigation). They do not modify production code or require original game resources.

## Map compilation boundary

The runtime does not derive movement restrictions from vertex heights. `MatrixMapPrepare.cpp` loads each point's index into `DATA_MOVE`, expands its `SCompileMoveCell` into four runtime movement cells and copies the zone, stop, sphere and directional fields. It separately loads point heights and cell flags, then computes normals and land-plane coefficients. The map compiler that originally authored those restrictions is outside this repository. A test claiming to exercise terrain-to-mask compilation in this engine would describe an unavailable path.

This fixture therefore authors serialized restrictions before initialization, using the existing version-27 map format. It does not assign runtime `m_Stop` values, inject the resulting route or rewrite the robot's accepted destination. The native cases check how the actual loader, pathfinder and command logic consume those records. They establish loading and preparation evidence, without claiming to validate the original editor's compilation algorithm or terrain balance. The broader work package and baseline gate remain open.

## Declared map and restrictions

The map has 32 by 32 terrain cells and 64 by 64 movement cells. Terrain vertices and compiled movement records are generated into a temporary map; texture and model files are synthetic. The reused native asset generator supplies the tracked text configuration with replacement artwork and two unarmed robots, one per side.

| Band along world X | Terrain declaration | Authored traversal restriction |
| --- | --- | --- |
| Below 160 | Flat dry land, height 10 | Open inside the map border |
| [160,240) | Shallow submerged land, height -3 | Open for all five chassis |
| [240,320) | Water-only cells, stored vertex height -40 | Pneumatic, wheel and track blocked; hovercraft and antigravity allowed |
| [320,400) | Flat dry land, height 10 | Open |
| [400,480) | Continuous ramp from height 10 to 58, gradient 0.6 | Wheel and track blocked; pneumatic, hovercraft and antigravity allowed |
| [480,640] | Flat high land, height 58 | Open inside the border |

Band boundaries can contain the connecting terrain triangles. These are declared test inputs, not a claim about original maps or the original editor's thresholds. Restrictions are expanded for footprint sizes one through five and a two-cell outer border. The four road places preserve vertical connections; their horizontal edges carry the pneumatic/wheel/track restriction because they cross the deep-water band. This keeps the serialized road and cell restrictions consistent.

Shallow land remains `CELLFLAG_LAND` at negative height. The deep band is `CELLFLAG_WATER`: `GetZ` returns its existing -1000 no-land-plane sentinel, while the stored point still has height -40. The loader cases distinguish both states rather than treating that sentinel as a seabed height. At the ramp midpoint, generated normals must have the expected uphill direction and unit slope; the interpolated height is 28 within 0.0001 world units for plane-arithmetic rounding. Four open marker subcells carry distinct sphere/directional values to detect swapped offsets. Their stop bits remain clear, so the markers cannot alter route choice.

The ordinary asset case validates the serialized dimensions, height/flag declarations, four marker records, every footprint's deep/slope mask and the road-edge restrictions. It runs in the default engine suite without creating a window or device. It verifies the fixture's format, not runtime loading.

## Production observations

Each native case creates an owned hidden child window and a native DirectX 9 device, then calls `CGame::Init` with the generated map. Initialization performs its normal graphical resource preparation; subsequent gameplay is advanced through controlled map ticks without drawing. Host sound creation returns zero and no audible output is checked. The inherited containment helper prevents foreground activation and physical cursor capture. The test requires the child to remain effectively invisible and owned by the hidden parent. Missing native DirectX support fails the case rather than counting as a successful or skipped load.

After loading, the test checks dimensions, road-edge restrictions, all four marker subcells, dry/submerged/water heights and flags, the computed ramp normal and the ramp's interpolated height. All five chassis query each footprint size in the deep and slope bands. The size-four boundary immediately before deep water distinguishes a free single cell from a blocked robot footprint. The outer border rejects movement for every chassis.

For each chassis, actual `FindLocalPath` searches use the loaded map. A dry-to-shallow route from movement cell (8,8) to (18,8) has eleven points. Crossing the complete deep strip from (8,8) to (36,8) fails for pneumatic/wheel/track and has 29 straight points for hovercraft/antigravity. Crossing the ramp from (36,8) to (52,8) fails for wheel/track and has seventeen points for the other three chassis. Every returned point is checked. Repeating the initial valid search after the failed alternatives verifies that search marks do not poison a later query. None of these component searches may consume RNG draws.

The loaded wheel robot then receives a real context Move toward world (270,100), inside the blocked deep band. Its declared maximum/group speed is 0.6; the unarmed enemy has maximum/group speed zero to keep it outside the journey. The command retains its requested group destination (25,8), but production placement assigns the fallback cell (20,3). The robot moves through shallow submerged land and settles at world (220,50), never entering deep water. Ten seconds of real map ticks require live identity, finite positions, retained Move intent and no pause/result dialog. During the final two seconds its position remains exact, its speed is zero and its low-level order pool is empty. This records arrival at a legal fallback; it does not claim arrival at the original requested point or a player-facing failure notification.

The two RNG modes and 10/16 ms steps form four separate cases, each repeated through a fresh complete game initialization. DLL initialization follows its existing ParkMiller/seed-81 contract; after loading, the controlled simulation is explicitly reseeded to the case's declared mode and seed. Output records revision/dirty state, compiler, configuration, cheats, floating-point precision, route counts, observed arrival and RNG draws. Every map frame contributes raw position, height and speed bits, time, RNG draw count, arrival state and order count to an exact same-profile replay comparison. These comparisons do not establish accepted historical references or equivalence across compilers. Cleanup releases the form and retained core before production game teardown; the engine aliases must be cleared afterwards, and Debug retains the ordinary tracked-allocation check.

## Running

The asset check participates in the normal suite:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.navigation\.loaded_map_assets$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.navigation\.loaded_map_assets$'
```

Native loading needs a Windows desktop with the x86 DirectX runtime. It uses hidden owned windows and does not require interactive playtesting. Build the DLL tests normally, opt into their native registration, then build and select only this fixture:

```powershell
.\tools\build.ps1 -Configuration Debug -DLL -TestsOnly
$cmake = '.\.tools\cmake-4.4.4-windows-i386\bin\cmake.exe'
& $cmake -S . -B build/mingw-debug-dll -DMATRIXGAME_NATIVE_SESSION_TESTS=ON
& $cmake --build build/mingw-debug-dll --target matrixgame_loaded_navigation_tests
.\tools\test.ps1 -Configuration Debug -DLL -NoBuild -Filter '^game\.navigation\.native_loaded_masks\.'
& $cmake -S . -B build/mingw-debug-dll -DMATRIXGAME_NATIVE_SESSION_TESTS=OFF
```

Repeat with `Release` and `build/mingw-release-dll`. Reset the native option after the selected checks so a later ordinary test run does not unexpectedly include the wider native session suite. CTest gives each case its normal 30-second process limit and runs native cases serially; the movement loop separately limits frames. CI compiles the DLL code and runs the asset check in its normal matrix; native execution requires the explicit opt-in above. Original-host behavior, visible terrain/model correctness, accepted trajectories, performance measurements and whole-baseline acceptance remain separate evidence.
