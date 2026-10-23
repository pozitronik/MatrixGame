# Scoped navigation work observations

N5 in the [baseline completion checklist](baseline-completion.md#navigation) requires per-frame observations of path-search and avoidance work. Existing benchmarks report route sizes and a final search-mark footprint; those do not count repeated queue entries, collision candidates or work performed across a complete map tick. This small observation boundary exposes those production operations without moving their responsibilities or changing their algorithms.

The counters and their preservation tests form a separate review unit. Crowded/opposing-flow consumers and measurement-runner reporting must be added after this boundary is reviewed and integrated. These tests do not complete N5 or the whole navigation baseline.

## Collection contract

`NavigationWork::Counters` is caller-owned, initialized to zero by default and independent of the map, robot and resource lifetimes. `NavigationWork::Capture` temporarily makes it the observation destination. Outside a capture, collection is inactive. Game logic never reads a counter value. There is no callback, allocation, clock query, RNG draw or lazy game-state query in the collector.

Use collection on the simulation thread, where the observed engine functions run. The destination pointer follows that existing single-thread execution contract; this API does not add concurrent collection or worker-thread access. The caller must keep its counter object alive for the capture's lifetime. Capture objects cannot be copied or moved. An inner capture temporarily redirects collection and restores its predecessor, including during C++ exception unwinding. Construction and destruction preserve the supplied values, so one destination can accumulate multiple intervals.

```cpp
NavigationWork::Counters frame;
{
    NavigationWork::Capture capture(frame);
    map.Takt(step_ms);
}
// Read or report frame after the simulation boundary.
```

Callers may choose a command, component call, frame or longer interval. This boundary defines counter units, not a frame scheduler. A frame consumer must declare exactly which calls occur inside its capture. The preservation cases below capture `Session::advance`, leaving initial context-command dispatch outside it.

## Counter units

| Field | Increment point and unit |
| --- | --- |
| `local_searches` | One per `FindLocalPath` entry. |
| `local_cells_initialized` | The area of the actual rectangle initialized by a completed local queue search. Count all cells, including blocked ones. |
| `local_queue_entries` | The final queue length before path reconstruction, including the starting cell and any repeated entries after weight relaxation. This is not a unique-cell count. Ordinary failed searches contribute too. |
| `local_failures` | One when the queue search has no result and returns zero. It does not classify exceptions or invalid arguments. |
| `zone_searches` | One per `FindPathInZone` entry, including same-zone and adjacent-zone fast paths. It does not count BFS nodes or road-network searches. |
| `robot_collision_queries` | One per `RobotToObjectCollision` entry. |
| `robot_collision_candidates` | One per actual `CollisionCallback` invocation from that query, including eligible cannon candidates. Objects rejected by the map's broad phase before the callback are outside this count. |
| `terrain_collision_queries` | One per `SphereRobotToAABBObstacleCollision` entry. |
| `terrain_mask_cells` | Number of movement cells whose corner masks are loaded whenever that function rebuilds its sampling rectangle. Repeated rebuilds count again. |
| `terrain_geometry_tests` | One per `SphereToAABB` call for a non-free sampled cell. A cell can contribute in several collision sweeps. |

The local-search volume comes from existing queue and rectangle variables, with no added increment in its neighbor loop. Terrain mask volume is also added once per rebuilt rectangle. Candidate increments accompany actual robot callbacks and non-free-cell geometry tests. A stack-only collision-query context carries the capture pointer to its callback. No persistent class, serialization record or host structure changes layout.

These are operation counts with stated units, not CPU instruction counts or timing budgets. They deliberately exclude place-allocation scans, broad-phase map scans, path optimization, region-road search internals and other simulation work. A completed ordinary failure is observable; an exception or fatal failure need not have completed the volume observations. No counter participates in a search limit, collision response or command decision.

## Run the contracts

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^game\.navigation\.work_'
.\tools\test.ps1 -Configuration Release -Filter '^game\.navigation\.work_'
```

Add `-DLL` for the x86 DLL compile-time profile. The seven cases use synthetic data and `MG_CHECK` in both configurations. No visible window, game packages or native device is needed. Instrumented builds also check tracked allocation balance.

`work_scopes` checks inactive state, retained initial values, nested redirection and restoration after an intentionally distinct exception type. An assertion failure cannot be mistaken for that expected exception. `work_search` checks a real 41-point local route, its 3,600-cell initialized rectangle, queue volume, a failed destination outside the legal search rectangle, accumulated failures and the same-zone fast path. Searches outside a capture leave the destination unchanged. `work_avoidance` supplies nearby wheel robots and a position next to the original central obstacle; actual robot callbacks, mask reads and geometry tests must contribute.

Four `work_preservation` cases use seed 79, both RNG modes and separate 10/16 ms schedules. They issue the ordinary Move to (500, 300) in the central-obstacle world, then compare an inactive journey with two collected journeys. The bounded schedules contain 1,500 or 937 complete frames: 15,000 or 14,992 ms. Each tick must leave collection inactive after its scope. Both robots must arrive, complete snapshots including raw RNG draw counts must match all three journeys, and every per-frame counter must repeat between collected journeys. Inactive totals stay zero. Collected paths must reach the actual search, robot-candidate and terrain-candidate operations. Profiles identify the build and declared unavailable-audio/no-draw fixture.

## Preservation evidence and cost

Before adding the production observations, record the existing movement, replacement and target-removal traces in MinGW Debug/Release EXE and DLL. These provide eight traces per configuration, 32 in total. The candidate must preserve every non-header record exactly, including ordered commands, complete snapshots, RNG draw positions and completion records. Header comparison may exclude only revision and dirty status; compiler, precision, corrections and the remaining profile must agree. This check complements enabled/inactive comparisons inside the candidate binary, because instrumentation can otherwise affect compiler output even when collection is inactive.

Use the existing [measurement procedure](../MEASUREMENTS.md) for disabled-path overhead. On the measured MinGW Release host, a three-process A/A comparison of `navigation.crowd_move` had a median ratio interval of -1.8% to +1.2%. With the same three processes, three warm-ups, fifteen repetitions and five iterations per sample, the inactive-counter candidate interval was -1.1% to +3.2%, with median +0.5%. Both were classified as no material change at the preselected 5% threshold; allocation counts were unchanged. These are component measurements on one host, not a universal frame-time guarantee or an active-collection cost bound.

Sensitivity controls must independently detect omitted local rectangle volume, omitted robot candidates, omitted terrain geometry tests and broken nested restoration. Restore exact production bytes, rebuild and rerun positive cases after each control. Supported MSVC/cheats/trace configurations remain required CI coverage. Original-host execution, native presentation, crowded-scene measurements and accepted historical reference manifests retain their separate requirements; the complete-baseline gate remains closed.
