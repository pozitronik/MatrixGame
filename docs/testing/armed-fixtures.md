# Synthetic armed battle fixtures

The controlled battle harness can load generated meshes with named attachment matrices and use them in production weapon and turret code. These fixtures are shared prerequisites for command, combat, navigation and construction scenarios. They do not change production code or establish the complete behavior baseline.

## Ownership and setup

`tests/battle_mesh.hpp` exposes `load_battle_mesh`. Its implementation remains in `battle_geometry.cpp`, so existing scenario targets do not need another translation unit. Each load writes a temporary storage-format box mesh, eagerly loads it through the production cache and removes the file. Callers specify attachment IDs, names and positions; an optional cache name supports production model lookup. Combat animations are opt-in. The original unarmed chassis keeps its geometry, animations and attachment values.

`BattleCombatAssets` supplies synthetic debris, billboard handles and plasma-impact decal properties. Construct this guard before a `tests::battle::Session`, then call `install()` after session construction. The session retires effects and cache objects before the outer guard restores borrowed static catalogs. No native texture upload or drawing is implemented; the existing device surrogate rejects unsupported operations.

`arm_battle_robot` adds armor and a weapon to a chassis-only fixture robot, then runs production weapon initialization, mass calculation and attachment transforms. Laser and plasma are the exercised weapon families. `create_battle_cannon` supplies the basis, turret and shaft meshes for cannon type 1, then invokes production graph loading and grouping. Synthetic dimensions and weapon configuration are explicit test inputs, not packaged balance data. The robot fixture assigns `m_LinkMatrix` directly to its declared synthetic attachment; it bypasses the pylon selection/compatibility tables used by `WeaponInsert` and `WeaponSelectMatrix`, so it does not establish legal native assembly combinations.

After assembly, the helper invalidates the bounds timestamp before production grouping recomputes the bounding box and sphere. The chassis-only box is approximately 20 units high; the assembled robot is approximately 40 units high. Reusing the chassis bounds until the 100 ms cache interval expires would give the fixture an inconsistent initial collision shape. Exported traces identify the corrected setup as `synthetic-armed-boxes-v2`; v1 traces describe the earlier stale-bounds setup and are a different profile. This changes test initialization only. The three registered shared contracts retain their impact, health and target-transition expectations.

Keep fixture extensions in their owning change and consume the reviewed shared implementation. Do not copy model writers or substitute targeting, collision, damage or weapon execution in a dependent suite. New mesh attachments, effect catalogs or device capabilities need assertions demonstrating the production route they enable.

## Executable evidence

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.combat\.(robot_firing|plasma_flight|cannon_firing)$'
.\tools\test.ps1 -Configuration Release -Filter '^game\.combat\.(robot_firing|plasma_flight|cannon_firing)$'
```

The three registered contracts exercise real player attack dispatch and laser firing, plasma flight and damage after shooter removal, and autonomous turret acquisition and retargeting. Both RNG modes use seed 82. Laser scenarios run separate 10 ms and 16 ms schedules; plasma and turret scenarios use 10 ms. Fixed impact times, exact health and target transitions supplement repeated-run comparisons. Checks remain active in Release; Debug instrumentation also rejects outstanding engine allocations.

Both RNG modes currently have the same asserted impact, health and target-transition outcomes; their draw-count traces differ. These cases do not establish a gameplay outcome that depends on the generator's values.

Audio is deliberately unavailable in this secondary fixture profile. It isolates the initial geometry, firing and effect-lifetime contracts from sound-variant choices and synthetic voice completion. Production weapon code still calls the sound frontend, whose unavailable-backend path does not establish the available-audio draw schedule. The adopted primary profile remains available synthetic audio; these three cases do not cover it or replace its required combat evidence. Enabling it requires separate profile assertions for draw consumption, voice lifetime and the resulting gameplay timeline. Neither native audio timing nor audible output is claimed here.

CTest writes completed schema-1 traces under the build's `tests/scenario-traces` directory. Headers identify fixture, source and execution profile; command records and snapshots retain draw counts, health, heat, fixed cannon body yaw, rotating turret yaw and pitch observations. The cannon scenario asserts that turret yaw actually changes. Session identity checks bind the raw shooter/patient pointers to the IDs later used by commands and removal. Incomplete traces lack the completion footer and are rejected by `tools/compare-battle-traces.py`. Exact comparisons apply within one profile; cross-build comparisons require the documented explicit override and do not establish same-profile equivalence.

## Coverage boundary

These contracts establish the shared fixtures and selected COM-01 through COM-03 routes in the [behavior catalog](../research/75-behavior-baseline/README.md). Other weapons, heat limits, repair, complete destruction, carrier delivery, constructor assembly and player command variants remain in their respective scenario suites. Native graphics, sound, original-host execution and packaged resource behavior require separate evidence. No supplied package or extracted game asset is used by these tests.

## Axis-aligned trace defect

The correction and its gameplay disposition are tracked in [the axis-aligned trace issue](https://github.com/pozitronik/MatrixGame/issues/129).

An unregistered correctness probe expects both an oblique ray and a zero-X-travel ray to hit the same synthetic robot, through both sphere intersection and mesh `Pick`. The current engine fails the axis-aligned expectations. This is a defect probe, not a passing golden gameplay rule, and it is excluded from CTest registration. The source and independent reproduction are recorded in the [review findings](https://github.com/pozitronik/MatrixGame/pull/126#pullrequestreview-5503679613).

In `CMatrixMap::ScanLandscapeGroup`, `t1` starts at zero. The nonzero-X branch can increase it; the Y branch only decreases it. For zero X travel the later `t < t1 && t > t0` test rejects object hits. The rejection follows both the sphere-intersection and mesh-picking paths; it is not specific to armed robots or sphere traces. Native occurrence frequency and gameplay impact have not been measured. A separate correction needs regressions and a disposition of any changed gameplay; no production fix is included here.

After building the tests, run the diagnostic directly from the repository root:

```powershell
& '.\build\mingw-debug-exe\tests\matrixgame_combat_scenarios.exe' 'probe.combat.axis_aligned_trace'
& '.\build\mingw-release-exe\tests\matrixgame_combat_scenarios.exe' 'probe.combat.axis_aligned_trace'
```

Both commands should currently exit with code 1 at the final hit assertion, after printing both trace modes for each RNG profile. That expected diagnostic failure is not CI success evidence. Once the engine is corrected, the same correctness assertions should pass and can become registered regression coverage.

The revised fixture suite passes all 240 registered tests in MinGW Debug and Release. Sixteen same-profile trace pairs and eight explicitly overridden cross-build pairs match through the supported comparator. The unregistered axis probe separately exits with code 1 in each configuration, after checking both intersection modes under each RNG profile. Repository checks pass; these local results do not replace supported-toolchain CI or independent review.
