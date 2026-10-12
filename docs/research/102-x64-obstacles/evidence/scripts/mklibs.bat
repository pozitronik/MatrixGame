@echo off
setlocal
set T=%WORK%
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" %1 >nul || exit /b 1
if "%1"=="x64" (set R=%T%\fake-dxsdk-x64\Lib) else (set R=%T%\fake-dxsdk-x86\Lib\x86)
cd /d %R% || exit /b 1
cl /nologo /c /I%T%\fake-dxsdk-x64\Include %T%\fake-dxsdk-x64\src\dxerr_stub.c /Fo:dxerr_stub.obj || exit /b 1
lib /nologo /out:DxErr.lib dxerr_stub.obj || exit /b 1
del dxerr_stub.obj
copy /y "C:\Program Files (x86)\Windows Kits\10\Lib\10.0.26100.0\um\%1\d3d9.lib" d3d9.lib >nul || exit /b 1
if "%1"=="x64" (
  dumpbin /nologo /exports C:\Windows\System32\d3dx9_43.dll > %T%\d3dx9_43_x64_exports.txt
) else (
  rem x86 baseline is compile-only; placeholder import library
  copy /y DxErr.lib d3dx9.lib >nul
)
