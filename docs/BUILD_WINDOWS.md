# Windows builds

Run commands from the repository root in PowerShell. MatrixGame targets Windows
x86 and uses C++20 and DirectX 9.

## MinGW

Install 7-Zip and make `7z.exe` available on PATH. The setup script downloads
and verifies a WinLibs bundle containing GCC 13.1.0, CMake 3.26.3 and Ninja 1.11.1.
It installs these tools under the ignored `.tools/` directory.

```powershell
.\tools\setup-toolchain.ps1
.\tools\build.ps1 -Configuration Debug
.\tools\build.ps1 -Configuration Release
```

An existing copy of the bundle can be passed to `setup-toolchain.ps1 -ArchivePath <path>`.
Use the supplied x86 compiler rather than an x64 installation on PATH.

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

`robots.pkg` is required to play. If `CFG/robots.dat` is absent, the
standalone game uses the text configuration. The helper refreshes the tracked
configuration after each build, then copies any supplied `robots.dat`.

`-WithoutResources` skips copying external resources. It leaves any copies
already present in a reused build directory. Compilation itself does not require
the resource package.

Launch the EXE without arguments for interactive play. A map argument selects the
visibility-calculation path.

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

## DLL

For integration with the Space Rangers host:

```powershell
.\tools\build.ps1 -Configuration Release -DLL -WithoutResources
```

The DLL uses the host game's resource and configuration layout.

CI builds the normal standalone configurations, a developer EXE with cheats, and Release DLLs with cheats for both MinGW and MSVC. Artifacts identify compiler, configuration, output type and cheats setting; these names replace the older `dll_gcc` and `dll_msvc` names.
