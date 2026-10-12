# x64 ABI, data layout and dependency obstacles (issue #102)

## Summary

An x64 standalone build is feasible without changing any resource format or the x86 host contract, but it is not a recompile. At 540251d an MSVC x64 build fails to compile on `GWL_WNDPROC` and on x86-layout test assertions, and fails to link on an unconditional `/MACHINE:X86`; once those are bypassed the asset-free suite fails at exactly the two places where native width leaks into data: the package directory record and the PNG reader handle. Source reading identifies several further runtime pointer truncations that the suite does not reach (vector-object draw parameters, effect user data, selection callbacks, interface pointer identity, effect-path rebasing). All persisted formats other than the package record are already fixed-width on Windows LLP64 and measure identically on x86 and x64.

Measured (exploratory compilation, not validated x64 gameplay): with a nine-file exploratory patch, the MSVC x64 Debug and Release builds link and pass 172/172 registered engine contracts. Release additionally required stopping C++ exceptions from crossing libpng's C frames, which MSVC x64 `/EHsc` code generation does not catch. The pinned x86 MinGW Debug and Release suites pass 172/172 at the same revision.

Recommendation: keep the x86 host DLL and every wire layout frozen, fix the width defects as small #103 leaves behind #86 layout fixtures, then add MinGW x64 (packaged) and MSVC x64 (compatibility) standalone jobs in #104. Compare architectures by exact equality for formats, integer state and RNG streams, and by first-divergence diagnosis for floating-point simulation; do not promise bit-identical battles.

## Identities

| Item | Value |
| --- | --- |
| Revision | `540251d4662c590bad4684df01e0c521e6c4aee7` (origin/dev) |
| x86 baseline | WinLibs i686 GCC 13.2.0 (`i686-ucrt-mcf-dwarf`, r3), CMake/CTest 4.4.4 i386, Ninja 1.11.1, via `tools/test.ps1 -Configuration Debug\|Release` |
| MSVC probes | cl 19.51.36252 for x86 and x64 (toolset 14.51.36231, Visual Studio 18 Community), Windows SDK 10.0.26100.0, Ninja generator, CMake 4.4.4 i386 |
| Probe configuration | `MATRIXGAME_BUILD_DLL=OFF`, `MATRIXGAME_CHEATS=OFF`, `BUILD_TESTING=ON`, Debug and Release; project `/W3` options plus `CMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /GR /EHsc /w14311 /w14312 /w14302 /w14267 /w14244 /w14334 /w14305 /w14826 /w14477 /w14313` |
| DirectX for MSVC probes | No DirectX SDK is installed. Probes used the mingw-w64 D3DX9 headers with a `WINBOOL` shim, a stub `dxerr.h`/`DxErr.lib`, the Windows SDK `d3d9.lib`, and an x64 `d3dx9.lib` import library generated from the export table of the installed system `d3dx9_43.dll`. This substitutes for, and is not equivalent to, the June 2010 SDK |
| Layout probe | Real project headers (`Pack.hpp`, `CStorage.hpp`, `MatrixGameDll.hpp`, `Common.hpp`, `VectorObject.hpp`) compiled with i686 GCC, MSVC x86 and MSVC x64 |

The x64 runtime results come from an exported copy of the source with exploratory edits (machine flag, `GWLP_WNDPROC`, x86-only layout assertions gated, PNG handle width, package side table, `/EHc-` for `FilePNG.cpp`). No tracked file in the checkout was modified. Nothing was launched except console probe and test executables; no game window, map or package was loaded.

## Fixed contracts and changeable representations

| Must stay fixed | Source | Measured x86 layout |
| --- | --- | --- |
| Package root offset, `SFolderRec`, `SFileRec` (1-byte packed, 63-byte names, 32-bit enum, offset and extra fields) and the stored-record-size check | `MatrixLib/Base/Pack.hpp:37-58`, `MatrixLib/Base/Pack.cpp:206-217` | `SFileRec` 158 bytes, `m_Offset` at 150, `m_Extra` at 154; `SFolderRec` 12 |
| ZL02 package blocks, STRG header/version, record/column metadata, ZL03 framing, UTF-16 `ST_WCHAR` | `MatrixLib/Base/CStorage.cpp:53-75,99-125,159-183,260-305`, `MatrixLib/Base/CStorage.hpp:20-32` | Explicit `uint32_t` writes; element sizes 1/2/4/8 |
| Map payload records read as raw arrays | `MatrixGame/src/Common.hpp:203-232`, `MatrixGame/src/MatrixMapGroup.cpp:230-281`, `MatrixGame/src/MatrixMapPrepare.cpp:366-376,1188-1194`, road network `Get<int/uint8_t/bool/CPoint/CRect/DWORD>` in `MatrixGame/src/Logic/MatrixRoadNetwork.cpp` | `SCompilePoint` 12, `SCompileMoveCell` 64, `SCompileBottomVert` 8, `CRect` 16; identical on x64 |
| Vector-object file records | `MatrixLib/3G/VectorObject.hpp:55-140` (`SVOHeader` .. `SVOEdge`) | `SVOHeader` 80, `SVOMaterial` 136, `SVOFrame` 64; identical on x64 |
| PNG, WAV and text configuration byte formats | `MatrixLib/Bitmap/FilePNG.cpp`, `MatrixGame/src/Audio/Audio.cpp`, `MatrixLib/Base/CBlockPar.cpp` | Architecture-independent byte parsers |
| Original host ABI: `GetRobotInterface` (`extern "C" __cdecl`), `__stdcall` callback tables, `SRobotsSettings`, `SRobotGameState`, `SMGDRangersInterfaceText`, `dword` sound ids, `wchar*` strings, result codes 0-4 and +100 | `MatrixGame/src/MatrixGameDll.hpp:19-155`, `MatrixGame/src/MatrixGameDll.cpp` | `SMGDRobotInterface` 16 (`m_Run` at 12), `SMGDRangersInterface` 64, `SMGDRangersInterfaceText` 20, `SRobotsSettings` 56, `SRobotGameState` 24 |

On x64 the same declarations measure 32, 128 and 32 bytes for the three callback/text structures, while `SRobotsSettings` stays 56 bytes because its pointer-carrying `long FDirect3D`/`FD3DDevice` fields (`MatrixGameDll.hpp:20-21`) remain 32-bit and could not hold x64 pointers. The host ABI is therefore an x86-only contract: there is no meaningful x64 DLL, and the x64 build should refuse `MATRIXGAME_BUILD_DLL=ON`.

Free to change: in-memory runtime structures (`SFileHandleRec`, `CHsFolder` folder links, effect and selection callback parameters, interface element parameters, helper group keys, handle wrappers), container and size types, standalone settings representation (currently reusing the host `SRobotsSettings`, `MatrixGame/src/MatrixGame.cpp:328`), and internal callback signatures. Changing them must not alter RNG consumption or simulation order.

Windows LLP64 keeps `DWORD`, `dword` (`unsigned long`, `MatrixLib/Base/BaseDef.hpp:20,95`), `long`, `int` and `wchar_t` widths unchanged, which is why the native-scalar `CBuf` writes remain compatible on x64. That assumption fails on LP64 platforms and should be recorded as Windows-specific, not generalized.

## Obstacle matrix

Confidence: High = measured failure or unambiguous source path; Medium = source inference with a plausible benign case; Low = diagnostic-only or compiled-out code. Dependency column refers to external components needed by the fix.

| ID | Area | Source | Problem | Required fix | Confidence | Test needed | Dependency |
| --- | --- | --- | --- | --- | --- | --- | --- |
| O1 | MSVC link flags | `CMakeLists.txt:39-43`, `MatrixGame/CMakeLists.txt:47`, `MatrixLib/CMakeLists.txt:124` | `/MACHINE:X86` is unconditional; `-SAFESEH:NO` is x86-only; `/DYNAMICBASE:NO` places the x64 image at `0x140000000` (measured) | Select machine and SAFESEH by `CMAKE_SIZEOF_VOID_P`; decide x64 ASLR policy (D2) | High | Configure/link both architectures in CI; PE machine check | None |
| O2 | Build, test, setup and packaging helpers | `tools/build.ps1:18,31-32,49-51,83`, `tools/test.ps1:17,52-56`, `tools/setup-toolchain.ps1:12-15`, `tools/setup-directx-runtime.ps1:7-15,37-44`, `cmake/FindDIRECTX9.cmake:23`, `.github/workflows/win-build.yml:107-118,156`, `tools/package.ps1:10,28`, `tools/package_game.py:67-72,154,190`, `.github/workflows/release.yml:17` | i686 compiler check, `-A Win32`, architecture-less build directories, x86-only D3DX staging and SDK library path, x86-only PE validation and archive names | Explicit architecture parameter defaulting to x86, distinct outputs, x64 D3DX cabinet extraction, `Lib/x64`, per-architecture PE/import checks. `tools/release.py:54-99` already accepts an optional x64 archive | High | Helper unit tests (`tests/test_*.py`), clean CI setup, mixed-cache rejection | WinLibs x86_64, DXSDK `Lib/x64`, redist x64 cabinet |
| O3 | Window procedure subclassing | `MatrixLib/3G/3g.cpp:46,213,275-278,333`; `tests/lifecycle_tests.cpp:362,369` | `GWL_WNDPROC` is undefined on x64 (C2065, measured); `DWORD g_WndOldProg` with `GetWindowLong` would truncate the host procedure | `GWLP_WNDPROC`, `LONG_PTR`, `Get/SetWindowLongPtr` throughout | High | Existing lifecycle host-window restore test on both architectures | None |
| O4 | Package directory record | `MatrixLib/Base/Pack.hpp:38-50`; `MatrixLib/Base/Pack.cpp:210,220-253,273,378,394,673,756,1442,1475` | `uintptr_t m_Offset/m_Extra` make `SFileRec` 166 bytes on x64 (measured), so every package folder is rejected (`base.dependencies.legacy_pack_blocks` fails, measured). `m_Extra` also stores a runtime `CHsFolder*` inside the disk record | Fixed `uint32_t` disk fields and an x86-independent `static_assert(sizeof == 158)`; keep subfolder pointers in a per-folder side table. Prototype passes on x64 (measured) | High | Existing fixture with the assertion enabled everywhere; add nested-folder, truncated-folder and wrong-record-size fixtures | None |
| O5 | PNG reader handle | `MatrixLib/Bitmap/CBitmap.cpp:887,906`; `MatrixLib/Bitmap/FilePNG.hpp:14-15` | Pointer returned as `uintptr_t` is stored in `uint32_t id`; x64 access violation in `FilePNG::Read` (four PNG tests, measured) | Use `uintptr_t` or an owning opaque type end to end | High | Existing PNG tests under high-entropy ASLR | None |
| O6 | Exceptions through libpng | `MatrixLib/Bitmap/FilePNG.cpp:31-34,43-50,81-127`; `/EHsc` in `CMakeLists.txt:37` and CMake defaults | Callbacks throw C++ exceptions through C frames. MSVC x64 Release does not catch them in `ReadStart_Buf` (`bitmap.png.invalid` fails); `/EHc-` on this file fixes it (measured). Relies on undefined cross-language unwinding on every compiler | Contain errors at the libpng boundary (setjmp/longjmp-safe C-compatible shim or status propagation); `/EHc-` is a fallback only (D4) | High (MSVC x64), Medium (MinGW x64, unmeasured) | `bitmap.png.invalid` plus truncated-IDAT/CRC cases in Release on all four configurations | libpng 1.6.59 unchanged |
| O7 | Vector-object draw parameter | `MatrixLib/3G/VectorObject.hpp:43-47,430,651,742`; `MatrixGame/src/MatrixObjectRobot.cpp:984-1019`; `MatrixGame/src/MatrixObjectBuilding.cpp:943,1394`; `MatrixGame/src/MatrixObjectCannon.cpp:536,550`; `MatrixGame/src/MatrixFlyer.cpp:1760,1764`; `MatrixGame/src/MatrixObject.cpp:841`; `MatrixGame/src/MatrixSkinManager.cpp:251-262` | Texture/object pointers pass through `Draw(DWORD user_param)` into `uintptr_t` skin callbacks, which call `SetTexture((LPDIRECT3DTEXTURE9)user_param)`; truncated on every side-coloured draw | Widen the draw chain to `uintptr_t` or a typed pointer; do not change draw order | High (source) | Fake-device draw contract capturing the texture argument; native rendering playtest (#105) | None |
| O8 | Effect user data | `MatrixGame/src/Effects/MatrixEffect.hpp:128,458-483`; `MatrixEffectFirePlasma.hpp:14,34`; `MatrixEffectFlame.hpp:71,81`; `MatrixEffectMovingObject.hpp:88-89`; `MatrixEffectBigBoom.hpp:35,51`; `MatrixEffect.cpp:573,648,805,822` | Factories and `FIRE_END_HANDLER` use `uintptr_t user`, but effect constructors/members store `DWORD`; hit handlers receive truncated owner pointers | Store `uintptr_t` (or a typed owner) consistently | High (source) | Effect handler round-trip with a value above 4 GiB; weapon-hit scenario (#82) | None |
| O9 | Selection enumeration parameter | `MatrixGame/src/MatrixMultiSelection.hpp:17,135-136`; `MatrixGame/src/MatrixFormGame.cpp:358,583,712`; `MatrixGame/src/MatrixSide.cpp:491` | `SELECT_ENUM` takes `uintptr_t`, but `Update`/`UpdatePending` take `DWORD`; callers pass stack addresses | Widen to `uintptr_t` | High (source) | Selection contract with a non-32-bit parameter (`selection_tests.cpp` does not exercise it) | None |
| O10 | Pointer identity in `int` | `MatrixGame/src/Interface/CIFaceElement.h:58`; `MatrixGame/src/Interface/CInterface.cpp:1763,4717,4742`; `MatrixGame/src/MatrixObjectBuilding.cpp:1894` | Base pointer stored in `int m_iParam` and compared with `uintptr_t`; sign extension/truncation breaks build-queue icon matching. Comparisons produce no width warning | Typed pointer field or stable id (#93) | High (source) | Interface queue contract or factory playtest | None |
| O11 | Pointer rebasing after reallocation | `MatrixGame/src/Effects/MatrixEffectPath.cpp:82-104` | `DWORD rebase = new_base - old_base` wraps correctly only with 32-bit pointers; downward moves corrupt links on x64. It also relocates objects through reallocation | `std::ptrdiff_t` as a minimal fix; index-based links preferred, separately from the width fix | High (source) | Path growth test forcing relocation | None |
| O12 | Debug helper group keys | `MatrixLib/3G/Helper.hpp:60,66`; `MatrixGame/src/MatrixLogic.cpp:1390,1395,1477`; `MatrixGame/src/MatrixSide.cpp:4408,4419,7767-7769` | Pointer-derived keys truncated to `int`; possible collisions in debug drawing | `uintptr_t` keys | Medium | Compile check; debug-overlay smoke | None |
| O13 | `size_t` narrowing (87 C4267 sites) | Largest: `MatrixLib/Base/CBlockPar.cpp` (12), `CStorage.cpp` (10), `ShadowStencil.cpp` (8), `CFile.cpp` (8), `Text/Render.cpp` (6), `CHeap.hpp:233-288` (5) | Mostly bounded lengths; wire writers already emit `uint32_t` (`CStorage.cpp:105-114,161,262-264`) but do not check sizes above 4 GiB | Checked narrowing at wire and API boundaries; `size_t` internally; no blind replacement | Medium | Checked-conversion unit tests; storage boundary tests | None |
| O14 | Package file handles | `MatrixLib/Base/Pack.hpp:60-71,189,218-256`; `MatrixLib/Base/Pack.cpp:1504-1599,1757-1762,2005` | OS `HANDLE` kept in `uintptr_t` but exposed through `DWORD` APIs with a `0xFFFFFFFF` sentinel. Kernel handles have 32 significant bits, so this works by convention, not by type | Separate the 32-bit virtual handle from a typed OS handle | Medium | Package open/close/failure tests on x64 | None |
| O15 | Host ABI types | `MatrixGame/src/MatrixGameDll.hpp:19-21,91-131`; `MatrixLib/3G/3g.hpp:287-288`; `MatrixLib/3G/3g.cpp:255-273`; `MatrixGame/src/MatrixGame.cpp:336-348` | Pointers carried in `long` and 32-bit layouts are valid only for the x86 host | Keep as is; build the DLL only for x86; isolate standalone settings from the host struct | High | Keep `game.dll.interface_layout` x86 assertions; add all host struct sizes/offsets (#86) | None |
| O16 | Architecture-specific test assertions | `tests/dependency_tests.cpp:114`; `tests/dll_entry_tests.cpp:116-117` | x64 compile fails (C2607, measured). The first is a wire contract and must hold on x64 after O4; the second is the x86 host ABI | Keep the wire assertion unconditional; gate or exclude host-ABI layout checks off x86 (D3) | High | Self-evident | None |
| O17 | Floating-point model | i686 GCC defaults `-march=pentiumpro -mfpmath=387`, SSE2 disabled (measured); MSVC x86 default SSE2; x64 SSE2; `MatrixLib/3G/3g.cpp:192` creates the device without `D3DCREATE_FPU_PRESERVE` | x87 extended precision, then single precision after device creation on x86 only; x64 never uses x87 | No port fix; comparison criteria below; any x86 FP flag change is a separate gameplay decision (D5) | High (defaults), magnitude unknown | Differential scenarios (#105) | None |
| O18 | Pointer printing | `MatrixLib/Base/utils.hpp:12-31`; `MatrixGame/src/MatrixMap.cpp:1150-1161`; `MatrixGame/src/MatrixMapStatic.hpp:185`; `MatrixLib/Base/Tracer.cpp:222` | `%x` with `uintptr_t`, pointers or `ULONG_PTR`; unchecked variadic wrapper | `%p`/`PRIxPTR` or typed formatting | Low | Compile check | None |
| O19 | Compiled-out latent code | `MatrixLib/3G/Texture.hpp:30,147-167`; `MatrixLib/3G/Texture.cpp:392-397`; `MatrixLib/Base/Pack.cpp:1720-1731,2008-2032` | `(DWORD)this` reminder parameter and `(DWORD)CreateFileA` exist only behind disabled macros | None now; record for #88/#90; re-audit if re-enabled | Low | None | None |
| O20 | Assembly and intrinsics | `MatrixLib/Base/Tracer.hpp:15-31` | No live inline assembly (only comments); `__rdtsc` via `<x86intrin.h>` is valid on x86-64 GCC and MSVC | None | High | Compile on MinGW x64 | None |

Not obstacles after audit: no `#pragma pack` other than `Pack.hpp`, no pointer-keyed ordered/unordered containers or sorts that could make iteration order architecture-dependent, no `long double` use outside comments, standalone audio ids are fixed 32-bit counters (`MatrixGame/src/SoundBridge.hpp:10-16`, `MatrixGame/src/Audio/Audio.hpp:14,58-77`), and the MSVC compiler reported zero direct pointer-truncation casts (C4311/C4312/C4302) because earlier work already routes pointers through `uintptr_t`. That earlier migration is incomplete: O5, O7, O8 and O9 are `uintptr_t` values narrowed again at a `DWORD`/`uint32_t` signature.

## Exploratory compilation results

Unique project diagnostics, deduplicated by code, file and line (headers counted once). External headers contributed three C4244 in `xutility` on both architectures.

| Build | Unique | Breakdown |
| --- | --- | --- |
| MSVC x86 Debug (compile only) | 83 | C4244 45, C4018 25, C4996 11, C4826 2 |
| MSVC x64 Debug | 194 | C4267 87, C4244 86, C4996 11, C2065 6, C2607 3, C4018 1 |
| MSVC x64 Release | 162 | Subset of x64 Debug (no Release-only diagnostics) |
| x64-only versus x86 | 137 | C4267 87, C4244 41 (36 from `uintptr_t`, 5 from 64-bit pointer differences), C2065 6, C2607 3 |

Representative x64-only diagnostics: `MatrixLib/3G/3g.cpp:213` C2065 `GWL_WNDPROC`; `tests/dependency_tests.cpp:114` C2607 static assertion; `MatrixLib/Bitmap/CBitmap.cpp:887` C4244 `uintptr_t` to `uint32_t`; `MatrixGame/src/MatrixObjectRobot.cpp:999` C4244 `uintptr_t` to `DWORD` argument; `MatrixGame/src/Effects/MatrixEffectPath.cpp:91` C4244 `__int64` to `DWORD`; `MatrixLib/Base/Pack.hpp:218` C4244 `uintptr_t` to `DWORD` return; `MatrixLib/Base/CStorage.cpp:109` C4267 `size_t` to `uint32_t`. The x64-only C4244 set by directory: `MatrixGame/src` 25, `MatrixGame/src/Effects` 5, `MatrixGame/src/Interface` 1, `MatrixLib/Base` 9, `MatrixLib/Bitmap` 1. C4267 by directory: `MatrixLib/Base` 49, `MatrixGame/src` 17 (including Effects, Interface and Text), `MatrixLib/3G` 13, `MatrixLib/Bitmap` 5, tests 3.

Warning counts are a lower bound. Explicit integer casts, comparisons such as O10 and arithmetic such as `uintptr_t(this) + 4` do not warn, so the matrix also relies on targeted source searches.

Runtime results of the exploratory x64 executables (asset-free CTest label `engine`, 172 registered tests):

| Step | x64 Debug | x64 Release |
| --- | --- | --- |
| Build-only edits (O1 machine flag, O3, O16 gating) | 167 pass; fail `legacy_pack_blocks` (`pack.OpenPacketFile()`), `png.legacy_gray/rgb/rgba`, `png.write_round_trip` (access violation in `FilePNG::Read`) | Not run at this step |
| Plus O5 handle width | 171 pass; `legacy_pack_blocks` still fails | Not run at this step |
| Plus O4 side-table prototype | 172 pass | 171 pass; `bitmap.png.invalid` fails (exception escapes `ReadStart_Buf`) |
| Plus `/EHc-` for `FilePNG.cpp` | Not rerun | 172 pass |

The x64 executables report machine `0x8664`. The game EXE imports `d3d9.dll`, `d3dx9_43.dll`, `winmm`, `ole32` and the MSVC/UCRT runtime; test executables carry dynamic base and high-entropy VA, the game EXE carries high-entropy VA without dynamic base. Synthetic tests use fake devices; they do not establish x64 rendering, audio output, map loading or gameplay.

## Dependency and toolchain feasibility

| Component | x64 availability | Evidence |
| --- | --- | --- |
| MSVC x64 | Available. CI runner `windows-2022` provides VS 2022; the helper hard-codes `Visual Studio 17 2022` (`tools/build.ps1:83`). Locally only VS 18 (14.51) exists, so the MSVC helper path cannot run here unmodified | Measured build with 14.51; VS 2022 x64 unmeasured |
| MinGW x64 | The pinned WinLibs release `13.2.0mcf-11.0.1-ucrt-r3` publishes `winlibs-x86_64-mcf-seh-gcc-13.2.0-mingw-w64ucrt-11.0.1-r3.7z` (95,929,716 bytes) with `.sha256`/`.sha512` sidecars, matching the x86 GCC, mingw-w64, UCRT and MCF thread model with SEH instead of DWARF unwinding | Read-only GitHub API listing; not downloaded, hashed or compiled |
| CMake/Ninja | The pinned CMake 4.4.4 i386 and WinLibs Ninja drive x64 MSVC builds | Measured |
| Direct3D 9 | `d3d9.dll` is part of x64 Windows; `d3d9.lib` x64 ships in the Windows SDK and DXSDK | Measured locally |
| D3DX9 runtime | The pinned `directx_Jun2010_redist.exe` contains `Jun2010_d3dx9_43_x64.cab` beside the x86 cabinet; `setup-directx-runtime.ps1` extracts only x86 | Measured archive listing; x64 DLL hash not yet pinned |
| D3DX9/DxErr headers and libraries | MSVC needs DXSDK June 2010 `Include` and `Lib/x64`; CI extracts only `DXSDK/Lib/x86` (`win-build.yml:113`) and `FindDIRECTX9.cmake:23` searches `Lib/x86` first. MinGW uses mingw-w64 headers and `libd3dx9`/`libdxerr9` import libraries (`FindDIRECTX9.cmake:39`) | SDK layout inferred; not installed locally |
| zlib 1.3.2, libpng 1.6.59 | Built from the pinned sources by `ThirdParty/CMakeLists.txt` with the selected compiler | Measured MSVC x64 build and tests |
| Audio | XAudio2.9 is a Windows 10+ system component for x64 (`MatrixGame/src/Audio/CMakeLists.txt`); no OpenAL or other audio dependency exists | Measured system DLL present; audio tests use fake devices |
| Visual C++ runtime | MSVC x64 builds need the x64 VC++ redistributable; MinGW Release links statically (`CMakeLists.txt:79-81`) | Measured imports |

## Proposed build and release matrix

| Configuration | CI | Package | Notes |
| --- | --- | --- | --- |
| MinGW x86 Debug/Release EXE, Debug EXE with cheats | Required (existing) | x86 Release (existing) | Unchanged pins and output names |
| MSVC x86 Debug/Release EXE | Required (existing) | No | Unchanged |
| MinGW and MSVC x86 Release DLL with cheats | Required (existing) | No | Only supported DLL architecture |
| MinGW x64 Debug/Release EXE | Required after #103 | x64 Release (#106) | Same WinLibs release, x86_64 MCF SEH; static Release runtime like x86 |
| MSVC x64 Debug/Release EXE | Required after #103 | No | Compatibility and second-compiler diagnostics |
| Any x64 DLL | Rejected at configure time | No | The host process is 32-bit |

x64 output directories must be distinct, for example `build/mingw-x64-release-exe`, while x86 keeps its existing names (D1). Each job should verify PE machine type for the executable and every staged DLL so x86 and x64 runtimes cannot mix.

## Phased plan

| Phase | Issue | Bounded content | Exit evidence |
| --- | --- | --- | --- |
| 0 | #86 | Freeze wire layouts independent of `sizeof`: `SFileRec` 158/offsets, `SFolderRec`, nested-folder and malformed package fixtures, STRG/ZL03, map payload records, VO headers; freeze x86 host struct sizes/offsets and export name | Fixtures pass on x86 MinGW/MSVC; wire assertions unconditional, host assertions x86-only |
| 1a | #103 | Package record: fixed-width disk struct plus folder side table (O4); typed OS handle separate from virtual handle (O14) | x86 fixtures unchanged; x64 exploratory pass |
| 1b | #103 | PNG: handle width (O5) and an error boundary that never unwinds through libpng (O6) | PNG suite including new malformed cases in Release |
| 1c | #103 with #93 | Pointer-carrying parameters: draw chain (O7), effect user (O8), selection parameter (O9), interface identity (O10), helper keys (O12) | Zero x64-only C4244 in touched files; new non-32-bit round-trip contracts |
| 1d | #103 | Effect-path rebase arithmetic (O11) | Relocation test |
| 1e | #103 | Window procedure `LONG_PTR` (O3) and pointer formatting (O18) | Lifecycle host-window tests |
| 1f | #103 | Checked narrowing at storage, file and API boundaries (O13) | Boundary unit tests; no format byte change |
| 2 | #104 | CMake architecture detection, machine/SAFESEH selection, DLL rejection on x64, `FindDIRECTX9` `Lib/x64`, helper `-Architecture`, pinned x64 WinLibs and D3DX cabinet, CI jobs | Clean CI for all rows of the matrix |
| 3 | #105 | Differential runs under the criteria below; native launch/display/audio/exit on x64 | Recorded evidence; every divergence diagnosed |
| 4 | #106 | Architecture-aware `package_game.py`/`package.ps1`, release workflow job, prerequisites text | Fresh-extraction smoke on both architectures |

Phase 1 leaves are structural changes and remain gated by the behavior baseline (#69). Phase 2 should not become required CI before Phase 1, because MinGW `-Werror` on x86_64 is expected to turn the same conversions into errors.

## Architecture-comparison criteria

Exact equality across x86 and x64 is required for: decoded resource and configuration content, synthetic byte fixtures, storage re-encoding of uncompressed data, file lookup results, raw RNG sequences and draw counts for both generators, integer command/state transitions in deterministic scripted fixtures until the first floating-point-dependent decision, startup diagnostics and exit codes, and absence of any loss of package/PNG/map content.

Expected but diagnosable: event sequences and integer outcomes of short controlled scenarios with injected time and seeds (#76, #79). The first divergent event must be traced; acceptable root causes are documented floating-point model differences, not width defects. Tolerances apply only to floating-point observables, are recorded with justification, and are never widened to make a run pass.

Not promised: bit-identical floating-point state or whole-battle outcomes between x86 MinGW (x87, single precision after device creation), x86 MSVC (SSE2) and x64 (SSE2), or identical rendered pixels. MSVC x86 versus MSVC x64 is the closest pair and should be the primary differential for isolating width defects; MinGW x86 versus x64 additionally includes the x87 effect. Pointer values, allocation addresses and handle values are never comparison inputs. Performance comparison uses the representative measurements of #78 and reports, rather than gates on, architecture differences.

## Constraints for #91 and #99

Architecture boundaries (#91) should not use raw addresses as identities, group keys or `int`/`DWORD` user data; use typed references or the stable entity references proposed by #93. On-disk records must not contain runtime pointers or ownership. The host ABI in `MatrixGameDll.hpp` is an x86-only adapter: the standalone session and settings model should not reuse `SRobotsSettings`, and adapters convert host types at the boundary. C++ exceptions must not cross C libraries or host `__stdcall` callbacks. Objects with internal pointers must not be relocated by reallocation or byte copies.

The automation API (#99) should use explicit fixed-width little-endian encodings, entity ids that are counters rather than addresses, and defined float encodings with documented comparison rules, so one client works with x86 and x64 builds. The same rules apply to future save/load (#67).

## Alternatives

| Alternative | Assessment |
| --- | --- |
| Width fixes in place with frozen wire layouts (recommended) | Smallest diff; measured to reach a passing asset-free suite |
| Ship only x86, running under WOW64 | No porting risk and no x64 benefit; the epic requires x64 |
| x86 with `/LARGEADDRESSAWARE` | Gains 4 GiB under WOW64 but exposes O10-style signed pointer comparisons; needs a similar audit for less benefit |
| Native-width wire formats or a global packing change | Rejected: breaks resources and contradicts #103 |
| Single x64 compiler | MSVC alone avoids a new download but changes the packaged runtime; MinGW alone loses second-compiler diagnostics. Both in CI, MinGW packaged, is preferred |
| Replace D3DX9 | Unnecessary; x64 redistributable and import libraries exist |

## Unresolved decisions

- D1: x64 output directory names and the helper parameter shape; whether x86 names stay unchanged.
- D2: x64 ASLR policy (`/DYNAMICBASE` and high-entropy VA); keeping x86 MSVC `/DYNAMICBASE:NO`. High-entropy ASLR in tests is recommended because it exposed O5.
- D3: whether x64 builds compile the DLL entry tests with host-layout checks gated, or exclude them.
- D4: libpng error boundary mechanism.
- D5: whether x86 MinGW should ever move to SSE2 math; this changes gameplay arithmetic and requires a maintainer decision.
- D6: MSVC pin (runner image versus explicit toolset version) and local VS 18 support in `build.ps1`.
- D7: x64 D3DX runtime delivery as a documented prerequisite (current x86 practice) versus bundling.
- D8: whether the x64 EXE keeps compiling and exporting `GetRobotInterface`.
- D9: scope of the 87 C4267 sites (boundary-only fixes versus full cleanup) and an MSVC x64 warning-as-error policy.

## Coverage gaps

- No MinGW x64 compiler was installed or downloaded; GCC x86_64 diagnostics under `-Werror` and SEH unwinding through libpng are unmeasured.
- MSVC probes used substitute D3DX headers, a generated import library and a stub DxErr, not the June 2010 SDK; the MSVC x86 probe was compile-only.
- Debug/Release DLL configurations, cheats code (`CHEATS_ON`, `MatrixGame/CMakeLists.txt:59-65`) and DLL-only branches were not compiled for x64.
- Runtime results are asset-free synthetic contracts with fake devices and the exploratory edits listed under Identities applied; no rendering, audio device, real package, map, input or battle ran on x64.
- Warning-based discovery misses explicit casts and comparisons; O7-O12 come from targeted source reading and need tests to confirm runtime effects.
- No byte-for-byte check of a real `robots.pkg` directory tree was made (proprietary content was not read); nested-folder behavior of the side-table prototype is untested.
- The x64 D3DX DLL inside the pinned redistributable was not extracted or hashed.

## Evidence files

The `evidence/` directory beside this report holds the exploratory material, with local directories replaced by `%REPO%` (this checkout), `%WORK%` (a scratch directory) and `<work>`.

- `exploratory-x64.patch`: the nine-file exploratory edit set applied to an exported source copy for the x64 runtime runs. It applies to 540251d with `git apply`. It is evidence only, not a proposed implementation; the reviewed fixes belong to #103 and #104.
- `probes/layout_probe.cpp` and `probes/layout_*.txt`: the layout probe over the real project headers and its output for i686 GCC, MSVC x86 and MSVC x64; `layout_compare.txt` is the side-by-side table.
- `scripts/`: the MSVC configure, build and probe scripts. `mklibs.bat` builds the stub `DxErr.lib`, copies the Windows SDK `d3d9.lib` and dumps the exports of the system x64 `d3dx9_43.dll`; `mkimp.bat` turns a `.def` written from that dump into the x64 `d3dx9.lib` import library.
- `shim/`: the `WINBOOL` shim and the `DxErr` stub used with the mingw-w64 D3DX9 headers. The headers themselves are not included; they come from the mingw-w64 headers shipped with the pinned WinLibs toolchain.
- `logs/`: CTest output of the exploratory x64 executables: `ctest-edited-*` after the package side-table step and `ctest-edited2-*` after the final step.
