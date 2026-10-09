# Testing

Automated engine tests use CTest and small C++ console executables. They run with synthetic data, without game resources or an interactive window. Use the Windows x86 tools from the [build guide](BUILD_WINDOWS.md); building the base library still needs the normal compiler prerequisites.

The project registers tests directly with CMake's `enable_testing` and `add_test`. Optional CDash/dashboard tool discovery is omitted, so configuring the engine suite does not probe Windows registry entries for unrelated memory-checking tools.

## Run the automated tests

From the repository root:

```powershell
.\tools\setup-toolchain.ps1
.\tools\test.ps1 -Configuration Debug
.\tools\test.ps1 -Configuration Release
```

The helper configures the selected build, enables `BUILD_TESTING`, builds the `matrixgame_tests` target and runs every test labelled `engine`. It rebuilds changed test/library code before execution and leaves the standalone game and resource staging to `tools/build.ps1`. A failed test, missing executable or empty selection makes the command fail. `MG_CHECK` assertions remain active in Release.

Command tests link the engine library and import the x86 `d3dx9_43.dll` component. The test helper prepares a checksum-verified copy from Microsoft's June 2010 redistribution package under `.tools/directx-x86` and stages it beside the test executable. The first run needs network access and 7-Zip. For offline preparation, use `tools/setup-directx-runtime.ps1 -ArchivePath <directx_Jun2010_redist.exe>` with the pinned package. This preparation does not install system components.

Run a subset by CTest name, using a regular expression:

```powershell
.\tools\test.ps1 -Configuration Debug -Filter '^base\.crc\.'
```

Use `-NoBuild` when the selected build is already current, such as immediately after a full build. Changing source requires rebuilding before relying on the results.

```powershell
.\tools\build.ps1 -Configuration Debug -WithoutResources
.\tools\test.ps1 -Configuration Debug -NoBuild
```

Both helpers accept `-Compiler`, `-Configuration`, `-DLL`, `-Cheats`, `-ToolchainRoot` and `-Jobs`. Use the same options for building and testing. Relative toolchain paths are resolved from the current PowerShell working directory. For MSVC, set up Visual Studio and the DirectX SDK as described in the build guide:

```powershell
.\tools\test.ps1 -Compiler MSVC -Configuration Release
```

Normal game builds also compile the test executables. `tools/build.ps1 -WithoutTests` sets `BUILD_TESTING=OFF` for a game-only build; the test helper enables it again on its next build.

## Results and direct CTest commands

Each named case runs in its own process with a 30-second timeout. CTest reports the failed case, assertion expression, source location and captured output. The helper writes `test-results.xml` in the selected build directory, replacing the report with the current selection's results; detailed output is in `Testing/Temporary/LastTest.log` below that directory.

MSVC Debug tests retain compiler runtime checks and send fatal CRT reports to standard error instead of waiting for a desktop dialog. Native failures include a best-effort symbolized stack when the executable's PDB is available, then exit with a failure status. This reporting applies to the console tests; interactive game diagnostics are unchanged.

To list or run already-built tests directly:

```powershell
$ctest = '.\.tools\winlibs-13.2.0\mingw32\bin\ctest.exe'
& $ctest --test-dir build/mingw-debug-exe -C Debug -N -L '^engine$'
& $ctest --test-dir build/mingw-debug-exe -C Debug -L '^engine$' --output-on-failure --no-tests=error
```

Always select the matching configuration with `-C` for MSVC's multi-configuration generator. CTest runs binaries already on disk; use the helper to build them first.

CI runs the engine suite after every game-build configuration. Test failures fail the build job and the aggregate Required checks gate. JUnit reports are retained as separate artifacts, including when a test fails. Contribution-policy tests remain a separate check.

## Initial coverage

| Area | Contracts exercised |
| --- | --- |
| Checksums | Empty input, frozen engine checksum fixtures, incremental/one-shot agreement, binary bytes and selected byte ranges |
| Points | Coordinate arithmetic and squared distance |
| Rectangles | Empty bounds, strict interior containment and normalization |
| Storage | Record/column growth, schema copies, deletion/reuse, duplicate parameters, UTF-16 values, legacy bytes, compressed round trips, rejected format tags/versions and partial-load cleanup |
| Random numbers | Original generator sequence, seed normalization, range endpoints, reversed/equal bounds, fractional scales, index bounds and shared-stream consumption |
| AI robot definitions | Head aliases and resource valuation, headless definitions, weapon strength ordering and missing armor-capacity diagnostics |
| Audio | PCM8/16 mono/stereo parsing, rejected chunk/frame metadata, shared clip caching, unique handles, loop flags, gain/balance, failure diagnostics, shutdown order, frontend layers and preserved host callback routing |
| Bomb commands | A moving bomber retains its ground order when the object target is absent, inactive or destroyed; target validation and fixture cleanup use production implementations |
| Lifecycle | Repeated cursor cleanup and reload, empty configuration, form transitions and constructor unwinding, partial game/standalone teardown, cleanup before cache creation, continued cleanup after C++ failures, owned/borrowed graphics references and rejected graphics configuration before window creation |
| Keyboard state | Independent key releases and repeats, focus-loss reset, fresh presses, configured actions and keep-alive activation dispatch without deactivation/resource changes |
| Selection | Queued mouse moves without geometry scans, one refresh of the latest rectangle, release before a frame, reversed/small rectangles, hit masks, the existing 30-object order/limit, cancellation and object teardown before refresh |
| Device recovery | Resource release/reset/restore order, waiting during loss, transient and permanent reset failures and presentation error handling |
| UI font lifetime | Existing font definitions and stable cache identity, owned-interface release, repeated loss/reset notifications, absent fonts, partial creation and callback failure diagnostics |
| Error diagnostics | Omitted secondary messages, errors without an active Debug trace, file/line metadata and missing-file operation/path diagnostics |
| Executable paths | Unicode and long module paths, native image lookup, API failure diagnostics and bounded retry of truncated module queries |
| Compiler/runtime | Catching a C++ exception across a callback boundary, running stack cleanup and reporting an assertion with a normal failure exit while retaining a representative global configuration layout |

The checksum fixtures preserve the engine's existing data compatibility contract. Tests link the production `MatrixLib` target and use its real implementations. The console executables use static MinGW runtimes so they can run outside the compiler environment.

Storage record copies intentionally recreate an empty schema, while moves preserve owned column buffers. Storage objects have single ownership and support moves. The fixture checks preserve the existing STRG versions, ZL03 framing and swap-with-last record deletion order. The storage executable initializes the base services, supplies a console logger and checks for tracked heap leaks in Debug.

To check the configuration-packing operations used by `BUILDCFG` against the tracked text files, run these commands from the repository root after building the tests:

```powershell
.\build\mingw-debug-exe\tests\matrixgame_storage_tests.exe manual.storage.buildcfg
.\build\mingw-release-exe\tests\matrixgame_storage_tests.exe manual.storage.buildcfg
```

This local smoke check parses the interface/data configuration, removes runtime replacements, packs the `if` and `da` roots in memory, restores them and compares their parameter/block trees. It leaves externally supplied resources and `robots.dat` unchanged. The CTest suite uses synthetic fixtures; invoking the console command in a running battle remains a playtest.

The random-number generator uses the original map simulation's Park-Miller sequence (`16807`, modulus `2147483647`), with the output shifted down by one. Seeds are reduced modulo the modulus; a zero result selects state one. Integer results span `0..2147483645`; floating results span the closed interval `0..1`. The fixed integer sequence is portable across the supported compilers. This contract describes the recurrence and normalized seeds; it does not reproduce the original game's startup seed selection or promise per-seed battle parity with Space Rangers 2.

Integer and finite floating bounds are normalized when reversed and include both endpoints. Ordinary integer ranges use the legacy modulo mapping, which carries modulo bias; ranges wider than one generator output combine two draws using 64-bit arithmetic. Equal bounds consume one draw. `IRND(n)` selects an index in `0..n-1` for a positive count and returns zero otherwise. The fractional-scale helpers take double arguments and return float values; their scales should be finite and representable as float.

`IRND` uses integer modulo mapping instead of rounded interpolation. The previous mapping gave endpoint indices roughly half the weight of interior indices; the corrected mapping removes that weighting while retaining modulo bias. This changes index-based choices such as axes, sound/frame variants and effect thresholds.

Simulation and effects still share one seeded stream, so changing the order or number of effect calls can change later simulation draws. Stream separation would require an explicit gameplay decision. Floating-point mappings are checked by their range/endpoint contract; the suite does not claim cross-compiler replay of an entire battle.

The AI robot executable compiles the production definition loader and pricing code using the common engine compiler options. Synthetic configuration prices and armor weapon capacities let it check definition parsing, total resource costs and selection ordering without a map, renderer or game package. Robot mesh assembly and AI construction in a running battle remain playtests.

The bomb executable links `MatrixGameInternal` and constructs a synthetic map, side, robot and idle weapon through their real constructors. Map and robot objects use the same zeroed engine heap and matching destruction as production; their constructors do not initialize every member for ordinary stack allocation. A narrow fixture grants access to weapon state so meshes are unnecessary. A test diagnostic overlay records messages and rejects drawing or device transitions. The cases exercise `TaktPL`, including clearing an inactive or destroyed target, and check that no graphics device or loaded cache entries appear. Debug checks also require all tracked allocations to be released, including visual helpers normally retired by drawing. Actual detonation, effects, selection and UI/manual-control equivalence remain playtests.

The input executable compiles the production keyboard-state code and supplies synthetic action bindings. It tests focus-loss notifications without physical keyboard input or an interactive window. The form forwards deactivation to this state reset; visible focus transitions, mouse capture and complete command behavior remain playtests.

Keep-alive cases call the production window procedure and game form with synthetic map/side state. They check an input-only notification on activation loss, release all 256 tracked keys, clear the last-key marker, preserve application/map flags and accept fresh input. Regain does not synthesize a second reset; ordinary deactivation remains covered. The tests use no window/device and do not establish visible fullscreen or mouse-drag behavior.

The error executable checks catchable engine diagnostics using literal and string messages, an empty Debug trace and a nonexistent synthetic file path. It suppresses native error dialogs so a crash fails its CTest process rather than waiting for desktop input. These cases do not validate a live startup exception, renderer teardown or the standalone message box.

The executable-path cases link the production module-path reader. Synthetic Windows API replies check buffer growth and error handling; the native case resolves the console test image without creating a window. Actual cmd.exe launch spelling, map arguments and startup diagnostics require the separate standalone checks described in the build/playtest guides.

The audio service tests use synthetic WAV bytes and a fake device; frontend cases link the real command/layer implementation with a non-rendering diagnostic substitute. They cover layer interrupt/skip, stale slot reuse and repeated clearing through standalone and synthetic host callbacks without initializing an audio device or opening private archives. A local, muted hardware smoke test explicitly exercises XAudio2 creation, looping, balance and teardown:

```powershell
.\build\mingw-debug-exe\tests\matrixgame_audio_tests.exe manual.audio.native_device
.\build\mingw-release-exe\tests\matrixgame_audio_tests.exe manual.audio.native_device
```

This opt-in check requires a working Windows 10/11 audio endpoint. It emits no sound and is not registered in CTest. Audible game checks follow [Standalone sound](AUDIO.md).

The lifecycle executable links production game cleanup, configuration, map, cache and forms with the same diagnostic overlay substitute. It checks empty and partially populated state without invoking interactive startup. [Session lifetime](LIFECYCLE.md) describes ownership and the boundaries this establishes. Menu restart, result dialogs, actual startup failure handlers and graphics-device teardown remain playtests or require separate regressions.

Recovery tests compile the production decision path and font cache. Synthetic HRESULTs and COM fonts keep them independent of physical graphics hardware and game packages. An explicit native font-reset check is available outside CTest; [DirectX device recovery](DEVICE_RECOVERY.md) describes the procedure, covered transitions and limits. Visible restored graphics and complete renderer resource lifetimes still need playtests.

Construction, rendering, lifecycle, audible/device audio behavior and the gameplay effects of corrected random ranges need additional regressions and playtests. Passing these tests establishes only the contracts listed above.

Selection cases link production multi-selection and game-form input. Synthetic visible buildings count rectangle hit tests without meshes, packages or a device. They verify queued movement and final release through the real form, plus pending refresh, membership, masks, limits and cancellation in the selection implementation. They do not invoke the rendered frame/device path; native queue-load measurements and [Selection dragging](PLAYTESTING.md#selection-dragging) cover that integration separately. Debug rejects tracked allocation leaks and test crashes do not open native dialogs.

## Add a regression

1. Define a bounded behavior and construct the smallest synthetic input that demonstrates it. Keep tests deterministic; use fixed seeds where random behavior matters.
2. Add a test function in the appropriate executable. Use `MG_CHECK(expression)` from `tests/test_support.hpp` for assertions and local objects for automatic cleanup. Assertions throw on failure and are evaluated in every build configuration.
3. Add the function to the executable's `tests::Case` array with a stable name such as `base.storage.round_trip`.
4. Register the same name in `tests/CMakeLists.txt`, for example `add_matrix_test(base.storage.round_trip matrixgame_base_tests base storage)`. Registration adds the required `engine` and `asset-free` labels and the timeout.
5. Run the case in Debug and Release, then run the affected suite. For a defect correction, verify that the regression fails before the fix and passes afterward.

An executable with another test group can reuse `tests::run` and `tests::Case`. Create it with `add_matrix_test_executable`, which links production `MatrixLib`, applies matching compile definitions and adds it to `matrixgame_tests`:

```cmake
add_matrix_test_executable(matrixgame_storage_tests storage_tests.cpp)
add_matrix_test(base.storage.round_trip matrixgame_storage_tests base storage)
```

CMake target names in `add_matrix_test` resolve to the correct executable for each compiler/configuration. Keep game startup and window creation outside automated unit-test entry points; link additional production targets only when the test needs them.

Fixtures belong in the test sources or a clearly named directory under `tests/`. Use synthetic data and explicit byte layouts for format checks. Temporary output belongs in the build directory and should be isolated per case if tests run concurrently. Keep externally supplied game packages and configuration copies untracked.

## Repository checks

```powershell
.\tools\check-repository.ps1
python -B -m unittest discover -s .github/scripts -p 'test_*.py'
```

These checks validate repository hygiene and contribution-policy tooling. For game or build changes, also compile the affected Debug and Release standalone configurations using `tools/build.ps1` and perform relevant playtests.

## Playtesting

Use [Standalone playtesting](PLAYTESTING.md) for packed/text configuration cases, resource failures, the map-argument path and a reproducible validation record.

Launch the standalone EXE without arguments, using locally supplied resources.
Check the affected behavior in Debug and Release with cheats disabled first.

- Start a battle and check map rendering and UI text.
- Select robots, move, attack, and construct robots and turrets.
- Detonate bombs through both the UI and manual control.
- Check sound, pause/resume, focus changes and supported window transitions.
- Finish or exit the battle, then repeat launch and exit several times.
- Test relevant developer commands separately with cheats enabled.

Record the source revision, build configuration, map or seed where available,
steps performed and results. For a failure, include the relevant part of
`test.log` and a debugger stack. Keep resource packages and extracted assets
out of reports.
