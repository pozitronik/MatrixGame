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
| Error diagnostics | Omitted secondary messages, errors without an active Debug trace, file/line metadata and missing-file operation/path diagnostics |
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

The error executable checks catchable engine diagnostics using literal and string messages, an empty Debug trace and a nonexistent synthetic file path. It suppresses native error dialogs so a crash fails its CTest process rather than waiting for desktop input. These cases do not validate a live startup exception, renderer teardown or the standalone message box.

Construction, rendering, audio, lifecycle and the gameplay effects of corrected random ranges need additional regressions and playtests. Passing these tests establishes only the contracts listed above.

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
