# Research #76: time and random-state control for battle comparisons

## Summary

A battle is reproducible bit-for-bit only when every input is fixed. The inputs are the revision, compiler, configuration, x87 floating-point control word, random mode and seed, integer frame schedule, command schedule, camera placement, audio availability and drawn-frame activity. Under those conditions, repeated runs of production code produced identical traces in every trial (3 trials per schedule, Debug and Release). Changing any one of these inputs changes later simulation state, and the experiments below locate the first differing state or event for each one.

The main measured findings:

- **Frame schedule.** It is part of the result. Splitting the same 3000 ms of simulated time into different frame lengths changed production effect state from the first comparable instant. At the end of the lifetime it also changed how many random draws were consumed (112, 110 or 108).
- **Render and presentation calls draw from the simulation stream.** Draw-time effect calls, effect creation gated by distance from the camera, and sound playback all take values from the same random stream as the simulation. One laser effect added exactly one draw per drawn frame, so the draw position depends on frame rate. With an audio device present, each non-positional sound played added 2 draws; without one it added none.
- **Device creation changes floating-point precision.** The standalone device is created without `D3DCREATE_FPU_PRESERVE`. In a test this switched the thread's x87 control word from 0x037F (64-bit precision) to 0x007F (24-bit precision), and it stayed that way after the device was released. Headless tests therefore compute with different precision than the running game. A test-only `_control87(_PC_24, _MCW_PC)` call reproduces the in-game results exactly without a device. Under that setting, MinGW Debug and Release effect traces were bit-identical.
- **Compiler and optimization change the random helpers' floating results.** Production `random.cpp` gives different double-precision `RND(a,b)` results for GCC x87 at `-O0`, GCC x87 at `-Os`, and any SSE2 build (MSVC or GCC SSE). The largest absolute difference was 3.6e-15. Integer helpers and float-returning helpers matched across all builds tested.

Fully headless replay is feasible as a self-consistent reference: production code, a fixed schedule, no rendering. It is not equivalent to an interactive game. No existing narrow surrogate preserves the game's render-dependent random consumption.

Recommendation:

- Drive `Takt(int)` with explicit integer schedules: 10 ms as the primary, 16 ms as a secondary.
- Seed explicitly, run both random modes, and fix camera, audio and x87 precision as recorded scenario inputs.
- Record JSON Lines traces with per-frame hashes.
- Compare in three tiers: exact within one configuration; across MinGW configurations, discrete state exact and floats within a per-observable bound scaled by operand magnitude, only until the first discrete divergence; and invariant-only checks across compilers, architectures or schedules.

Splitting the random stream, adding a fixed-timestep accumulator, and preserving FPU precision would each change observable gameplay. They are listed as maintainer decisions, not proposed for the baseline.

## Revision and environment identities

| Item | Value |
| --- | --- |
| Source | `origin/dev` at `540251d4662c590bad4684df01e0c521e6c4aee7` (merge of #59), branch `research/76-battle-determinism`, clean tree after experiments |
| MinGW | winlibs GCC 13.2.0 `i686-ucrt-mcf-dwarf` r3. Target defaults are `-march=pentiumpro -mfpmath=387`, no SSE, `__FLT_EVAL_METHOD__ 2` |
| Project flags | Debug has no `-O` (GCC `-O0`) plus `-D_DEBUG`. Release is `-O3 -DNDEBUG -Os` (effective `-Os`) plus `ASSERT_OFF`. Both use `-ftrivial-auto-var-init=zero` (`CMakeLists.txt:32-80`) |
| CMake | 4.4.4 (pinned i386 build) |
| MSVC | 19.51.36252 for x86. Used only for standalone `random.cpp` and replica kernels; no DirectX SDK was available for an MSVC engine build |
| DirectX | `d3dx9_43.dll` SHA256 `0B28546BE22C71834501F7D7185EDE5D79742457331C7EE09EFC14490DD64F5F`; system `d3d9.dll` (WOW64) 10.0.26100.8972 |
| Host | Windows 11 Pro 10.0.26200, Intel Core Ultra 9 275HX, Intel Graphics |
| Builds | `tools/build.ps1 -WithoutResources` for Debug and Release (`build/mingw-debug-exe`, `build/mingw-release-exe`) |
| Baseline suite | CTest `-L ^engine$`: 172/172 passed in Debug and Release. `random` tests repeated 5 times: 23/23 passed in both |
| Game resources | None used. All experiments use synthetic production objects; `robots.pkg` and `robots.dat` were not opened |

## Findings

### Time source and timestep

1. **Variable, wall-clock-driven step.** `L3GRun` measures `high_resolution_clock` deltas, multiplies them by 4 under `GFLAG_4SPEED`, truncates to whole milliseconds, caps at 100 ms and passes the result to `SRemindCore::Takt` and the form's `Takt`. It then draws once per iteration (`MatrixLib/3G/3g.cpp:358-408`). The truncation remainder is discarded (`prev_takt = cur_takt`), so a 16.7 ms vsync frame becomes 16 ms of game time (source inference; not measured in a live loop). Frames longer than 100 ms slow game time relative to wall-clock time. Startup presentation uses `D3DPRESENT_INTERVAL_ONE`, and the reset parameters use `D3DPRESENT_INTERVAL_IMMEDIATE` (`3g.cpp:184`, `3g.cpp:225`), so frame pacing depends on the display, the driver and the device's history.
2. **How a frame is split inside `CMatrixMapLogic::Takt`** (`MatrixGame/src/MatrixLogic.cpp:2927-3024`):
   - Game time advances by the whole step.
   - `GatherInfo` (enemy look-up for AI robots, `MatrixRobot.cpp:4408`) runs when more than 100 ms have passed since the last run (`MatrixLogic.cpp:2980`).
   - Object logic runs as `ProceedLogic(10)` once per complete 10 ms, plus one call for the remainder (`MatrixLogic.cpp:2989-2999`).
   - Side logic catches up afterwards in fixed 10 ms ticks (`MatrixLogic.cpp:3003-3011`).
   - Graphic takts follow (`MatrixLogic.cpp:3024`).

   Robot movement scales by `m_SyncMul = ms / 10` (`MatrixRobot.cpp:373`), so remainder calls are variable-step integration. Applying the source model (`experiments/partition_model.py`) to 1000 ms of game time shows that schedules change the call pattern but not the side tick count:

   | Frames | `ProceedLogic` calls (ms: count) | Side ticks | Max side ticks per frame | `GatherInfo` count and spacing |
   | --- | --- | --- | --- | --- |
   | 10 ms | 10:100 | 100 | 1 | 9, every 110 ms |
   | 16 ms | 10:62, 6:62, 8:1 | 100 | 2 | 9, 104-112 ms |
   | 33 ms | 10:91, 3:30 | 100 | 4 | 7, 132 ms |
   | 100 ms | 10:100 | 100 | 10 | 5, every 200 ms |
   | 16 ms with one stall | 10:66, 6:56, 4:1 | 100 | 10 | 8, 112-148 ms |

   This is a model of the control flow, not a production run. A full `CMatrixMapLogic::Takt` cannot run headlessly yet; see Coverage gaps.
3. **Other wall-clock inputs.**
   - The standalone and DLL entry points seed from `time(nullptr)` (`MatrixGame.cpp:75`, `MatrixGameDll.cpp:131`).
   - `CGame::Init` already accepts the seed as a parameter (`MatrixGame.cpp:195`).
   - Side `STAT_TIME` uses `timeGetTime` minus a `timeGetTime` start (`MatrixSide.cpp:447-449`, `MatrixMapPrepare.cpp:1564`, `MatrixMapPrepare.cpp:1759`). It reaches the statistics dialog and the host result (`MatrixGame.cpp:849`) but was not found in any decision path.
   - The developer `hTestSpdTrace` command reseeds with 1 (`DevConsole.cpp:100`).
4. **Pause.** While paused, the map runs only `PauseTakt`, cursor, interface and camera work and returns before game time advances (`MatrixLogic.cpp:2882-2919`). Draw-time random calls are guarded by `IsPaused()` (measured: zero consumption while paused).

### Random streams and consumers

1. **One process-wide stream.** Park-Miller is a `std::minstd_rand0` in an anonymous namespace; LegacyCRT is `std::rand` (`MatrixLib/Base/random.cpp:12-47`). Neither exposes a state or a draw count.
   - Park-Miller state can be recovered and restored through the public API alone: one draw reveals the next state, and `random::seed(prev)` restores the previous one. The probe used this to read exact draw indices without changing production code.
   - LegacyCRT state is not observable. A foreign `std::rand()` call shifted the stream: seed 5 gives 54, 28693 normally and 54, 12255 with one intervening `rand()`.
2. **Inventory.** Static call-site inventory, approximate and including commented-out sites, in 67 functions: about 185 direct `RND`/`FRND`/`FSRND`/`IRND` sites plus 63 calls through the map wrappers. They fall into five groups:
   - **Simulation:** robot, cannon, building and map-object `LogicTakt`/`Damage`/`DIPTakt`, side orders and AI, `Logic/MatrixRule.cpp`, flyers.
   - **Effect construction and takt:** explosions (about 19 sites per constructor, debris-proportional), weapons, moving objects, smoke and fire, dust.
   - **Draw-time:** `CLaser::BeforeDraw` (`Effects/MatrixEffectWeapon.cpp:795-797`), `CVolcano::Draw` (`MatrixEffectWeapon.cpp:824-828`), `CMatrixEffectFireStream::Draw` (`Effects/MatrixEffectSmokeAndFire.cpp:372-373`), `SWeaponRepairData::Draw` (`MatrixRobot.cpp:38-42`).
   - **Visibility-gated graphic takt:** `CMatrixMapGroup::GraphicTakt` (`MatrixMapGroup.cpp:703`); robot `Takt` creating keel-water billboards and dust (`MatrixObjectRobot.cpp:885`, `MatrixObjectRobot.cpp:893`); portrait map objects (`MatrixObject.cpp:638`).
   - **Camera and audio:** auto-fly (`MatrixCamera.cpp:463`); non-positional `CSound::Play`, which draws twice only when audio is available (`MatrixSoundManager.cpp:766-772`).
3. **Camera gates on effect creation.** Eleven effect creators return early when the effect is more than 3000 units from the camera frustum center. They include explosions, smoke, fire, landscape spots, point lights, lightning and dust (`Effects/MatrixEffect.cpp:444, 496, 523, 547, 585, 733, 760, 782, 831, 889, 923`; `MAX_EFFECT_DISTANCE_SQ` at `Effects/MatrixEffect.hpp:15`). Whether these effects exist, and therefore consume draws, depends on camera position.
4. **Commands consume random draws.** `PGOrderMoveTo` draws `IRND` for the acknowledgement voice and then plays it (`MatrixSide.cpp:9053`, `MatrixSide.cpp:9069`). Right-click and left-click movement of flyers draws `RndFloat` around the cursor trace position (`MatrixSide.cpp:707`, `MatrixSide.cpp:883`).

### Effect and render coupling

1. **Graphic work inside the simulation step depends on the last drawn frame.** `CMatrixMap::Takt` runs inside the simulation step (`MatrixMap.cpp:2410-2470`), and parts of it use the visibility computed by the previous `BeforeDraw` (`CalcMapGroupVisibility` and `Sort`, `MatrixMap.cpp:1326-1350`):
   - graphic `Takt` for the sorted visible objects in camera order (`MatrixMapStatic.cpp:719-728`);
   - `GraphicTakt` for visible map groups (`MatrixMap.cpp:2428`);
   - takts for effect spawners and all effects (`MatrixMap.cpp:2443`, `MatrixMap.cpp:2467`).
2. **Animation may affect gameplay (source inference, not measured).** Non-pneumatic robot animation advances only in that visible-object graphic `Takt` (`MatrixObjectRobot.cpp:900`); pneumatic chassis animate in logic (`MatrixRobot.cpp:339`). Weapon fire origin uses the animated unit matrix (`MatrixRobot.cpp:1370`). Visibility and animation state may therefore influence firing geometry. In a headless fixture `BeforeDraw` never runs, so the visible sets are empty.
3. **Player commands come from rendered picking.** The cursor's world position and object are traced in `BeforeDraw` (`MatrixMap.cpp:1120`) and read by mouse-order handlers (`MatrixSide.cpp:824-893`).
4. **Effect caps.** Effect population is capped at 1280 with priority eviction (`MatrixMap.hpp:755-791`). There are also per-type caps: 50 explosions and 100 smoke/fire effects (`Effects/MatrixEffect.hpp:175-177`). Cosmetic effects therefore compete with gameplay-relevant effects only by priority, and their presence depends on the camera gates above.

### Iteration ordering

Object logic and effects use intrusive insertion-ordered lists (`LIST_ADD` at `MatrixLib/Base/BaseDef.hpp:98`), and deletion during iteration goes through `m_NextLogicObject` and `m_EffectsNextTakt`. No pointer-keyed ordered or hashed containers were found in simulation code; the `unordered_map` uses are confined to the audio service. Render-side sorting by camera depth (`MatrixMapStatic.cpp:766`) controls the order of graphic takts, so it reaches the random stream through the consumers above. Ordering is deterministic given identical history and camera; repeated runs measured identical.

### Floating point and compiler variation

1. **x87 evaluation.** The pinned MinGW target evaluates float and double in x87 registers. Results therefore depend on spill points, which differ between `-O0` and `-Os`. MSVC x86 defaults to SSE2.
2. **Device precision (measured).** `CreateDevice` passes `D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED` without `D3DCREATE_FPU_PRESERVE` (`3g.cpp:188-193`). A console probe created a device with these flags on a hidden, never-shown window. The control word went from 0x037F to 0x007F and stayed there after release. With `D3DCREATE_FPU_PRESERVE` it stayed at 0x037F. The x87 setting thus differs between headless tests and the game thread, and gameplay arithmetic after device creation runs at 24-bit precision in MinGW builds. A DLL host supplies its own device and flags (not measured).
3. **D3DX runs in simulation code.** D3DX math from `d3dx9_43.dll` is used in simulation paths, for example `D3DXVec3Normalize` in `GatherInfo` (`MatrixRobot.cpp:4436`). Whether its results depend on the processor was not measured; only one CPU was available.
4. **Uninitialized stack values (not investigated).** MinGW zero-initializes automatic variables and suppresses uninitialized warnings (`CMakeLists.txt:58`, `CMakeLists.txt:78`). MSVC Debug uses `/RTC1`. Any uninitialized stack read could therefore differ between compilers.

### Command ordering

Windows messages are dispatched before the frame's `Takt` (`3g.cpp:362-365`, `3g.cpp:402`). Order handlers run immediately and consume random draws, so command order within a frame matters. Commands take effect at the next frame boundary in whatever game time the wall clock produced. The pending selection rectangle is evaluated once at the start of the form `Takt`, before map simulation (`MatrixFormGame.cpp:358-361`). A test schedule must therefore define commands as an ordered list per frame index, submitted between frames. It should use production order functions with logical targets rather than rendered cursor picking, consistent with #77.

## Experiments

All experiments use production code linked from the selected build, or production `MatrixLib/Base/random.cpp` compiled standalone. Sources and raw outputs accompany this report under `experiments/`. Replica kernels are labelled as such and are not production code.

### Setup

1. Build Debug and Release: `.\tools\build.ps1 -Configuration Debug -WithoutResources`, then the same with `-Configuration Release`. Add `-ToolchainRoot` and `-CMakeRoot` if the pinned tools are elsewhere.
2. Apply the research-only access patch and register the probe temporarily, then revert both. The patch adds one `friend struct SmokeObserver;` declaration to `CMatrixEffectSmoke`, so the probe reads the smoke effect's own private members instead of reinterpreting the object as another type: `git apply --unidiff-zero <path>/smoke_observer.patch`. Then append to `tests/CMakeLists.txt`:

   ```cmake
   add_matrix_test_executable(matrixgame_determinism_probe <path>/determinism_probe.cpp diagnostic_overlay_stub.cpp)
   target_link_libraries(matrixgame_determinism_probe PRIVATE MatrixGameInternal)
   ```

   Build it with `cmake --build build/mingw-<cfg>-exe --target matrixgame_determinism_probe`, putting the pinned MinGW `bin` on `PATH`.
3. Run with `.tools/directx-x86` on `PATH`; the probe imports `d3dx9_43.dll`. The probe creates a synthetic `CMatrixMapLogic` through the engine heap, no device and no resources. It drives the production effect loop and `CLaser::BeforeDraw`, and reads exact Park-Miller draw indices through the public `seed` API. Compare traces with `python -I -B compare.py <reference> <others...>`.
4. The smoke outputs were regenerated through that observer on this branch's base revision. All 45 committed smoke traces, field dumps and PC24 traces were byte-identical to the earlier observation.
5. Build `rng_dump.cpp` together with production `MatrixLib/Base/random.cpp` using the project language mode: `g++ -std=gnu++20 -O0 -ftrivial-auto-var-init=zero -I MatrixLib/Base rng_dump.cpp MatrixLib/Base/random.cpp`, and the same with `-Os`. Then compare the two dumps with `ulp.py` and `ulp_scale.py`. The language mode matters: with `-std=c++20`, GCC applies standard excess-precision rounding and the `-O0` and `-Os` dumps are identical.

### Results

| # | Command (probe arguments) | Varied | Result |
| --- | --- | --- | --- |
| E1 | `smoke 10`, `smoke 16`, `smoke jitter`; 3 trials each, Debug and Release | Nothing (repeatability) | Byte-identical traces in every trial |
| E2 | `smoke S` for S = 10, 16, 20, 25, 40, 50, 80, 100, `jitter` | Frame schedule | Smoke state differed from the first common instant (t = 20, 50, 80, 100 or 200 ms). Total draws: 112 at 10/16/20/40/80 ms, 110 at 25/50 ms and jitter, 108 at 100 ms. The first draw-count difference is at t = 2500 ms: spawns due inside the final frame before the effect expires are dropped (`MatrixEffectSmokeAndFire.cpp:79-90`). Puff-count differences start at 720-880 ms for 16/40/80 ms and jitter. Max `dz` at equal counts was 0.10-1.06 |
| E3 | `smoke 10 lasers=1`, `lasers=3`, `smoke 20 lasers=1` | Drawn-frame consumer | Exactly +1 draw per laser per frame: 412 and 1012 draws versus 112 for 300 frames, 262 for 150 frames. The first difference is the puff angle and size drawn in frame 1 (t = 10 ms); puff heights were unaffected (max `dz` 0) |
| E4 | `smoke 10 lasers=1 pause` | Pause flag | Laser consumed 0 draws; trace identical to `smoke 10` |
| E5 | `smoke 10 far` (smoke 4000 units from the frustum center) | Camera distance | `CreateSmoke` created no effect: 0 draws versus 112 |
| E6 | `sound on`, `sound off` (fake `Audio::Service` through `SoundBridge::install`) | Audio availability | 10 `CSound::Play(S_BCLICK)` consumed 20 draws versus 0; the next raw value was 830221802 versus 13059038 |
| E7 | `crt` | Foreign `std::rand()` in LegacyCRT | Stream shifted (second value 12255 instead of 28693) |
| E8 | Same `smoke S` trace, Debug versus Release; `dumpat=T` for field dumps | Optimization level at the x87 default | Identical draws and puff counts for all 9 schedules. The first difference (t = 16-480 ms) is the puff `scale` from `LERPFLOAT`, by 1 float ulp (for example `4088c6f2` versus `4088c6f3`) |
| E9 | `smoke S pc24` for S = 10, 16, jitter; Debug versus Release, and versus non-pc24 | x87 precision set to the in-game value | Debug and Release bit-identical. Against the 64-bit headless run, differences start at the first frame, with max `dz` 6e-8 to 1.9e-6 |
| E10 | `rng_driver.exe ParkMiller\|LegacyCRT` built from production `random.cpp` with GCC `-O0`/`-Os`/`-O2`/SSE and MSVC `/Od`/`/O2`/`/arch:IA32`; the probe's `rng` mode on the engine libraries | Compiler and flags | `Rnd()`, `Rnd(a,b)` (including the full 32-bit range), `IRND`, `FRND` and `FSRND` hashes matched in all builds (1M draws each). `RndFloat()` matched across x87 builds but differed from SSE2 builds in 228 of 1M values (1 ulp). `RND(a,b)` (`std::lerp`) gave three result classes: GCC `-O0`; GCC `-Os`/`-O2`; all SSE2 including MSVC `/arch:IA32`. Max absolute difference 1.8e-15 to 3.6e-15, differing in 44-55% of draws. Engine Debug and Release probes matched standalone `-O0` and `-Os` exactly. Measured in ULPs of the result, GCC `-O0` versus `-Os` differs by up to 25984 ULP, but only near zero: at most 1-2 ULP for results of magnitude at least 1, 30 below 1, 950 below 0.1, and 25984 below 0.001. In every case the absolute difference stays within 1 ULP of the operand scale `max(|from|, |to|)` (`ulp_results.txt`, `ulp_scale_results.txt`) |
| E11 | `fpu_O0.exe`, `fpu_Os.exe`, with and without `preserve` (hidden window, never shown) | Device flags | Control word 0x037F before, 0x007F after `CreateDevice` and after release; unchanged with `D3DCREATE_FPU_PRESERVE`. After creation, `RndFloat`, `RND` and `FSRND` hashes changed and `-O0` and `-Os` agreed; `FRND(128)` was unchanged |
| E12 | `pc24_O0.exe`, `pc24_Os.exe` (`_control87(_PC_24, _MCW_PC)`, no device) | Test-only precision seam | Hashes identical to the post-device values in E11 |
| E13 | `fp_kernel` replica built like E10 | Compiler and flags on replica arithmetic | SSE builds (GCC SSE, MSVC `/Od` and `/O2`) bit-identical. x87 builds differed among themselves (MSVC `/arch:IA32` matched GCC `-O0`). The branchy movement replica diverged macroscopically after a target switch (`px` 1.26, 4.39, 7.33 versus 7.02) |
| E14 | `bench_base.exe` versus `bench_counted.exe` (adds one 64-bit increment per raw draw); 3 runs × 15 repetitions | Draw counter cost | `Rnd()` median 4.43/4.70/4.48 ns baseline versus 4.13/4.02/4.09 ns counted: within noise. FNV-1a over 12.8 KB took about 17 µs |

The earliest divergence is located per varied input: E2 at the first spawn/integration step, E3 at the first `SpawnPuff` after a render draw, E8 at the first `LERPFLOAT` scale, E9 at the first frame, E5 and E6 at the first creation or play call. The harness should locate divergence by per-frame hash plus draw index, then rerun with a field dump at that frame.

## Recommendation

### Time, random and command schedule

- **Time.** Do not run `L3GRun` in comparisons. Drive the production map or form `Takt(int)` with an explicit integer schedule recorded in the scenario.
  - Primary schedule: S10, constant 10 ms. It aligns with `LOGIC_TAKT_PERIOD`, has no remainder calls and one side tick per frame.
  - Secondary schedule: S16, constant 16 ms. It represents truncated 60 Hz frames and exercises remainder calls and two-tick side bursts.
  - Characterization only: 100 ms cap, stall and jitter schedules. They are not cross-compared with the others.
  - The schedule identifier and its hash belong to the test identity.
- **Random.**
  - Seed explicitly through `RandomConfiguration::initialize(seed, options)` or `random::seed(seed, mode)` before map construction; the map constructor consumes one draw (`tests/random_map_tests.cpp`).
  - Run both modes.
  - Record the draw index at every event and frame: a counter seam for both modes (S1 below) or, for Park-Miller only, the zero-change state peek.
  - Forbid `std::rand` in harness code.
- **Environment inputs, recorded in the header:** x87 control word (PC24 to mirror the standalone MinGW game), camera position (frustum center), audio availability (fake device installed or none), and render-consumer policy (`none` for headless). Assert the control word at checkpoints, because native code can change it.
- **Commands.** Use an ordered list keyed by frame index, applied after frame k-1 and before `Takt` of frame k. Submit through production selection and order functions with logical coordinates and stable entity identifiers, never rendered picking. Record per command: index, frame, draw index before and after, acceptance or rejection, and the resulting group order.

### Trace recording format

Use JSON Lines with one record per line:

- **`header`:** revision, compiler and version, configuration and defines, control word, random mode and seed, schedule identifier/hash/length, command-schedule hash, scenario identifier, camera, audio, render policy, resource identity (`synthetic` or package SHA256).
- **`frame`:** index, step, game time, draw index, FNV-1a-64 of a canonical snapshot.
- **`event`:** frame, draw index, kind (command result, order change, spawn, damage, death, capture, resource change), entity identifier, integer fields.
- **`snapshot`:** every N frames, and on demand at the first mismatching frame. Per entity in creation order: identifier, type, side, state enums, integers exactly, floats as hex bit patterns plus decimal.

Exclude or normalize wall-clock fields (`STAT_TIME`, `m_StartTime`). Hashing cost is test-only: about 17 µs per 12.8 KB snapshot, so about 1.7 ms per simulated second at 100 snapshots per second.

### Comparison policy

| Tier | When | Rule | Justification |
| --- | --- | --- | --- |
| A, exact | Same revision, compiler, configuration, control word, mode, seed, schedule, camera, audio and scenario | Bitwise equality of all records; any difference fails at the first differing frame | E1: repeated trials identical; no address-dependent ordering in simulation code |
| A′, exact across MinGW Debug/Release | Same as A, with PC24 set in both | Bitwise; treat as Tier A once confirmed on movement and combat scenarios (#79) | E9: bit-identical on effect code. Until confirmed for broader paths, fall back to Tier B |
| B, discrete exact plus bounded float tolerance | MinGW Debug versus Release at the 64-bit default, or other same-compiler variants | Draw indices, events, integers and counts exact. Any discrete divergence fails with its earliest record. Each compared float needs a stated scale `S`, the magnitude of the operands or range that produce it. Allow an absolute difference of at most 4 ULP of `S`, and only until the first discrete divergence. Do not compare ULPs of the result: near zero a tiny absolute difference spans thousands of them. Observables without a measured scale and bound are compared only discretely until #79 measures them | E10 (`RND`): at most 1 ULP of the operand scale, but up to 25984 ULP of the result near zero. E8 (smoke `scale`, magnitude about 4): 1 float ULP, where result and operand scales coincide. The 4-ULP margin is demonstrated only for these two kinds of observable. Positions, accumulated values and angles have no measured bound yet |
| C, invariants only | MinGW x87 versus MSVC SSE2, x86 versus x64 (#105), different schedules, different camera, audio or render policy | No trace equality. Assert scenario outcomes with explicit decision margins and report first divergence for diagnosis. Never regenerate a reference to absorb a difference | E2, E3, E5, E6, E10, E13: differences from the first step and macroscopic divergence after a branch flip |

## Render/effect surrogate analysis

A surrogate preserves game consumption only if it reproduces, per frame:

- the number of drawn frames;
- the set of active draw-time consumers (lasers, volcanoes, fire streams, drawn repair beams);
- the previous frame's visible map groups and sorted visible objects, which drive graphic-takt consumption and order;
- the camera position for the eleven gated creators;
- audio availability.

The current code meets these requirements only partly:

- `CLaser::BeforeDraw` is device-free and preserved consumption exactly when called (E3).
- `CVolcano::Draw` calls `m_Konus.Draw()` and needs a device.
- `CMatrixEffectFireStream::Draw` and `SWeaponRepairData::Draw` sit in billboard/draw paths that were not verified device-free.
- Visibility comes from `CalcMapGroupVisibility` and `Sort` inside `CMatrixMap::BeforeDraw`, which interleave device work. Running them headlessly would need production restructuring or broad stubs.

Conclusion: no narrow surrogate preserves interactive consumption at 540251d. Use two references instead:

1. A render-free headless reference. It is deterministic, built from production simulation code, and documented as not game-equivalent.
2. Native-device runs with a fixed schedule for presentation coupling (#87).

Making simulation independent of frame rate, camera and audio would require moving presentation consumers onto a separate stream (decision D1). That changes the simulation draw sequence for a given seed and therefore observable gameplay. A fixed-timestep accumulator (D2) would change robot integration (`m_SyncMul`) and `GatherInfo` cadence. Neither is needed for tests, because the harness already chooses the step.

## Minimal test-only seams and costs

| Seam | Production change | Cost | Purpose |
| --- | --- | --- | --- |
| S0: drive `Takt(int)`, seed through the existing `CGame::Init`, `RandomConfiguration::initialize` or `random::seed`, Park-Miller state peek | None | Test-only: one extra modular multiply and reseed per observation | Fixed time, seed and draw index for Park-Miller |
| S1: `random::draws()` 64-bit counter incremented per raw draw (`random.cpp:44-47`) | About 3 lines in `MatrixLib/Base`; no ABI or format change | Within measurement noise (E14) | Draw index in both modes, including LegacyCRT |
| S2: harness sets `_control87(_PC_24, _MCW_PC)` and asserts it | None | Zero | Headless arithmetic equals the in-game MinGW standalone (E12) |
| S3: camera placement in the fixture (sets frustum center; `CMatrixCamera::m_FrustumCenter` is private, `MatrixCamera.hpp:134`) | A narrow fixture friend, like the existing `RobotCommandFixture` pattern, or a public setter; to be decided in #77 | Zero at runtime | Deterministic camera-gated effect creation |
| S4: audio availability through `SoundBridge::install` with a fake device | None (existing) | Zero | Fixed `CSound::Play` consumption (E6) |
| S5: read-only snapshot and event observer | Fixture access only; no production hooks before the baseline | About 17 µs per snapshot (test-only) | Trace records and first-divergence search |

Known obstacles to a headless full map `Takt` belong to #77. They include the status check calling `g_D3DD->SetMaterial` about once per second (`MatrixLogic.cpp:3034`, `MatrixLogic.cpp:3072`) and the interface, water, minimap and camera dependencies inside `CMatrixMap::Takt` (`MatrixMap.cpp:2410-2490`).

## Alternatives considered

- **Replay recorded wall-clock deltas through `L3GRun`.** This reproduces timing but not visibility or draw consumption without a device. It is useful only as a native check.
- **Native hidden-window replay of the whole game.** This is closest to the game, but it depends on the GPU, driver and display pacing. It suits #87 evidence, not asset-free CI.
- **Tolerance-only comparisons.** Rejected: E2, E13 and the shared stream show discrete divergence that no tolerance bounds.
- **Splitting streams or adding a fixed step before the baseline.** Rejected as a behavior change (D1, D2).
- **Building MinGW with `-msse2 -mfpmath=sse`, or adding `D3DCREATE_FPU_PRESERVE`.** Either would remove precision dependence but changes shipped arithmetic (D3). It is relevant to x64 parity (#102, #104, #105), because x64 uses SSE.
- **Hooking or patching CRT `rand`.** Rejected; S1 is smaller and covers both modes.

## Unresolved decisions for the maintainer

These are proposals only.

1. **Reference configuration.** Proposed: MinGW x86 Release, the supported package (`docs/RELEASES.md:3`), with headless PC24 (S2), plus MinGW Debug as Tier A′.
2. **Primary schedule:** S10 only, or S10 plus S16.
3. **Audio availability in the reference:** fake device present (matches a player with `sound.pkg`) or absent.
4. **Camera placement policy** per scenario.
5. **Render-free reference.** Accept the headless reference as the gameplay baseline, with presentation coupling recorded as native-only evidence.
6. **Seam S1.** Approve the draw counter as a pre-baseline narrow seam (#79 boundary).
7. **D1: separate presentation random stream.** Gameplay change; sequence with #94.
8. **D2: fixed-timestep accumulator.** Gameplay change.
9. **D3: FPU precision policy** (`D3DCREATE_FPU_PRESERVE` or SSE). Gameplay change, with x64 implications.
10. **Tier C invariant rules** for MSVC and x64 (#105).

## Bounded follow-up proposals mapped to existing issues

No new issues were filed. Overlap was checked against #68, #69, #75, #77-#87, #94, #99 and #102-#106.

- **#77 (fixtures and seams):**
  - Adopt the frame-boundary command model, stable creation-order identifiers and the S3/S5 access design.
  - Inventory the device-free requirements of `CMatrixMapLogic::Takt` (`SetMaterial`, interface, water, minimap, camera).
  - Reconcile the design with this report before #79.
- **#79 (controlled scenarios):** implement S0, S2, S4 and S5 plus the trace recorder and a first-divergence tool (hash, then dump); if approved, S1. Run S10 in both random modes under Tiers A and A′. Include one negative control per varied input from E2-E6.
- **#75 (catalog):** state the comparison tier per observable. Record presentation-coupled random consumption, camera gates and audio-dependent draws as current behavior quirks, not defects.
- **#78 (measurements):** reuse E14's method for draws per frame, trace overhead and effect-population counters; keep wall-clock thresholds out of unit CI.
- **#99 (external API):** advance time only in integer-ms steps from an explicit schedule, apply commands at frame boundaries in order, and exclude wall-clock statistics from observations.
- **#94 (domain/presentation):** host decision D1 and any movement of presentation draws.
- **#102, #104, #105 (x64):** use Tier C; E10, E11 and E13 show that x87-to-SSE changes are expected. Decision D3 affects x86 parity.
- **#87 (presentation):** native checks for render consumption, the control word under DLL hosting, and behavior after device reset.
- **#81, #82, #84 (movement, combat, AI):** assert decision margins wherever Tier C comparisons are planned.

A possible new decision record, to be authorized separately, would cover x87 precision under Direct3D and the reference precision.

## Coverage gaps

- **Full battle.** No full `CMatrixMapLogic::Takt` or battle ran headlessly; device and interface dependencies prevent it. Robot movement, combat and AI divergence under different schedules is source-modelled, not measured.
- **Live game counts.** Real-battle draws per frame, render consumer counts, frame-time distributions and the ms truncation loss were not measured, because the game was not launched.
- **Visibility and animation.** The visibility-dependent animation effect on fire origin is a source inference.
- **DLL host.** The control word and device flags in a DLL host, and control-word changes after device reset, were not measured.
- **Hardware.** D3DX CPU-dependent paths and other GPUs and drivers were not tested; one machine was used.
- **MSVC and x64.** The MSVC engine build was not measured locally (no DirectX SDK), only `random.cpp` and replica kernels. x64 was not measured.
- **LegacyCRT.** Effect traces in LegacyCRT mode were not run; only helper hashes and the foreign-`rand` check.
- **Probe method.** The probe read private smoke fields through a layout mirror (probe-only technique), and the effect loop replicates the `CMatrixMap::Takt` effect pass rather than calling the whole function.
- **Uninitialized values.** Uninitialized-read differences across compilers were not investigated.
