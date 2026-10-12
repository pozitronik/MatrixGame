#include "dxerr.h"
const WCHAR *WINAPI DXGetErrorStringW(HRESULT hr) { (void)hr; return L"probe"; }
const WCHAR *WINAPI DXGetErrorDescriptionW(HRESULT hr) { (void)hr; return L"probe"; }
