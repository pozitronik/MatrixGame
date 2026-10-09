# Debugging standalone failures

Establish which executable and configuration failed before changing code. Compiler, Debug/Release, EXE/DLL and cheats options alter compiled behavior. Source paths embedded in an exception identify where the binary was compiled; they do not prove where that executable was launched or where it searched for resources.

## Confirm the launch layout

From the repository root, inspect the normal Debug output:

```powershell
$gameDirectory = (Resolve-Path '.\build\mingw-debug-exe\MatrixGame').Path
$executable = Join-Path $gameDirectory 'MatrixGame.exe'
Get-Item -LiteralPath $executable
Get-FileHash -LiteralPath $executable -Algorithm SHA256
Get-ChildItem -LiteralPath $gameDirectory -Filter '*.dll'
Get-Item -LiteralPath (Join-Path $gameDirectory 'DATA\robots.pkg')
Get-Item -LiteralPath (Join-Path $gameDirectory 'CFG\robots\data.txt')
Select-String -LiteralPath '.\build\mingw-debug-exe\CMakeCache.txt' -Pattern '^CMAKE_BUILD_TYPE:|^MATRIXGAME_BUILD_DLL:|^MATRIXGAME_CHEATS:'
```

Use `mingw-release-exe` for Release. MSVC places binaries under the configuration subdirectory described in [Windows builds](BUILD_WINDOWS.md). Record the executable hash and source revision used to build it; the checkout's current branch alone is not a build identity.

When ready for an interactive check, launch the complete output directory and capture the process exit status:

```powershell
$process = Start-Process -FilePath $executable -WorkingDirectory $gameDirectory -PassThru -Wait
"Exit code: $($process.ExitCode)"
```

An expected successful standalone run returns zero. A caught initialization/execution error returns nonzero and displays a diagnostic. Keep the launch command, observed condition, configuration and diagnostic together when reporting a defect; see [Standalone playtesting](PLAYTESTING.md) for battle procedures.

## Find the failure boundary

| Symptom | Check first | Source boundary |
| --- | --- | --- |
| Windows reports a missing MinGW DLL before a game window appears | Rebuild with the supported helper, then check imported runtime DLLs beside that exact EXE; a copied EXE alone is incomplete | [tools/build.ps1](../tools/build.ps1) stages imported and transitive compiler runtimes |
| Windows reports missing `d3dx9_43.dll` | Verify the required x86 DirectX runtime, including when the operating system is x64 | [Windows builds](BUILD_WINDOWS.md), [setup-directx-runtime.ps1](../tools/setup-directx-runtime.ps1) |
| `Error open file: cfg\robots\data.txt` | Confirm working directory and staged `CFG`/`DATA`; packed-config absence selects text loading | [CGame::Init](../MatrixGame/src/MatrixGame.cpp), [CFile::OpenRead](../MatrixLib/Base/CFile.cpp) |
| Invalid packed configuration | Check that the selected `robots.dat` is the planetary STRG format; inspect a loose override/extension backup before replacing supplied data | [CStorage::Load](../MatrixLib/Base/CStorage.cpp), [Data formats](DATA_FORMATS.md) |
| A crash without a caught C++ diagnostic | Preserve binary/PDB and use a native debugger for the faulting instruction and caller stack | Entry-point catch boundaries in [MatrixGame.cpp](../MatrixGame/src/MatrixGame.cpp) and [MatrixGameDll.cpp](../MatrixGame/src/MatrixGameDll.cpp) |
| An asset-free test fails | Rebuild its actual configuration, run its named CTest case and inspect the current report | [Testing](TESTING.md), [tests](../tests) |
| A failure appears only after focus loss, restart or closure | Record the exact transition and prior actions; inspect form/device and object lifetime separately | [MatrixFormGame.cpp](../MatrixGame/src/MatrixFormGame.cpp), [Architecture](ARCHITECTURE.md) |

Relative resource errors can be caused by a run layout rather than the named reader itself. A Windows loader error occurs before `WinMain`, so the game's exception handlers and `test.log` cannot diagnose a missing imported DLL. Inspect imports with the pinned `objdump.exe -p <executable>` when needed; use matching x86 files from the selected toolchain rather than an unrelated runtime on PATH.

## Logs, traces and native debugging

The logger in [MatrixGame.cpp](../MatrixGame/src/MatrixGame.cpp) opens `test.log` before the entry point changes the working directory. Other output may be written after that change. Inspect the launch directory as well as the executable directory instead of assuming every log shares one location. A Debug build also writes `g_ConfigDump.txt` during initialization, and the engine tracer can save history when enabled. These generated files can contain local configuration or resource details; keep them private when preparing a public report.

Engine errors originate in [CException.hpp](../MatrixLib/Base/CException.hpp) and carry file/line information plus any active trace. `ASSERT_OFF` removes engine assertions in Release; Debug tracing and allocation instrumentation add different checks. CTest's `MG_CHECK` remains active in both configurations. A Release string saying a stack is unavailable is not a substitute for inspecting the actual build configuration.

For an access violation, launch the matching EXE under Visual Studio's native debugger or Windows Debugging Tools and break on the first-chance exception. Keep the matching MSVC PDB next to the binary or load it explicitly; for MinGW use an x86-capable debugger that supports its debug information. Record exception code, instruction location and caller stack, then identify the accessed object's owner/lifetime. A caught engine exception and a CPU access violation are different failure paths.

For a Debug heap report, follow the recorded allocation source through its matching release. Engine-allocated objects use typed destruction, configuration references borrow their tree, and forms borrow active game state. Native Debug stack initialization can expose fixture/object assumptions hidden by zeroed storage; compare allocation contracts before weakening a runtime check.

## Reduce and verify

Reproduce from the supported helper-built output, then reduce to the smallest relevant synthetic fixture or exact live transition. Keep supplied resources unchanged, use separate synthetic bytes for corrupt-input checks, and restore any temporary output-copy change after a playtest.

For a code correction, demonstrate the regression before the change, exercise the production implementation, and verify affected Debug and Release configurations. Use CI for missing compiler coverage and report the specific check that remains unavailable. Compilation, a clean startup or a successful synthetic order test establishes only that boundary; visible movement, effects, rendering and audible behavior need the matching live observation.
