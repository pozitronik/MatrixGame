// Exploratory issue 102 shim for MSVC use of mingw-w64 D3DX headers.
#pragma once
#include <windows.h>
#ifndef WINBOOL
#define WINBOOL BOOL
#endif
