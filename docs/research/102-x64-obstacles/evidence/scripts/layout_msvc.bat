@echo off
setlocal
set T=%WORK%
set W=%REPO%
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" %1 >nul 2>&1 || exit /b 1
cd /d %T%\probes
cl /nologo /std:c++20 /EHsc /DNOMINMAX /DWINDOWS_IGNORE_PACKING_MISMATCH /I%T%\fake-dxsdk-%1\Include /I%W%\MatrixLib\Base /I%W%\MatrixLib\3G /I%W%\MatrixLib\Bitmap /I%W%\MatrixGame\src /I%W%\MatrixGame layout_probe.cpp /Fe:layout_msvc_%1.exe /Fo:layout_msvc_%1.obj > layout_msvc_%1.compile.log 2>&1 || (type layout_msvc_%1.compile.log & exit /b 1)
layout_msvc_%1.exe > layout_msvc_%1.txt
cl 2>&1 | findstr /C:"Version"
