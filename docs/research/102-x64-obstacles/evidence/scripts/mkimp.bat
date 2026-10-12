@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1 || exit /b 1
lib /nologo /def:%WORK%\d3dx9_43_x64.def /machine:x64 /out:%WORK%\fake-dxsdk-x64\Lib\d3dx9.lib
