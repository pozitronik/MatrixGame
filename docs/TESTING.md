# Testing

Automated engine tests use CTest and small C++ console executables. They run with synthetic data, without game resources or an interactive window. Use the Windows x86 tools from the [build guide](BUILD_WINDOWS.md); building the base library still needs the normal compiler prerequisites.

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

Both helpers accept `-Compiler`, `-Configuration`, `-DLL`, `-Cheats`, `-ToolchainRoot` and `-Jobs`. Use the same options for building and testing. For MSVC, set up Visual Studio and the DirectX SDK as described in the build guide:

```powershell
.\tools\test.ps1 -Compiler MSVC -Configuration Release
```

Normal game builds also compile the test executables. `tools/build.ps1 -WithoutTests` sets `BUILD_TESTING=OFF` for a game-only build; the test helper enables it again on its next build.

## Results and direct CTest commands

Each named case runs in its own process with a 30-second timeout. CTest reports the failed case, assertion expression, source location and captured output. The helper writes `test-results.xml` in the selected build directory, replacing the report with the current selection's results; detailed output is in `Testing/Temporary/LastTest.log` below that directory.

To list or run already-built tests directly:

```powershell
$ctest = '.\.tools\winlibs\mingw32\bin\ctest.exe'
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

The checksum fixtures preserve the engine's existing data compatibility contract. Tests link the production `MatrixLib` target and use its real implementations. The console executables use static MinGW runtimes so they can run outside the compiler environment.

This is a small starting suite. Configuration/storage round trips, random-number behavior, construction, rendering, audio and lifecycle need additional regressions and playtests. Passing these tests establishes only the contracts listed above.

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
