Issue 102 exploratory x64 MSVC rerun: substitute DirectX SDK and patch stages

Placeholders: <work> is the scratch work directory of this rerun; <repo> is the project checkout. Nothing in <repo> was modified.

1. Source
- git -C <repo> archive 540251d4662c590bad4684df01e0c521e6c4aee7, extracted to <work>/src-x64 (files have CRLF line endings, as the exploratory patch does).
- exploratory-x64.patch SHA-256 66e27729f04fc35a9b9f2d4316e5e1980a7cf72502138eb1e730053d5adcfea7, used unmodified.

2. Substitute DirectX SDK (<work>/fake-dxsdk-x64), found through DXSDK_DIR by cmake/FindDIRECTX9.cmake (Include, then Lib because Lib/x86 does not exist)
Include:
- Copied unmodified from the pinned WinLibs 13.2.0 toolchain (<repo>/.tools/winlibs-13.2.0/mingw32/i686-w64-mingw32/include): d3dx9.h, d3dx9anim.h, d3dx9core.h, d3dx9effect.h, d3dx9math.h, d3dx9math.inl, d3dx9mesh.h, d3dx9shader.h, d3dx9shape.h, d3dx9tex.h, d3dx9xof.h, _mingw_unicode.h (needed for __MINGW_NAME_AW and __MINGW_TYPEDEF_AW).
- WINBOOL shim wiring: evidence/shim/probe_shim.h copied into Include, and the single line `#include "probe_shim.h"` prepended to every copied header that uses WINBOOL (d3dx9anim.h, d3dx9core.h, d3dx9effect.h, d3dx9math.h, d3dx9mesh.h, d3dx9shader.h, d3dx9xof.h). No forced include and no compiler flag was added; the CMake flags are those of probe-build-edited.bat.
- d3d9.h, d3d9types.h and d3d9caps.h are NOT copied: they come from the Windows SDK 10.0.26100.0 (shared include directory). A first attempt that also copied the mingw-w64 d3d9*.h headers failed to compile under MSVC (C3861 `__MSABI_LONG` undefined, then C2660 on IDirect3DDevice9::CreateIndexBuffer/CreateVertexBuffer in MatrixLib/3G/D3DControl.hpp), because those headers use mingw-only macros that the WINBOOL shim does not provide.
- dxerr.h stub (written for this rerun): includes <windows.h> and declares, with C linkage, `const WCHAR *WINAPI DXGetErrorStringW(HRESULT)` and `const WCHAR *WINAPI DXGetErrorDescriptionW(HRESULT)`; MatrixLib/3G/3g.cpp includes "dxerr.h" on non-GNU compilers.
Lib:
- DxErr.lib: evidence/shim/dxerr_stub.c (copied to <work>/fake-dxsdk-x64/src) compiled with cl x64 and archived with lib, by evidence/scripts/mklibs.bat x64.
- d3d9.lib: copied from Windows Kits 10.0.26100.0 um/x64 by mklibs.bat.
- d3dx9.lib: mklibs.bat writes `dumpbin /exports` of the x64 system d3dx9_43.dll (329 named exports, export time stamp 4BF70A1E, file SHA-256 84b900dbd7fa978d6e0caee26fc54f2f61d92c9c75d10b35f00e3e82cd1d67b4); an awk filter turns the name column into d3dx9_43_x64.def (`LIBRARY d3dx9_43.dll`, `EXPORTS`, one undecorated name per line); evidence/scripts/mkimp.bat runs `lib /def:... /machine:x64` to produce the import library.
Runtime: tests resolve d3dx9_43.dll from the x64 system directory; no DLL is staged into the build directories.

3. Build
- Driver mirrors evidence/scripts/probe-build-edited.bat for x64 EXE: vcvarsall x64 (Visual Studio 18 Community, cl 19.51.36252), VSLANG=1033, pinned CMake 4.4.4 i386 and WinLibs Ninja 1.11.1, -DMATRIXGAME_BUILD_DLL=OFF -DMATRIXGAME_CHEATS=OFF -DBUILD_TESTING=ON -DMATRIXGAME_PKG_BRING_FROM_GAME=OFF, the same CMAKE_CXX_FLAGS/CMAKE_C_FLAGS, DXSDK_DIR=<work>/fake-dxsdk-x64, `cmake --build --parallel 8 -- -k 0`.
- One build directory per configuration (<work>/build-x64-Debug, <work>/build-x64-Release), rebuilt incrementally after each stage. All eight stage/configuration builds succeeded.
- Tests: `ctest -L engine --output-on-failure --timeout 120` run serially from the build directory with the pinned CTest 4.4.4. No game executable or window was launched.

4. Patch stages (cumulative; each stage applied with `git apply --include=<path>` from the unmodified patch, with git repository discovery disabled above <work>)
The patch hunks map cleanly by file; no hunk had to be split.
- S1 (O1, O3, O16), 8 hunks in 5 files:
  CMakeLists.txt (1 hunk: /MACHINE:X86 removed from MSVC LINK_OPTIONS) - O1
  MatrixLib/3G/3g.cpp (4 hunks: g_WndOldProg as LONG_PTR, GWLP_WNDPROC, Get/SetWindowLongPtr) - O3
  tests/lifecycle_tests.cpp (1 hunk: GWLP_WNDPROC) - O3
  tests/dependency_tests.cpp (1 hunk: static_assert(sizeof(SFileRec) == 158) under #ifndef _WIN64) - O16
  tests/dll_entry_tests.cpp (1 hunk: SMGDRobotInterface size/offset static_asserts under #ifndef _WIN64) - O16
- S2 = S1 + O5, 1 hunk: MatrixLib/Bitmap/CBitmap.cpp (ReadStart_Buf result kept as uintptr_t).
- S3 = S2 + O4, 12 hunks: MatrixLib/Base/Pack.hpp (2 hunks: m_Offset/m_Extra as uint32_t; m_SubFolders side table and SubFolder accessor) and MatrixLib/Base/Pack.cpp (10 hunks: side table filled in ReadFolder, released in Clear, used by every subfolder lookup).
- S4 = S3 + /EHc-, 1 hunk: MatrixLib/CMakeLists.txt (set_source_files_properties(Bitmap/FilePNG.cpp ... /EHc- under MSVC)); the O6 fallback.
After S4, `git apply -R --check` of the whole patch succeeds on <work>/src-x64, so the four stages together equal the published patch.
Note: the O16 hunk in tests/dependency_tests.cpp keeps the SFileRec wire assertion gated on x64 in every stage, including after O4.

5. Output files
ctest-x64-S<n>-<Debug|Release>.txt: header (revision, patch hash, stage and obstacle IDs, cl banner, configuration, CTest command) followed by the CTest output with absolute local paths replaced by <work> and <repo>, LF line endings.
