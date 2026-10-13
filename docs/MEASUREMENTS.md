# Coverage and performance measurements

This guide describes how to measure which production code the automated suite executes and how to time representative engine workloads. Both measurements use the pinned Windows x86 tools from the [build guide](BUILD_WINDOWS.md), synthetic data and production implementations; neither needs game resources, a window or a graphics device. They provide evidence for later decisions. They do not define coverage percentages or timing budgets.

## Coverage

### Collect

```powershell
.\tools\test.ps1 -Configuration Debug -Coverage
```

`-Coverage` is available for the MinGW toolchain only. The build helper configures `MATRIXGAME_COVERAGE=ON` in a separate `build/mingw-debug-exe-coverage` directory, so ordinary build directories stay uninstrumented. The option adds GCC's `--coverage` to the engine, library and test targets; the zlib and libpng dependency builds keep their own flags. The test helper removes previous counter files (`*.gcda`), runs the selected engine tests one at a time, because instrumented executables merge shared counter files on exit, and then calls `tools/coverage_report.py` with the toolchain's `gcov`. A `-Filter` expression limits the run in the usual way and is recorded in the report. The `benchmark` label is excluded: its smoke pass checks repeatable work, not behavior.

MinGW Debug is the reference coverage configuration. It compiles without optimization, so executed lines map directly to source lines. A Release coverage build is possible, but optimization merges and removes lines; do not compare its percentages with Debug. CI runs the same command without supplied resources and uploads `coverage.json`, `coverage-summary.md` and the JUnit report as the `coverage-MinGW-Debug-EXE` artifact. The summary also appears on the job page. No coverage threshold is applied.

### Report contents

The collector writes `coverage/coverage.json` and `coverage/coverage-summary.md` in the build directory. The report identifies:

- the measured revision and whether tracked files had uncommitted changes;
- the build type, DLL, cheat and coverage options from the CMake cache;
- the compiler and `gcov` versions, which must match;
- the number of tests run, failed and skipped, the filter and excluded labels;
- totals for the engine (`MatrixGame/src`), base (`MatrixLib/Base`), graphics (`MatrixLib/3G`) and bitmap (`MatrixLib/Bitmap`) areas and for all production code combined;
- per-file line, function and branch totals, unexecuted line ranges and the execution count of every function.

The denominator is every executable line that `gcov` reports for production files in every object compiled by the coverage build. This includes all sources of the `MatrixLib`, `MatrixGameInternal` and `MatrixAudio` targets, and production sources compiled directly into a test executable. An object that no executed test linked counts as unexecuted; the summary shows these as the difference between files and files with data. Lines from headers are merged across translation units. A line is executed if any translation unit executed it.

Code outside the production areas is reported separately and never enters the production totals: `tests/`, third-party and generated build-tree files, and compiler or SDK headers. The report also lists production sources that compiled to no executable code, such as disabled implementations, production sources on disk that no target compiles, and the number of headers without executable code. Branch totals exclude exception edges. Where translation units disagree about a line's branches, the merge keeps the larger set, so branch totals are approximate.

### Failures

The collector stops with a specific message instead of writing a partial report when:

- the directory is not a coverage build or the CMake cache is missing;
- `gcov` is missing, or its version differs from the compiler;
- no `.gcno` notes or no `.gcda` counters exist;
- `gcov` reports stale or unreadable counters, such as notes and counters from different builds;
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
| `navigation.crowd_move` | `PlaceFindNear`, `FindPathInZone`, `FindLocalPath` and `OptimizeMovePath`, with the arguments that `PGAssignPlacePlayer`, `ZonePathCalc` and `ZoneMoveCalc` pass for a group move | One move order for 48 wheeled robots across a 128 x 128 movement grid with 8 x 8-cell zones and three walls with gaps | Robot movement, steering, collision, road routes and regions |
| `selection.drag_refresh` | `CFormMatrixGame::MouseMove` and `MouseKey`, `CMultiSelection::UpdatePending` and release handling | One 60-frame drag over 600 visible objects, four queued moves per frame | Picking and projection: synthetic objects replace mesh/frustum hit tests, as in the selection regressions |
| `simulation.side_logic_s10` | The side catch-up of `CMatrixMapLogic::Takt` calling `CMatrixSideUnit::LogicTakt` every 10 ms, including group evaluation in `TaktPL` | 100 frames of the S10 schedule with 96 idle player robots in six groups with stop and automatic orders | Object logic, enemy sides, AI sides, effects, interface and presentation; a full map `Takt` is not yet headless |

The selection and simulation workloads use the software cursor setting, so the player tick does not call the desktop cursor or a device. Every sample builds a fresh fixture outside the timed region after seeding the Park-Miller generator with 78. Lazy first-use allocations of a fixture, such as the path search buffers, therefore occur inside each timed sample.

### Run

Build the Release tests, which also stages the DirectX runtime component beside the executables, then run the workloads in several fresh processes:

```powershell
.\tools\test.ps1 -Configuration Release
python -B tools/benchmark.py run --output build/benchmarks/reference.json
```

The default is three processes, each with three discarded warm-up samples and fifteen timed samples. Use `--workload NAME` to select workloads, `--processes`, `--warmup` and `--repetitions` to change the repetition, and `--build-dir` for another configuration. The wrapper refuses a coverage build because instrumentation distorts timing. The executable can also be run directly; `--list` names the workloads and `--output FILE` writes the single-process JSON.

Each workload first runs an untimed reference pass. Every later sample must produce the same work counters, or the run fails. After each sample, untimed inspection counters are recorded: the number of random draws consumed, counted through the public generator interface, the number of movement cells visited by the path searches, and the fixture size. The result records:

- revision, uncommitted-change flag, CMake options, compiler, configuration, architecture, processor, timer frequency, x87 control word and operating system;
- warm-up, repetitions, processes, iterations per sample and workload parameters;
- every sample in nanoseconds, each process median, and the minimum, 10th percentile, median, 90th percentile, maximum and median absolute deviation (MAD);
- work counters, inspection counters and allocation counts per sample.

Allocation counts replace the benchmark executable's CRT imports `malloc`, `calloc`, `realloc` and `free`. They therefore include the engine heap, which uses these functions, and C++ allocations. Allocations inside other DLLs and CRT-internal helpers are not counted. In Debug, tracked engine-heap blocks carry diagnostic headers, so requested bytes differ from Release. Counts are reported, not used as pass/fail criteria.

### Compare

```powershell
python -B tools/benchmark.py compare build/benchmarks/reference.json build/benchmarks/candidate.json
```

The comparison first requires the same compiler, configuration, architecture, CMake options, processor, workload parameters, iterations and work counters. Different work means the change altered behavior or the workload, so timing is not compared; the command exits with status 2. Use `--allow-identity-mismatch` only for an explicitly cross-configuration investigation.

For comparable workloads, the comparison computes a 95% percentile bootstrap interval of the ratio of the candidate median to the base median, using 2,000 resamples and a fixed seed. With the material threshold `t`, 5% by default:

- **regression:** the whole interval is above `1 + t`, and every candidate process median is above every base process median;
- **improvement:** the reverse;
- **no material change:** the whole interval is within `1 - t` to `1 + t`;
- **inconclusive:** anything else; repeat with more processes or samples on a quieter machine.

The process condition prevents one disturbed process from producing a regression. The command exits with status 1 when any workload regresses and 0 otherwise.

### Noise and the reference

Timings are comparable only on the same machine and configuration. Before comparing a change, run the reference revision twice and compare the results with each other. If this A/A comparison is not `no material change`, reduce background load, use the same power plan, or increase processes and repetitions before interpreting a candidate. Choose `--threshold` above the A/A interval, and record the threshold with the comparison. `--validation-delay-us N` adds a fixed busy wait to every timed sample and is recorded in the result. Use it only to check that the comparison detects a change of known size.

To refresh the reference, check out the accepted reference revision and build its Release tests, then run the workloads and keep the result outside the repository, for example as a CI artifact or attached to the tracking issue. Measure the candidate on the same host and compare. Reference results are machine-specific; do not commit them as budgets.

## Native frame measurements

Device-backed frame timing needs supplied resources and a desktop session, so it is a manual measurement. It is not part of CI. Do not use synthetic benchmark results in its place.

1. Build the standalone Release EXE with local resources: `.\tools\build.ps1 -Configuration Release`.
2. Record the revision, configuration, display mode, resolution, refresh rate, GPU, driver, map and the standalone settings in use. The engine passes the measured frame time to the simulation in whole milliseconds, capped at 100 ms; see the [time and random-state investigation](research/76-battle-determinism/README.md).
3. Start the same battle from a fresh launch and let it run for 30 seconds without input as warm-up.
4. Capture present intervals for 60 seconds with an ETW-based frame capture tool such as PresentMon, filtering on `MatrixGame.exe`. Record the tool name and version.
5. Repeat the capture three times from fresh launches. Report the median, 90th and 99th percentiles of the present interval for each capture, as well as the number of frames, rather than an average frame rate.

Compare native captures only with captures of the same scenario on the same hardware and settings.
