# Debugging standalone failures

Establish which executable and configuration failed before changing code. Compiler, Debug/Release, EXE/DLL and cheats options alter compiled behavior. Supported MinGW builds map compile-time source paths to repository-relative names, such as `MatrixLib/Base/CFile.cpp`, in errors, assertions and logger locations. These paths identify the source at the recorded build revision; they are not resource lookup paths. MSVC and older binaries can still report absolute compile-time paths, which do not prove where the executable was launched.

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

The game EXE uses the Windows GUI subsystem. An ordinary PowerShell `&` or cmd.exe invocation can return before the game exits; `$LASTEXITCODE` or `%ERRORLEVEL%` then describes the shell's launch rather than the completed game. Use `Start-Process -Wait -PassThru` as above, or `start /wait` in cmd.exe, when recording an exit status.

## Find the failure boundary

| Symptom | Check first | Source boundary |
| --- | --- | --- |
| Windows reports a missing MinGW Debug DLL before a game window appears | Rebuild with the supported helper, then check imported runtime DLLs beside that exact Debug EXE; Release uses static compiler runtimes | [tools/build.ps1](../tools/build.ps1) stages imported and transitive compiler runtimes |
| Windows reports missing `d3dx9_43.dll` | Install the x86 DirectX June 2010 runtime; the setup helper provisions this DLL only beside engine-test executables and does not install or stage it for the game | [Windows builds](BUILD_WINDOWS.md), [setup-directx-runtime.ps1](../tools/setup-directx-runtime.ps1), [test.ps1](../tools/test.ps1) |
| `Error open file: cfg\robots\data.txt` | Check staged `CFG`/`DATA` beside the exact EXE; standalone startup changes there before loading, and packed-config absence selects text | [CGame::Init](../MatrixGame/src/MatrixGame.cpp), [CFile::OpenRead](../MatrixLib/Base/CFile.cpp) |
| `Error open file: cfg\robots.dat` | Check for a suffix-only backup such as `robots.dat.backup`: existence lookup can find it while loading still opens the original filename | [CFile::FileExist](../MatrixLib/Base/CFile.cpp), [Data formats](DATA_FORMATS.md) |
| Invalid packed configuration | Check that the selected `robots.dat` is the planetary STRG format; preserve the original and isolate the rejected format with synthetic data | [CStorage::Load](../MatrixLib/Base/CStorage.cpp), [Data formats](DATA_FORMATS.md) |
| An unhandled native exception | Collect the Debug tracer's `Unhandled Exception` diagnostic and `test.log`, then debug the faulting instruction and caller stack | [Tracer.cpp](../MatrixLib/Base/Tracer.cpp); game-loop catch boundary in [MatrixGame.cpp](../MatrixGame/src/MatrixGame.cpp), host entry in [MatrixGameDll.cpp](../MatrixGame/src/MatrixGameDll.cpp) |
| An asset-free test fails | Rebuild its actual configuration, run its named CTest case and inspect the current report | [Testing](TESTING.md), [tests](../tests) |
| A battle runs without sound | Check the private `CFG/sounds.txt` mapping, staged effects/speech packages and the active Windows audio endpoint; inspect `test.log` for bounded sound initialization/resource diagnostics | [Standalone sound](AUDIO.md), [SoundBridge.cpp](../MatrixGame/src/SoundBridge.cpp) |
| A failure appears only after focus loss, restart or closure | Record the exact transition and prior actions; inspect form/device and object lifetime separately | [MatrixFormGame.cpp](../MatrixGame/src/MatrixFormGame.cpp), [Architecture](ARCHITECTURE.md) |

Relative resource errors can be caused by a run layout rather than the named reader itself. A Windows loader error occurs before `WinMain`, so the game's exception handlers and `test.log` cannot diagnose a missing imported DLL. Inspect imports with the pinned `objdump.exe -p <executable>` when needed; use matching x86 files from the selected toolchain rather than an unrelated runtime on PATH.

## Logs, traces and native debugging

The logger in [MatrixGame.cpp](../MatrixGame/src/MatrixGame.cpp) opens `test.log` before the entry point changes the working directory. Other output may be written after that change. Inspect the launch directory as well as the executable directory instead of assuming every log shares one location. A Debug build also writes `g_ConfigDump.txt` during initialization, and the engine tracer can save history when enabled. These generated files can contain local configuration or resource details; keep them private when preparing a public report.

Engine errors originate in [CException.hpp](../MatrixLib/Base/CException.hpp) and carry file/line information plus any active trace. `ASSERT_OFF` removes engine assertions in Release; Debug tracing and allocation instrumentation add different checks. The suite's [MG_CHECK](../tests/test_support.hpp) remains active in both configurations. A Release string saying a stack is unavailable is not a substitute for inspecting the actual build configuration.

Debug and `_TRACE` base initialization install `sys_except_handler` through `CDebugTracer::StaticInit`. Unhandled native failures log their exception/trace to `test.log` and show an `Unhandled Exception` box. Collect those diagnostics first when no debugger is attached. MSVC Debug console tests install their own failure-reporting filter, separate from the game's diagnostics. The DLL entry file has no C++ catch block: its game-loop catch boundary is `CGame::RunGameLoop`, after initialization.

For an access violation, launch the matching Debug EXE under Visual Studio's native debugger or Windows Debugging Tools and break on the first-chance exception. Keep its matching MSVC Debug PDB next to the binary or load it explicitly; for MinGW Debug use an x86-capable debugger that supports its debug information. MSVC Release compiles with `/Zi` but does not link with `/DEBUG`, and MinGW Release is stripped with `-s`, so those supported Release outputs do not provide equivalent executable symbols. Record exception code, instruction location and caller stack, then identify the accessed object's owner/lifetime. A caught engine exception and a CPU access violation are different failure paths.

For a Debug heap report, follow the recorded allocation source through its matching release. Engine-allocated objects use typed destruction, configuration references borrow their tree, and forms borrow active game state. MinGW uses `-ftrivial-auto-var-init=zero` in every configuration, which can hide uninitialized stack assumptions. MSVC Debug's `/RTC1` uses a nonzero stack pattern and can expose them; compare allocation contracts before weakening a runtime check.

## Reduce and verify

Reproduce from the supported helper-built output, then reduce to the smallest relevant synthetic fixture or exact live transition. Keep supplied resources unchanged, use separate synthetic bytes for corrupt-input checks, and restore any temporary output-copy change after a playtest.

For a code correction, demonstrate the regression before the change, exercise the production implementation, and verify affected Debug and Release configurations. Use CI for missing compiler coverage and report the specific check that remains unavailable. Compilation, a clean startup or a successful synthetic order test establishes only that boundary; visible movement, effects, rendering and audible behavior need the matching live observation.
