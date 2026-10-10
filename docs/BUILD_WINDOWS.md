# Windows builds

Run commands from the repository root in PowerShell. MatrixGame targets Windows
x86 and uses C++20 and DirectX 9.

## MinGW

Install 7-Zip and make `7z.exe` available on PATH. The setup script downloads and verifies a WinLibs bundle containing GCC 13.2.0 and Ninja 1.11.1 under the ignored `.tools/winlibs-13.2.0/` directory. It also prepares the separately pinned CMake/CTest 4.4.4 i386 release under `.tools/cmake-4.4.4-windows-i386/`. The compiler target remains Windows x86; CMake 4.4 requires a Windows 10 or newer build host.

```powershell
.\tools\setup-toolchain.ps1
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release
```

An existing copy of the bundle can be passed to `setup-toolchain.ps1 -ArchivePath <path>`.
Use the supplied x86 compiler rather than an x64 installation on PATH.

For offline setup, also pass `-CMakeArchivePath <cmake-4.4.4-windows-i386.zip>`, or run `setup-cmake.ps1 -ArchivePath <path>` separately. Setup verifies the upstream archive SHA256 before extraction and checks the installed CMake/CTest versions. Downloads are cached under `.tools/downloads/`. Existing compiler installations are retained; the older CMake bundled with WinLibs is not selected by default.

Both build/test helpers accept `-CMakeRoot <installation-directory>` independently of `-ToolchainRoot`. The project's minimum is CMake 3.27, with policies tested through 4.4; use the pinned 4.4.4 for normal work. Relative installation paths are resolved from the PowerShell working directory. CMake's own executable path is propagated to the dependency builds.

The x86 GCC 13.1 bundle could abort while propagating C++ exceptions in relocated executables. The pinned minor update retains ASLR, the UCRT runtime and the MCF thread model. Keep old toolchain installations until existing work no longer needs them. The build helper selects explicit compiler paths and clears compiler caches in the selected build and dependency directories when the installation changes; cached dependency sources, supplied resources and other build configurations are retained.

Executables are written to `build/mingw-debug-exe/MatrixGame` and
`build/mingw-release-exe/MatrixGame`. The helper copies imported MinGW runtime
DLLs beside the executable so it can be launched outside the compiler environment.
The playtest machine also needs the 32-bit DirectX runtime `d3dx9_43.dll`.

`tools/setup-directx-runtime.ps1` prepares the pinned x86 D3DX9 component from [Microsoft's June 2010 runtime package](https://www.microsoft.com/en-us/download/details.aspx?id=8109). It verifies the archive and DLL SHA256 values and extracts the component into `.tools/directx-x86`. `tools/test.ps1` stages this DLL for engine command tests; it does not change the system installation or stage game resources.

Use `-Cheats` to enable developer cheats. The script builds the game without
launching it.

Game builds compile the CTest executables by default. Run them with `tools/test.ps1`; use `-WithoutTests` for a game-only build or `-TestsOnly` to build the test targets without staging game resources. See [Testing](TESTING.md) for commands, results and instructions for adding a regression.

## Game resources and configuration

Place `robots.pkg` and optionally `robots.dat` at the repository root.
The helper copies them into the executable directory:

```text
MatrixGame.exe
CFG/
  robots.dat
  robots/
    data.txt
    iface.txt
DATA/
  robots.pkg
```

`robots.pkg` is required to play. If `DATA/robots.pkg` is absent, the EXE shows "Place robots.pkg at DATA/robots.pkg." and exits with status 1 before game initialization. The path is relative to the executable directory; this dialog also works without `DATA` or `CFG` directories. If `CFG/robots.dat` is absent, the standalone game uses the text configuration. The helper refreshes the tracked configuration after each build, then copies any supplied `robots.dat`.

Standalone sound on Windows 10/11 uses optional local `sound.pkg` and `voices.pkg`, plus the private `sounds.txt` name mapping. Missing audio packages are ignored during startup; with neither present the audio service stays disabled. Prepare them with `python -B tools/prepare-sound-resources.py --game-directory '<original game directory>'`; the helper reads the original `CFG/CacheData.dat` without modifying it. The build stages the packages under `DATA` and the mapping under `CFG`. See [Standalone sound](AUDIO.md) for speech selection, playback contracts and listening checks.

`-WithoutResources` skips copying external resources. It leaves any copies
already present in a reused build directory. Compilation itself does not require
the resource package.

Launch the EXE without arguments for interactive play. A map argument selects the
visibility-calculation path.

The standalone game resolves its resource directory from the loaded executable's module path, so cmd.exe bare-name, relative-path and absolute-path launches use the files beside that EXE. Path resolution, command-line parsing and directory changes run inside the startup diagnostic boundary; failure reports a nonzero exit status. Command-line storage is released on exit and the first map argument retains its existing meaning. This setup applies to the EXE and does not change the DLL host's working directory.

The module-path reader grows its buffer for long names and bounds retries at the Windows path limit. Its synthetic contracts cover Unicode, truncation and API failure; this does not establish support for every long path in the game resource APIs. The logger opens `test.log` at process startup before changing directory, so a launch from another directory leaves that log in the original working directory.

`CFG/standalone.txt` selects the battle random generator independently of packed game configuration. The build helper stages its tracked default beside the EXE; see [Battle random generation](RANDOM.md) for the `ParkMiller` default and corrected `LegacyCRT` option.

Completed standalone execution and teardown return exit status zero. Caught initialization or execution failures return a nonzero status; inspect the diagnostic and `test.log` for the cause.

The standalone EXE uses the Windows GUI subsystem. To wait for completion and obtain its exit status in PowerShell, use `Start-Process -FilePath <executable> -WorkingDirectory <game-directory> -Wait -PassThru` and read the returned process's `ExitCode`; in cmd.exe use `start /wait`. An ordinary shell invocation and its `$LASTEXITCODE` or `%ERRORLEVEL%` do not reliably report a completed game. See [Debugging](DEBUGGING.md#confirm-the-launch-layout) for a complete command.

A present packed configuration must load successfully. A rejected format is reported as a startup error naming the file; text fallback is selected when the packed configuration is absent. When temporarily removing an output copy of `robots.dat`, give its backup a different filename prefix because resource lookup also searches `robots.dat.*`.

## MSVC

Install Visual Studio 2022 with C++ tools and the DirectX SDK (June 2010).
Set `DXSDK_DIR` to the SDK root, then build for Win32:

```powershell
.\tools\setup-toolchain.ps1
$env:DXSDK_DIR = '<DirectX SDK directory>'
.\tools\build.ps1 -Compiler MSVC -Configuration Release
```

Output is under `build/msvc-release-exe/MatrixGame/Release`.
The corresponding Visual C++ x86 runtime is required to run the game.

Debug builds retain `/RTC1` stack and uninitialized-variable checks. `/RTCc` narrowing checks are omitted because they reject valid casts in the Windows CRT and C++ standard library; see [Microsoft's runtime-check documentation](https://learn.microsoft.com/cpp/build/reference/rtc-run-time-error-checks). The `_ALLOW_RTCc_IN_STL` workaround is unnecessary. External zlib and libpng builds install and link the selected Debug or Release configuration so their CRT matches the engine.

## DLL

For integration with the Space Rangers host:

```powershell
.\tools\build.ps1 -Configuration Release -DLL -WithoutResources
```

The DLL uses the host game's resource and configuration layout.

The callback interface keeps its existing x86 calling conventions and structure layout. `Run` contains C++ exceptions from initialization, form construction and result handling. These entry failures return `100`, matching the existing loop-error convention; successful runs retain their existing exit values. Initialization failure leaves the supplied result statistics unchanged, releases partial game state and cursor confinement, and preserves borrowed host resources. Diagnostics go to `test.log` in the host working directory and Windows debug output, without a standalone error dialog. Native faults and invalid host pointers require separate investigation.

CI builds normal standalone Debug/Release configurations for MinGW and MSVC, a MinGW Debug developer EXE with cheats, and Release DLLs with cheats for both compilers. Every build runs the registered engine contracts and contributes to Required checks. MSVC Debug DLL and MSVC Debug EXE with cheats remain outside this matrix. Artifacts identify compiler, configuration, output type and cheats setting; these names replace the older `dll_gcc` and `dll_msvc` names.

`tools/package.ps1` prepares the supported MinGW Release distribution without local assets. [Standalone packaging](RELEASES.md) describes the committed configuration, matching source, notices and separate DirectX prerequisite. CI verifies packaging contracts and retains the package/source ZIPs as artifacts; this is separate from publishing a release.

## Dependencies and cached builds

[ThirdParty/dependencies.json](../ThirdParty/dependencies.json) records zlib 1.3.2 and libpng 1.6.59 source URLs and upstream SHA256 values. CMake verifies release archives in `.tools/downloads/` before extracting them into the selected build directory. A prepared cache permits new dependency builds without network access; provide the exact filenames and bytes from the manifest. There are no floating Git tags, submodule updates or requests during an unchanged incremental build. A damaged cache requires a verified replacement and is not an offline-ready source.

Dependencies build as static libraries with the selected compiler and Debug/Release configuration. MSVC configuration/CRT selection matches the engine; MinGW Debug dependencies now carry Debug information instead of using Release libraries. Installed headers and archives are declared build outputs so Ninja rebuilds consumers after an update. Sources and installed files belong to each normal build directory; supplied game resources and other configurations are retained. Explicit archive paths prevent obsolete libraries left in a reused directory from being selected.

Compressed STRG/ZL03 configuration and ZL02 package blocks retain their tags, widths, block limits and decoded content. Compressed bytes can differ between zlib versions. PNG compatibility checks use synthetic images and the production reader/writer. See [Testing](TESTING.md) for the registered regressions.
