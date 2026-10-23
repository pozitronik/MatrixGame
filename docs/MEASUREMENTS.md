# Coverage and performance measurements

This guide describes how to measure which production code the automated suite executes and how to time representative engine workloads. Both measurements use the pinned Windows x86 tools from the [build guide](BUILD_WINDOWS.md), synthetic data and production implementations; neither needs game resources, a window or a graphics device. They provide evidence for later decisions. They do not define coverage percentages or timing budgets.

## Coverage

### Collect

```powershell
.\tools\test.ps1 -Configuration Debug -Coverage
```

`-Coverage` is available for the MinGW toolchain only. The build helper configures `MATRIXGAME_COVERAGE=ON` in a separate `build/mingw-debug-exe-coverage` directory, so ordinary build directories stay uninstrumented. The option adds GCC's `--coverage` to the engine, library and test targets; the zlib, libpng, libogg and libvorbis dependency builds keep their own flags. The test helper builds the `matrixgame_tests` target, removes previous counter files (`*.gcda`), runs the selected engine tests one at a time, because instrumented executables merge shared counter files on exit, and then calls `tools/coverage_report.py` with the toolchain's `gcov`. The helper uses the Python launcher or a `python` that is not the Microsoft Store alias, and requires Python 3.9 or newer. A `-Filter` expression limits the run in the usual way and is recorded in the report. The `benchmark` label is excluded: its smoke pass checks repeatable work, not behavior.

MinGW Debug is the reference coverage configuration. It compiles without optimization, so executed lines map directly to source lines. A Release coverage build is possible, but optimization merges and removes lines; do not compare its percentages with Debug. CI runs the same command without supplied resources and uploads `coverage.json`, `coverage-summary.md` and the JUnit report as the `coverage-MinGW-Debug-EXE` artifact. The summary also appears on the job page. No coverage threshold is applied. For a pull request, the checked-out and recorded revision is GitHub's merge commit of the branch into its target, not the branch head.

### Report contents

The collector writes `coverage/coverage.json` and `coverage/coverage-summary.md` in the build directory. The report identifies:

- the revision checked out when the report was written and whether tracked files had uncommitted changes;
- the SHA-256 of every test executable that produced the counters, which identifies the measured binaries independently of the checkout;
- the build type, DLL, cheat and coverage options from the CMake cache;
- the compiler and `gcov` versions, which must match; the compiler defaults to the one in the CMake cache;
- the number of tests run, failed, in error and skipped, the filter and excluded labels;
- the number of notes and counter files used, and the notes ignored because they do not belong to the measured objects;
- totals for the engine (`MatrixGame/src`), base (`MatrixLib/Base`), graphics (`MatrixLib/3G`) and bitmap (`MatrixLib/Bitmap`) areas and for all production code combined;
- per-file line, function and branch totals, unexecuted line ranges and the execution count of every function.

The denominator is every executable line that `gcov` reports for production files in every object that the `matrixgame_tests` target builds, as listed by Ninja. This includes all sources of the `MatrixLib`, `MatrixGameInternal` and `MatrixAudio` targets, and production sources compiled directly into a test executable. Notes of other objects are ignored and listed: for example, the game executable's own objects after a full `tools/build.ps1 -Coverage`, which the test run does not rebuild, or objects left behind by deleted or renamed sources. An object that no executed test linked counts as unexecuted; the summary shows these as the difference between files and files with data. Lines are counted once per source line: lines from headers are merged across translation units, and a line is executed if any translation unit or template instantiation executed it. Functions are identified by their demangled symbol and location, so each template instantiation counts as a separate function, while the same inline function compiled into several translation units, and the compiler's variants of one constructor or destructor, count once.

Code outside the production areas is reported separately and never enters the production totals: `tests/`, third-party and generated build-tree files, and compiler or SDK headers. The report also lists production sources that compiled to no executable code, such as disabled implementations, production sources on disk that no target compiles, and the number of headers without executable code. Branch totals exclude exception edges. Translation units that describe a line's branches differently are merged by adding counts position by position and keeping the longer tail. An executed branch therefore never becomes unexecuted, but branch totals for such lines are approximate.

### Failures

The collector stops with a specific message instead of writing a partial report when:

- the directory is not a Ninja coverage build, or the CMake cache or Ninja is missing;
- `gcov` or the compiler is missing, or their versions differ;
- no `.gcno` notes or no `.gcda` counters exist for the test objects, or any compiled C/C++ object of the test target has no note; delete the listed objects or the coverage build directory and rerun `tools/test.ps1 -Coverage`. Resource objects and dependency archives are not instrumented and need no note;
- sources changed after the instrumented build (`source file is newer than notes file`); rebuild with `tools/test.ps1 -Coverage` without `-NoBuild`;
- counters come from a different build than the notes (`stamp mismatch`), or `gcov` output or the JUnit report cannot be read;
- a measured file falls outside every production area and exclusion;
- no production line was executed.

### Inspect a path

Query a collected report for files or functions by regular expression:

```powershell
python -B tools/coverage_report.py --build-dir build/mingw-debug-exe-coverage --query 'Base::CalcCRC32\('
python -B tools/coverage_report.py --build-dir build/mingw-debug-exe-coverage --query '^MatrixGame/src/MatrixSide.cpp$'
```

A function line shows its execution count, and a file line shows its totals and unexecuted ranges. Use these to verify that a new regression reaches the intended path and to find unexecuted branches. Executed code is not necessarily verified: line coverage cannot show whether a test asserts the relevant result. Map coverage to the [behavior catalog](research/75-behavior-baseline/README.md) rather than setting a global percentage.

## Benchmarks

`matrixgame_benchmarks` runs component workloads through production code with synthetic fixtures. It is built with the tests in every configuration. CTest registers only `game.benchmark.smoke`, which runs every workload briefly and checks that it repeats the same work; it has no time limit beyond the usual test timeout.

### Workloads

| Workload | Production path | Work per iteration | Not included |
| --- | --- | --- | --- |
| `navigation.crowd_move` | `CMatrixSideUnit::PGRemoveAllPassive`, `PGAssignPlacePlayer` and `TaktPL`, which issues the robot move orders, then `ZoneCurFind`, `ZoneFindNear`, `ZonePathCalc` and `ZoneMoveCalc` for each ordered robot in logic-list order | One move order for a group of 48 wheeled robots across a 128 x 128 movement grid with 8 x 8-cell zones and three walls with gaps | The acknowledgement sound and place markers of `PGOrderMoveTo`, the rest of the robot tick, movement, steering, collision, road routes and regions |
| `selection.drag_refresh` | `CFormMatrixGame::MouseMove` and `MouseKey`, `CMultiSelection::UpdatePending` and release handling | One 60-frame drag over 600 visible objects, four queued moves per frame | Picking and projection: synthetic objects replace mesh/frustum hit tests, as in the selection regressions |
| `simulation.side_logic_s10` | The side catch-up of `CMatrixMapLogic::Takt` calling `CMatrixSideUnit::LogicTakt` every 10 ms, including group evaluation in `TaktPL` | 100 frames of the S10 schedule with 96 idle player robots in six groups with stop and automatic orders | Object logic, enemy sides, AI sides, effects, interface and presentation; this workload measures the side catch-up component |

The crowd benchmark omits audio data, a device and robot geometry, so its fixture repeats two short pieces of the order and robot tick: the order acceptance before place assignment, and the route branch of the move order in `CMatrixRobotAI::LogicTakt`, which calls the production route functions listed above. The separate battle scenarios supply synthetic geometry and a non-rendering device to run full map ticks, including the [armed scenarios](testing/armed-fixtures.md); their behavior checks do not broaden this benchmark's measured workload. The selection and simulation workloads use the software cursor setting, so the player tick does not call the desktop cursor or a device.

Every sample builds a fresh fixture outside the timed region after seeding the Park-Miller generator with 78. Before each iteration an untimed preparation step resets state that an iteration consumes, such as stopping the crowd and advancing the map clock past the 100 ms group-evaluation interval. Lazy first-use allocations of a fixture, such as the path search buffers, therefore occur inside each timed sample.

### Run

Build the Release tests, which also stages the DirectX runtime component beside the executables, then run the workloads in several fresh processes:

```powershell
.\tools\test.ps1 -Configuration Release
python -B tools/benchmark.py run --output build/benchmarks/reference.json
```

The default is five processes, each with three discarded warm-up samples and fifteen timed samples. Use `--workload NAME` to select workloads, `--processes`, `--warmup` and `--repetitions` to change the repetition, and `--build-dir` for another configuration. The wrapper refuses a coverage build because instrumentation distorts timing, and stops a process that runs longer than `--timeout` seconds (600 by default). The executable can also be run directly; `--list` names the workloads and `--output FILE` writes the single-process JSON.

Each workload first runs an untimed reference pass in which every iteration must repeat the work counters of the first iteration. Every later sample must repeat the reference total, or the run fails. After the reference pass, each process records untimed inspection counters once: the number of random draws consumed, counted through the public generator interface, the number of movement cells visited by the path searches of one order, and the fixture size. The wrapper requires every process to report the same work and inspection counters. The result records:

- revision, uncommitted-change flag, SHA-256 of the benchmark executable, CMake options, compiler, configuration, architecture, processor, timer frequency, x87 control word and operating system;
- warm-up, repetitions, processes, iterations per sample and workload parameters;
- every sample in nanoseconds grouped by process, each process median, and the minimum, 10th percentile, median, 90th percentile, maximum and median absolute deviation (MAD);
- work counters, inspection counters and allocation counts per sample.

Only the iterations are timed and counted. Allocation counts replace the benchmark executable's CRT imports `malloc`, `calloc`, `realloc` and `free`; `realloc` to zero bytes counts as a release. They therefore include the engine heap, which uses these functions, and C++ allocations. Allocations inside other DLLs and CRT-internal helpers are not counted. In Debug, tracked engine-heap blocks carry diagnostic headers, so requested bytes differ from Release. Counts are reported, not used as pass/fail criteria; the comparison shows every allocation counter that changed and whether any counter varied between samples.

### Compare

```powershell
python -B tools/benchmark.py compare build/benchmarks/reference.json build/benchmarks/candidate.json
```

The comparison first requires the same compiler and version, configuration, architecture, engine heap tracking, CMake build type and DLL, cheat and coverage options, processor and logical processor count, timer frequency, x87 control word, timer, sample unit and allocation-counter scope, and the same workload parameters, iterations and work counters. Differences in the operating system version and in warm-up, repetitions or processes are listed as notes; the executable hash is recorded but expected to differ between revisions. Different work means the change altered behavior or the workload, so timing is not compared; the command exits with status 2. Workloads that exist only in the candidate are listed but not compared. Use `--allow-identity-mismatch` only for an explicitly cross-configuration investigation.

For comparable workloads, the comparison computes a 95% hierarchical percentile bootstrap interval of the ratio of the candidate median to the base median, using 2,000 resamples and a fixed seed. Each resample first draws processes with replacement and then samples within each drawn process, so a difference between processes widens the interval instead of being averaged away. With the material threshold `t`, 5% by default:

- **regression:** the whole interval is above `1 + t`, and every candidate process median is above every base process median;
- **improvement:** the reverse;
- **no material change:** the whole interval is within `1 - t` to `1 + t`;
- **inconclusive:** anything else; repeat with more processes or samples on a quieter machine.

The process condition prevents one disturbed process from producing a regression. The command exits with status 1 when any workload regresses and 0 otherwise.

### Noise and the reference

Timings are comparable only on the same machine and configuration. Before comparing a change, run the reference revision twice and compare the results with each other. If this A/A comparison is not `no material change`, reduce background load, use the same power plan, or increase processes and repetitions before interpreting a candidate. Choose `--threshold` above the A/A interval, and record the threshold with the comparison. `--validation-delay-us N` adds a fixed busy wait once to every timed sample and is recorded in the result. Use it only to check that the comparison detects a change of known size.

To refresh the reference, check out the accepted reference revision and build its Release tests, then run the workloads and keep the result outside the repository, for example as a CI artifact or attached to the tracking issue. Measure the candidate on the same host and compare. Reference results are machine-specific; do not commit them as budgets.

## Active combat and computer strategy

The dedicated `matrixgame_battle_benchmarks` executable adds two bounded CPU workload families through the same runner, report schema and comparison rules above. Each has independent ParkMiller/LegacyCRT and 10/16 ms profiles. These representatives complement the existing movement/idle-player-side measurements; selecting them does not accept a final reference or establish complete G4, native frame cost, all weapon types, arbitrary army sizes or every strategy branch.

`combat.laser_tick.{park_miller|legacy}{10|16}` reuses the [laser scenario](../tests/combat_scenario_tests.cpp)'s active prefix: seed 82, the original four wheel robots and obstacle geometry, one player laser, range 600, cooldown 100 ms, and a target with 50 internal health units. The normal context-target command runs during untimed setup. The timed interval is exactly 100 advances/1,000 ms at 10 ms or 62 advances/992 ms at 16 ms, preserving the original integer-loop boundary. It requires nine/eight real health decreases of two internal units at the established exact impact times, positive fire/hit state and a retained live target. The later shooter-removal and inactive continuation remain in the original regression test and are excluded from this active workload.

`strategy.capture_tick.{park_miller|legacy}{10|16}` reuses the [no-loss strategic capture journey](testing/84-strategic-capture.md): seed 84, two stationary player robots, two computer robots, the ordinary three-team configuration, a computer base and two neutral factories on `MultiRegion`. The full 48,000 ms interval contains actual computer Forward movement into the adjacent region, Capture action and orders, paint, both ownership transitions and later Defence. There are no injected human commands or team actions. Zero resources and 100,000 ms income intervals keep the initial population fixed while ordinary strategy remains enabled; purchasing/deployment and armed AI combat are separate scopes. Candidate processing can supply an individual capture order before regional Capture, so the workload preserves that route rather than imposing a new dispatch order.

`prepare()` destroys the previous fixture, seeds/rebuilds the next one and issues its initial command outside timing. `run()` measures complete production map ticks through `Session::advance()` plus the declared fixed scalar observations, assertions and typed hash updates. It includes the fixture's CPU camera/frustum update; audio is unavailable. Native drawing, trace serialization and trace/report I/O are excluded. Observation overhead is part of the measured cost; the results do not isolate pure production subsystem time. Fixed population and frame limits, the existing per-Session wall guard and the wrapper's process timeout bound execution. Smoke registrations use the ordinary 30-second timeout; these are failure bounds rather than accepted performance budgets.

Work counters report actual hit counts/times, damage, RNG draws and typed state hashes, or occupied-team phase frame counts, physical region crossing and actual capture/paint/ownership times. Phase counts are observations in simulated frames, can include multiple occupied teams per frame, and are not separate CPU timings. Untimed inspection checks that retaining a typed trajectory preserves work/RNG observations, then compares two complete fresh-session typed trajectories exactly and reports their word count. Hashes provide compact work diagnostics; the direct typed comparison supplies the repeatability oracle. The existing runner requires equal work across every iteration/sample and equal work/inspection across fresh processes. Both families require fresh state before advancing: the laser checks initial full health and strategy requires frame/time zero and untouched progression counters. `--iterations 2` therefore rejects a missing fixture reset and doubles the measured journey work.

For `--iterations N` above one, every `work_per_sample` counter is summed across the N fresh journeys, including milestone timestamps, float bit patterns and hash words. Those sums remain equality evidence; use `--iterations 1` for readable per-journey work values. `inspection` still describes a single verification journey and its exact replay checks rather than an N-journey sum.

Build and measure the selected executable explicitly; stop after any failed preparation/run so a stale report is not compared:

```powershell
.\tools\test.ps1 -Configuration Release -Filter '^game\.benchmark\.(combat\.laser_tick|strategy\.capture_tick)\.'
if ($LASTEXITCODE -ne 0) { throw 'Active workload smoke checks failed.' }
python -B tools/benchmark.py run --executable build/mingw-release-exe/tests/matrixgame_battle_benchmarks.exe --processes 3 --iterations 1 --output build/benchmarks/active-a.json
if ($LASTEXITCODE -ne 0) { throw 'First active workload measurement failed.' }
python -B tools/benchmark.py run --executable build/mingw-release-exe/tests/matrixgame_battle_benchmarks.exe --processes 3 --iterations 1 --output build/benchmarks/active-b.json
if ($LASTEXITCODE -ne 0) { throw 'Second active workload measurement failed.' }
python -B tools/benchmark.py compare build/benchmarks/active-a.json build/benchmarks/active-b.json
if ($LASTEXITCODE -ne 0) { throw 'Active workload comparison failed.' }
```

Inspect every A/A verdict: exit zero also permits `inconclusive`, which does not establish the required `no material change` condition. Keep the existing comparison policy; final integrated measurements, accepted historical reference selection and native execution remain separate gates. These synthetic scenes do not replace them.

## Navigation operation counts

[Scoped navigation work observations](testing/81-navigation-work.md#crowded-scene-consumers-and-reports) now report crowded-destination and opposing-flow scenes through the dedicated `matrixgame_navigation_work_benchmarks` executable, using the same runner and JSON/compatibility rules as the component workloads above. The existing `navigation.crowd_move` component benchmark still reports its search-mark footprint; its workload and results are unchanged.

Build the Release tests and stage the test runtime with the supported helper, then select the new executable explicitly:

```powershell
.\tools\test.ps1 -Configuration Release -Filter '^game\.navigation\.work_'
python -B tools/benchmark.py run --executable build/mingw-release-exe/tests/matrixgame_navigation_work_benchmarks.exe --iterations 1 --output build/benchmarks/navigation-work.json
```

Use `--workload navigation.crowded_destination.park_miller10` to select one profile; `--list` on the executable lists all eight scene/RNG/timestep combinations. The RNG labels have identical zero-draw executions for each scene/step pair, giving four distinct measured workloads. Each iteration starts a fresh fixture outside timing and runs exactly 32,000 simulated milliseconds. Reported `work_per_sample` contains actual completed ticks, elapsed simulation, arrived robots and RNG draws. Timing keeps collection inactive and omits full snapshots, but includes the scene's per-frame contract observations and result bookkeeping; it is not a production frame-cost or collector-overhead claim.

Untimed inspection reruns matching inactive/enabled scenes, compares their complete observations and exports every `NavigationWork::Counters` field directly. Fields use the [ten declared operation units](testing/81-navigation-work.md#counter-units), with `interval_0.*`, `interval_1.*`, `interval_2.*` totals for `(0,4000]`, `(4000,12000]`, `(12000,32000]` ms. Only `Session::advance()` is captured, including its production map tick and existing CPU camera refresh; setup, initial commands, state observation and reporting are excluded. Counts cover the whole map, including stationary opponents, and are not instruction counts. First movement/arrival times, assigned cells, final coordinate bits, minimum-separation bits and a split FNV-64 trajectory digest identify the measured execution. The existing runner requires those inspection values to repeat across fresh processes. Preserve the result identity and raw reports; this consumer does not freeze a historical reference or accept N5/the whole baseline.

## Native frame measurements

Device-backed frame timing needs supplied resources and a desktop session, so it is a manual measurement. It is not part of CI. Do not use synthetic benchmark results in its place.

1. Build the standalone Release EXE with local resources: `.\tools\build.ps1 -Configuration Release`.
2. Record the revision, configuration, display mode, resolution, refresh rate, GPU, driver, map and the standalone settings in use. The engine passes the measured frame time to the simulation in whole milliseconds, capped at 100 ms; see the [time and random-state investigation](research/76-battle-determinism/README.md).
3. Start the same battle from a fresh launch and let it run for 30 seconds without input as warm-up.
4. Capture present intervals for 60 seconds with an ETW-based frame capture tool such as PresentMon, filtering on `MatrixGame.exe`. Record the tool name and version.
5. Repeat the capture three times from fresh launches. Report the median, 90th and 99th percentiles of the present interval for each capture, as well as the number of frames, rather than an average frame rate.

Compare native captures only with captures of the same scenario on the same hardware and settings.
