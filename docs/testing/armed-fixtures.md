# Synthetic armed battle fixtures

The controlled battle harness can load generated meshes with named attachment matrices and use them in production weapon and turret code. These fixtures are shared prerequisites for command, combat, navigation and construction scenarios. They do not change production code or establish the complete behavior baseline.

## Ownership and setup

`tests/battle_mesh.hpp` exposes `load_battle_mesh`. Its implementation remains in `battle_geometry.cpp`, so existing scenario targets do not need another translation unit. Each load writes a temporary storage-format box mesh, eagerly loads it through the production cache and removes the file. Callers specify attachment IDs, names and positions; an optional cache name supports production model lookup. Combat animations are opt-in. The original unarmed chassis keeps its geometry, animations and attachment values.

`BattleCombatAssets` supplies synthetic debris, billboard handles and plasma-impact decal properties. Construct this guard before a `tests::battle::Session`, then call `install()` after session construction. The session retires effects and cache objects before the outer guard restores borrowed static catalogs. No native texture upload or drawing is implemented; the existing device surrogate rejects unsupported operations.

`arm_battle_robot` adds armor and a weapon to a chassis-only fixture robot, then runs production weapon initialization, mass calculation and attachment transforms. Laser and plasma are the exercised weapon families. `create_battle_cannon` supplies the basis, turret and shaft meshes for cannon type 1, then invokes production graph loading and grouping. Synthetic dimensions and weapon configuration are explicit test inputs, not packaged balance data.

Keep fixture extensions in their owning change and consume the reviewed shared implementation. Do not copy model writers or substitute targeting, collision, damage or weapon execution in a dependent suite. New mesh attachments, effect catalogs or device capabilities need assertions demonstrating the production route they enable.

## Executable evidence

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game.combat\.(robot_firing|plasma_flight|cannon_firing|axis_aligned_trace)$'
.\tools\test.ps1 -Configuration Release -Filter '^game.combat\.(robot_firing|plasma_flight|cannon_firing|axis_aligned_trace)$'
```

The four contracts exercise real player attack dispatch and laser firing, plasma flight and damage after shooter removal, autonomous turret acquisition and retargeting, and a legacy object-sphere trace asymmetry. Both RNG modes use seed 82. Laser scenarios run separate 10 ms and 16 ms schedules; plasma and turret scenarios use 10 ms. Fixed impact times, exact health and target transitions supplement repeated-run comparisons. Checks remain active in Release; Debug instrumentation also rejects outstanding engine allocations.

The trace asymmetry is an observed legacy result: a ray with zero X travel misses the synthetic robot while an oblique ray hits. Its expectation does not establish maintainer acceptance of intended gameplay or justify a correction. A separate investigation must determine its native impact before a gameplay contract changes.

CTest writes completed schema-1 traces under the build's `tests/scenario-traces` directory. Headers identify fixture, source and execution profile; command records and snapshots retain draw counts, health, heat and turret aim observations. Incomplete traces lack the completion footer and are rejected by `tools/compare-battle-traces.py`. Exact comparisons apply within one profile; cross-build comparisons require the documented explicit override and do not establish same-profile equivalence.

## Coverage boundary

These contracts establish the shared fixtures and selected COM-01 through COM-03 routes in the [behavior catalog](../research/75-behavior-baseline/README.md). Other weapons, heat limits, repair, complete destruction, carrier delivery, constructor assembly and player command variants remain in their respective scenario suites. Native graphics, sound, original-host execution and packaged resource behavior require separate evidence. No supplied package or extracted game asset is used by these tests.
