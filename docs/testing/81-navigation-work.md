# Scoped navigation work observations

N5 in the [baseline completion checklist](baseline-completion.md#navigation) requires per-frame observations of path-search and avoidance work. Existing benchmarks report route sizes and a final search-mark footprint; those do not count repeated queue entries, collision candidates or work performed across a complete map tick. This small observation boundary exposes those production operations without moving their responsibilities or changing their algorithms.

The counters and their original preservation tests were reviewed separately from the [crowded-scene consumers and measurement reports](#crowded-scene-consumers-and-reports) below. The consumers reuse that integrated boundary without adding production access or changing scheduling. These tests do not complete N5 or the whole navigation baseline.

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

Add `-DLL` for the x86 DLL compile-time profile. The seven original boundary cases and eight crowded-scene cases use synthetic data and `MG_CHECK` in both configurations. No visible window, game packages or native device is needed. Instrumented builds also check tracked allocation balance.

`work_scopes` checks inactive state, retained initial values, nested redirection and restoration after an intentionally distinct exception type. An assertion failure cannot be mistaken for that expected exception. `work_search` checks a real 41-point local route with a 3,600-cell initialized rectangle and exactly 1,943 queued entries. A failed destination outside the legal search rectangle accumulates 4,895 entries and one failure; the same-zone fast path is checked separately. Searches outside a capture leave the destination unchanged. `work_avoidance` supplies nearby wheel robots and a position next to the original central obstacle; exactly one robot callback, 72 mask-cell reads and 30 non-free-cell geometry tests must contribute. These deterministic component values are asserted in both Debug and Release, distinguishing queue length from processed entries and non-free geometry tests from all swept cells.

Four `work_preservation` cases use seed 79, both RNG modes and separate 10/16 ms schedules. They issue the ordinary Move to (500, 300) in the central-obstacle world, then compare an inactive journey with two collected journeys. The bounded schedules contain 1,500 or 937 complete frames: 15,000 or 14,992 ms. Each tick must leave collection inactive after its scope. Both robots must arrive, complete snapshots including raw RNG draw counts must match all three journeys, and every per-frame counter must repeat between collected journeys. Collected paths must reach the actual search, robot-candidate and terrain-candidate operations. Profiles identify the build and declared unavailable-audio/no-draw fixture.

## Preservation evidence and cost

Before adding the production observations, record the existing movement, replacement and target-removal traces in MinGW Debug/Release EXE and DLL. These provide eight traces per configuration, 32 in total. The candidate must preserve every non-header record exactly, including ordered commands, complete snapshots, RNG draw positions and completion records. Header comparison may exclude only revision and dirty status; compiler, precision, corrections and the remaining profile must agree. This check complements enabled/inactive comparisons inside the candidate binary, because instrumentation can otherwise affect compiler output even when collection is inactive.

Use the existing [measurement procedure](../MEASUREMENTS.md) for disabled-path overhead. On the measured MinGW Release host, a three-process A/A comparison of `navigation.crowd_move` had a median ratio interval of -1.8% to +1.2%. With the same three processes, three warm-ups, fifteen repetitions and five iterations per sample, the inactive-counter candidate interval was -1.1% to +3.2%, with median +0.5%. Both were classified as no material change at the preselected 5% threshold; allocation counts were unchanged. These are component measurements on one host, not a universal frame-time guarantee or an active-collection cost bound.

Sensitivity controls must independently detect omitted local rectangle volume, omitted robot candidates, omitted terrain geometry tests and broken nested restoration. The exact component-unit assertions also reject counting processed entries instead of the final queue length and counting free cells as geometry tests. Restore exact production bytes, rebuild and rerun positive cases after each control. Supported MSVC/cheats/trace configurations remain required CI coverage. Original-host execution, native presentation and accepted historical reference manifests retain their separate requirements; the complete-baseline gate remains closed.

## Crowded-scene consumers and reports

`work_crowded.*` and `work_opposing.*` each cover ParkMiller/LegacyCRT and independent 10/16 ms profiles, with seed 81 and exactly 32,000 simulated milliseconds: 3,200 or 2,000 complete frames. The shared test-only scene uses generated wheel meshes, unavailable audio and the existing no-draw session. Eight friendly movers have stable fixture IDs `1,2,5,6,7,8,9,10`; the original opponents `3,4` remain alive and stationary at (580,580) and (540,540). Setup refreshes relocated matrices, bounds and group membership before any command.

The crowded-destination scene uses the existing central-obstacle grid. Eight robots start at X 100 and Y 80,140,200,260,320,380,440,500. One selected-group context Move requests (500,300); production placement chooses eight distinct individual destinations. The opposing-flow scene reuses the established Open dense-flow topology: four robots start at X 100 and four at X 540, at Y 200,260,320,380. Each receives an ordinary `PGOrderMoveTo` through `RobotToLogicGroup`, to footprint-corner X 48 or 8 and the matching Y coordinate. Both families require every mover to progress, reach its assigned place, retire its movement order and retain the original group Move intention without War. Separation among movers remains positive; positions remain finite and in bounds. This is a declared command boundary, not screen-input coverage.

Initial commands run before collection. Every frame creates a fresh zeroed `Counters`, captures only the existing `Session::advance()` call and ends the scope before observations, aggregation or reporting. That call includes the production map tick and the harness's existing CPU camera refresh. The captured interval is `(0,32000]` ms; subintervals are `(0,4000]`, `(4000,12000]` and `(12000,32000]`, whose boundaries divide both timesteps exactly. Counts include every observed production call in the map, including the two stationary opponents; they are not per-mover costs. Setup, initial destination allocation, snapshot construction and reporting are excluded from capture.

Each profile runs once with collection inactive and twice with collection enabled in fresh sessions. Every completed boundary compares normalized actor positions, heading/health/order bits, group state and raw RNG draw count, including the six additional actors outside the original four-ID snapshot. Individual assigned cells, first-movement/arrival times, final coordinate bits and minimum separation also compare exactly. Enabled runs must repeat every frame's counters and each interval total; sums of both representations must equal the complete total. Independent frame-index ranges reconstruct all ten interval fields, with exactly 400/800/2,000 frames at 10 ms or 250/500/1,250 at 16 ms. Collection must be inactive after every frame. Both RNG modes are exercised, but these unarmed journeys consume no RNG draws after command setup in the measured profiles; no RNG-dependent outcome is claimed.

The following are observed MinGW x86 Debug EXE totals with cheats and trace disabled, identical in the two RNG modes. They are measured reports, not literals supplied to the collector or timing budgets. The MinGW x86 consumer cases require all ten totals exactly in Debug/Release EXE/DLL profiles and require zero RNG draws during capture. Other compilers retain within-profile preservation/repeat checks, positive failed-search counts and positive crowded-scene geometry work; no cross-compiler floating-point tolerance or shared total oracle is inferred.

| Scene / step | Local searches | Initialized cells | Queue entries | Failed searches | Zone searches |
| --- | ---: | ---: | ---: | ---: | ---: |
| Crowded / 10 ms | 86 | 309600 | 276385 | 5 | 86 |
| Crowded / 16 ms | 77 | 277200 | 247567 | 4 | 77 |
| Opposing / 10 ms | 193 | 694800 | 692835 | 74 | 193 |
| Opposing / 16 ms | 179 | 644400 | 648120 | 66 | 179 |

| Scene / step | Robot queries | Robot candidates | Terrain queries | Mask-cell reads | Geometry tests |
| --- | ---: | ---: | ---: | ---: | ---: |
| Crowded / 10 ms | 16998 | 17823 | 16998 | 611928 | 51264 |
| Crowded / 16 ms | 21258 | 22883 | 21258 | 765288 | 65020 |
| Opposing / 10 ms | 14933 | 16191 | 14933 | 537588 | 0 |
| Opposing / 16 ms | 19154 | 21963 | 19154 | 689544 | 0 |

Failed queue searches contribute real work before eventual arrival. Queue entries include re-entries and therefore need not equal unique initialized cells. The Open opposing scene has no non-free cells in its sampled terrain rectangles, so its zero geometry-test count does not imply that terrain queries or sampling were skipped. The original component contracts retain exact nonzero geometry-test evidence. Counts across different timestep schedules are observations of different executions, not a performance ranking.

`matrixgame_navigation_work_benchmarks` reuses `benchmark_support.cpp` and the existing [measurement runner](../MEASUREMENTS.md#navigation-operation-counts). Eight workload names use `navigation.crowded_destination.<profile>` or `navigation.opposing_flow.<profile>`, where profiles are `park_miller10`, `park_miller16`, `legacy10`, `legacy16`. The two RNG labels execute identical no-draw trajectories and workloads for each scene/step pair, so these eight names represent four distinct measured workloads. A timed iteration runs the complete scene with collection inactive and full snapshots disabled; it still includes per-frame contract observations and result bookkeeping. Its timing is not a production frame-cost or collector-overhead measurement. Fresh untimed inactive/enabled scenes then compare full states and events before exporting the ten actual collector fields, three interval totals, movement/arrival/allocation/final-position observations and a split FNV-64 trajectory digest. Across fresh benchmark processes the existing runner requires identical work and inspection records. A digest complements, rather than replaces, the exact within-process state comparisons.

The existing three component benchmark workloads and their search-mark footprint remain unchanged. The crowded-scene consumers add no production seam, scheduler, algorithm, persisted reference or acceptance decision. Intermediate/trailing formation rules, native trajectories, reference manifests and integrated performance acceptance retain their separate scope.
