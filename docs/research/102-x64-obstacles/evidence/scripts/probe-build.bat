@echo off
rem Exploratory issue 102 compilation probe. Usage: probe-build.bat <x64|x86> <Debug|Release> <exe|dll>
setlocal
set ARCH=%1
set CFG=%2
set KIND=%3
set T=%WORK%
set W=%REPO%
set TOOLS=%REPO%\.tools
set VSLANG=1033
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" %ARCH% >nul 2>&1 || exit /b 1
set DXSDK_DIR=%T%\fake-dxsdk-%ARCH%
set B=%T%\build-msvc-%ARCH%-%CFG%-%KIND%
if "%KIND%"=="dll" (set DLLOPT=ON) else (set DLLOPT=OFF)
set WFLAGS=/w14311 /w14312 /w14302 /w14267 /w14244 /w14334 /w14305 /w14826 /w14477 /w14313
"%TOOLS%\cmake-4.4.4-windows-i386\bin\cmake.exe" -S %W% -B %B% -G Ninja -DCMAKE_MAKE_PROGRAM=%TOOLS%\winlibs-13.2.0\mingw32\bin\ninja.exe -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=%CFG% -DMATRIXGAME_BUILD_DLL=%DLLOPT% -DMATRIXGAME_CHEATS=OFF -DBUILD_TESTING=ON -DMATRIXGAME_PKG_BRING_FROM_GAME=OFF "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /GR /EHsc %WFLAGS%" "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS" > %T%\configure-%ARCH%-%CFG%-%KIND%.log 2>&1 || (echo configure failed & exit /b 1)
"%TOOLS%\cmake-4.4.4-windows-i386\bin\cmake.exe" --build %B% --parallel 8 -- -k 0 > %T%\build-%ARCH%-%CFG%-%KIND%.log 2>&1
echo build exit %ERRORLEVEL%
