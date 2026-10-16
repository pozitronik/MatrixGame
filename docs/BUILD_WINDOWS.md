# Windows builds

Run commands from the repository root in PowerShell. MatrixGame targets Windows
x86 and uses C++20 and DirectX 9.

## Guided build

`build.bat` in the repository root starts the interactive build wizard `tools/build-wizard.ps1` with Windows PowerShell. It can be run from Explorer, cmd.exe or PowerShell; when started from Explorer, it keeps its window open at the end. The wizard asks for the configuration, compiler, EXE or DLL output, cheats, music decoder, tests and number of parallel jobs. It then runs `tools/build.ps1` with those options.

For each tool, the wizard shows what it found and offers three choices: use it, enter another installation path, or prepare the pinned version from a verified download or a supplied archive. The pinned versions are prepared with the setup scripts described below: MinGW with `setup-toolchain.ps1`, CMake with `setup-cmake.ps1` and the DirectX SDK subset with `setup-directx-sdk.ps1`. CMake is resolved before the compiler, and the compiler is then prepared with `-SkipCMake`, so a supplied WinLibs archive never triggers a CMake download. It validates a supplied installation before using it:
- The compiler must target `i686-w64-mingw32` and provide Ninja and objdump.
- CMake must be version 3.27 or newer and include CTest.
- The DirectX SDK must contain the D3DX9 headers and x86 libraries.

Versions other than the pinned ones are accepted with a warning.

7-Zip is needed only to extract downloaded archives. When 7-Zip is not found, the wizard can install it with winget, after confirmation. MSVC builds need Visual Studio 2022 with the x86/x64 C++ tools; the wizard can install the Visual Studio 2022 Build Tools with winget, after confirmation. Newer Visual Studio versions are reported but not used, because the build helper selects the Visual Studio 2022 generator.

For a standalone build, the wizard can import resources from an owned Space Rangers 2 installation. It looks for the installation in Steam and GOG libraries, or accepts the path of the directory that contains `DATA/robots.pkg`. It copies the following files to the ignored repository-root names that the build helper stages:
- `robots.pkg`;
- the packed `robots.dat`, which is optional;
- `sound.pkg`;
- the speech archive of the chosen language, as `voices.pkg`;
- `music.pkg`;
- the chosen language's `Lang.dat`.

Files that already match are left alone, and a different existing file is replaced only after confirmation. The installation itself is never modified. When the x86 `d3dx9_43.dll` is not installed, the wizard can prepare the pinned runtime with `setup-directx-runtime.ps1` and copy it beside the built executable. That script runs before every copy, so a previously prepared DLL that does not match the pinned SHA256 is rejected rather than copied.

Answers are saved in the ignored `.local/build-wizard.env`, and later runs offer to build with them directly. The wizard accepts these options:

| Option | Effect |
| --- | --- |
| `-Reconfigure` | Ask every question again, with the saved answers as defaults |
| `-NonInteractive` | Use saved, discovered or default answers without asking. Download missing pinned tools, but never install system software. Import only missing resources, and only from a saved installation |
| `-Check` | Report the environment without downloading, installing, copying or saving anything. Exit with status 1 when a required component is missing |
| `-Reset` | Remove the saved answers |
| `-Configuration`, `-Compiler` | Preselect these answers |

For scripted builds, tests and options the wizard does not ask about, use `tools/build.ps1` directly.

## MinGW

Install 7-Zip and make `7z.exe` available on PATH. The setup script downloads and verifies a WinLibs bundle containing GCC 13.2.0 and Ninja 1.11.1 under the ignored `.tools/winlibs-13.2.0/` directory. It also prepares the separately pinned CMake/CTest 4.4.4 i386 release under `.tools/cmake-4.4.4-windows-i386/`. The compiler target remains Windows x86; CMake 4.4 requires a Windows 10 or newer build host.

```powershell
.\tools\setup-toolchain.ps1
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release
```

An existing copy of the bundle can be passed to `setup-toolchain.ps1 -ArchivePath <path>`.
Use the supplied x86 compiler rather than an x64 installation on PATH.

For offline setup, also pass `-CMakeArchivePath <cmake-4.4.4-windows-i386.zip>`, or run `setup-cmake.ps1 -ArchivePath <path>` separately. `-SkipCMake` prepares only the compiler, for a CMake installation that is prepared or chosen separately. Setup verifies the upstream archive SHA256 before extraction and checks the installed CMake/CTest versions. Downloads are cached under `.tools/downloads/`. A download is written to a separate partial file and kept only after its SHA256 matches the pin. A failed, interrupted or mismatching attempt is discarded; setup makes at most three attempts, waiting 5 and then 10 seconds, and then reports the last error. A cached file with a different SHA256 is discarded and downloaded again. An archive supplied with `-ArchivePath` is verified but never modified or deleted. Existing compiler installations are retained; the older CMake bundled with WinLibs is not selected by default.

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

`-WithoutMusic` builds the standalone game without the battle music decoder by configuring `MATRIXGAME_MUSIC=OFF`. libogg and libvorbis are then neither downloaded nor built, and the game runs normally without music, writing `Battle music disabled: this build has no music decoder` to `test.log`. DLL builds always use `MATRIXGAME_MUSIC=OFF`, because the host owns music. The option defaults to `ON` for a standalone EXE configured directly with CMake and `OFF` for a DLL. The test helper accepts the same switch. Release packages require the decoder.

`-Coverage` builds an instrumented MinGW copy in a separate directory ending in `-coverage`, for example `build/mingw-debug-exe-coverage`. See [Coverage and performance measurements](MEASUREMENTS.md).

`-Trace` enables the legacy `_TRACE` diagnostic configuration through `MATRIXGAME_TRACE=ON`. Both build and test helpers use a separate directory ending in `-trace`, for example `build/mingw-debug-exe-trace`; combining coverage adds `-coverage-trace`. Use the same switch for building and testing. The option applies consistently to libraries, game and tests because trace parameters change some C++ signatures. It is off by default and is not a release-package option. Trace builds also enable Shift+Escape result shortcuts; see [trace result tests](testing/85-trace-results.md).

The legacy Debug trace assertions throw exceptions. In a `noexcept` destructor a failed assertion still terminates the process; enabling tracing does not make that failure recoverable. MinGW retains the associated `-Wterminate` warning without promoting it to an error in this optional configuration.

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

An optional root-level `Lang.dat`, the original game's map language catalog, is staged as `CFG/Lang.dat`; see [Map selection](MAPS.md).

`robots.pkg` is required to play. If `DATA/robots.pkg` is absent, the EXE shows "Place robots.pkg at DATA/robots.pkg." and exits with status 1 before game initialization. The path is relative to the executable directory; this dialog also works without `DATA` or `CFG` directories. If `CFG/robots.dat` is absent, the standalone game uses the text configuration. The helper refreshes the tracked configuration after each build, then copies any supplied `robots.dat`.

Standalone sound on Windows 10/11 uses optional local `sound.pkg`, `voices.pkg` and `music.pkg` with built-in sound-name mappings. Copy the desired archives from an owned installation to the repository root; the build stages them under `DATA`. No mapping preparation is required for standard sound effects. An existing root-level `sounds.txt` is staged under `CFG` as an optional override. Missing audio packages are ignored during startup; with none present the audio service stays disabled. See [Standalone sound](AUDIO.md) for speech selection, custom mappings and listening checks.

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

Instead of installing the SDK, `tools/setup-directx-sdk.ps1` can prepare the files the build uses. It downloads the pinned `DXSDK_Jun10.exe` (about 570 MB), verifies its SHA256 and extracts only `DXSDK/Include` and `DXSDK/Lib/x86` into `.tools/directx-sdk/DXSDK`. The extracted files are checked against a pinned digest, the downloaded installer is removed, and later runs reuse the verified files. `-ArchivePath` uses an existing copy of the installer, which is never modified or deleted; a copy inside `.tools/directx-sdk` or its `.tools/directx-sdk.partial` staging directory is refused before anything is removed. Point `DXSDK_DIR` at `.tools/directx-sdk/DXSDK` to use it.

Output is under `build/msvc-release-exe/MatrixGame/Release`.
The corresponding Visual C++ x86 runtime is required to run the game.

Debug builds retain `/RTC1` stack and uninitialized-variable checks. `/RTCc` narrowing checks are omitted because they reject valid casts in the Windows CRT and C++ standard library; see [Microsoft's runtime-check documentation](https://learn.microsoft.com/cpp/build/reference/rtc-run-time-error-checks). The `_ALLOW_RTCc_IN_STL` workaround is unnecessary. External zlib, libpng, libogg and libvorbis builds install and link the selected Debug or Release configuration so their CRT matches the engine.

## DLL

For integration with the Space Rangers host:

```powershell
.\tools\build.ps1 -Configuration Release -DLL -WithoutResources
```

The DLL uses the host game's resource and configuration layout. [Original host integration](ORIGINAL_HOST.md) describes the host contract, its automated evidence and the native smoke procedure.

The callback interface keeps its existing x86 calling conventions and structure layout. `Run` contains C++ exceptions from initialization, form construction and result handling. These entry failures return `100`, matching the existing loop-error convention; successful runs retain their existing exit values. Initialization failure leaves the supplied result statistics unchanged, releases partial game state and cursor confinement, and preserves borrowed host resources. Diagnostics go to `matrixgame-dll-errors.log` in the host working directory and Windows debug output, without a standalone error dialog. The dedicated file avoids write-sharing conflicts with the normal logger's `test.log`. Native faults and invalid host pointers require separate investigation.

CI builds normal standalone Debug/Release configurations for MinGW and MSVC, a MinGW Debug developer EXE with cheats, and Release DLLs with cheats for both compilers. Every build runs the registered engine contracts and contributes to Required checks. MSVC Debug DLL and MSVC Debug EXE with cheats remain outside this matrix. Artifacts identify compiler, configuration, output type and cheats setting; these names replace the older `dll_gcc` and `dll_msvc` names. Jobs restore `.tools/downloads` and the extracted DirectX SDK subset from caches keyed by the dependency manifests and setup scripts, so changing a pinned URL or checksum selects a new cache. Restored files are verified again before use. On a cache miss, a job downloads and verifies the archives and saves the cache before building; a download that still fails after its retries fails the job and saves nothing.

`tools/package.ps1` prepares the supported MinGW Release distribution without local assets. [Standalone packaging](RELEASES.md) describes the committed configuration, matching source, notices and separate DirectX prerequisite. CI verifies packaging contracts and retains the package/source ZIPs as artifacts; this is separate from publishing a release.

## Dependencies and cached builds

[ThirdParty/dependencies.json](../ThirdParty/dependencies.json) records zlib 1.3.2, libpng 1.6.59, libogg 1.3.6 and libvorbis 1.3.7 source URLs and upstream SHA256 values. libogg and libvorbis decode the optional battle music and are skipped when `MATRIXGAME_MUSIC` is `OFF`; libvorbis 1.3.7 is configured with `CMAKE_POLICY_VERSION_MINIMUM=3.5` because its build declares compatibility with CMake 2.8.12, which the pinned CMake no longer accepts. CMake verifies release archives in `.tools/downloads/` before extracting them into the selected build directory. A prepared cache permits new dependency builds without network access; provide the exact filenames and bytes from the manifest. `tools/fetch-dependencies.ps1` prepares it with the same verification and retry rules as setup, together with the thread-runtime source archive used by packaging. There are no floating Git tags, submodule updates or requests during an unchanged incremental build. A damaged cache requires a verified replacement and is not an offline-ready source.

Dependencies build as static libraries with the selected compiler and Debug/Release configuration. MSVC configuration/CRT selection matches the engine; MinGW Debug dependencies now carry Debug information instead of using Release libraries. Installed headers and archives are declared build outputs so Ninja rebuilds consumers after an update. Sources and installed files belong to each normal build directory; supplied game resources and other configurations are retained. Explicit archive paths prevent obsolete libraries left in a reused directory from being selected.

Compressed STRG/ZL03 configuration and ZL02 package blocks retain their tags, widths, block limits and decoded content. Compressed bytes can differ between zlib versions. PNG compatibility checks use synthetic images and the production reader/writer. See [Testing](TESTING.md) for the registered regressions.
