// Throwaway #76 probe: x87 control word and production RND results before/after a
// D3D9 device is created with the engine's flags. The window is never shown.
#define _USE_MATH_DEFINES
#include <windows.h>
#include <d3d9.h>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <string_view>
#include "random.hpp"
static unsigned short cw() { unsigned short c; __asm__ __volatile__("fnstcw %0" : "=m"(c)); return c; }
struct Fnv { uint64_t h = 1469598103934665603ULL;
  template<class T> void add(const T &v) { auto *b = reinterpret_cast<const unsigned char *>(&v); for (size_t i = 0; i < sizeof(v); ++i) { h ^= b[i]; h *= 1099511628211ULL; } } };
static void hashes(const char *when) {
  random::seed(12345, random::Mode::ParkMiller); Fnv a; for (int i = 0; i < 1000000; ++i) a.add(random::RndFloat());
  random::seed(12345, random::Mode::ParkMiller); Fnv b; for (int i = 0; i < 1000000; ++i) b.add(RND(-3.7, 11.2));
  random::seed(12345, random::Mode::ParkMiller); Fnv c; for (int i = 0; i < 1000000; ++i) c.add(FSRND(M_PI));
  random::seed(12345, random::Mode::ParkMiller); Fnv d; for (int i = 0; i < 1000000; ++i) d.add(FRND(128));
  std::printf("%s cw=%04x RndFloat=%016" PRIx64 " RND=%016" PRIx64 " FSRND=%016" PRIx64 " FRND=%016" PRIx64 "\n", when, cw(), a.h, b.h, c.h, d.h);
}
int main(int argc, char **argv) {
  const bool preserve = argc > 1 && std::string_view(argv[1]) == "preserve";
  hashes("before");
  HINSTANCE inst = GetModuleHandle(nullptr);
  WNDCLASSA wc{}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = inst; wc.lpszClassName = "mg76fpu";
  RegisterClassA(&wc);
  HWND wnd = CreateWindowA("mg76fpu", "", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, inst, nullptr);  // never shown
  IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
  std::printf("after Direct3DCreate9 cw=%04x\n", cw());
  D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.BackBufferFormat = D3DFMT_UNKNOWN;
  pp.hDeviceWindow = wnd; pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
  IDirect3DDevice9 *dev = nullptr;
  DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | (preserve ? D3DCREATE_FPU_PRESERVE : 0);
  HRESULT hr = d3d ? d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd, flags, &pp, &dev) : E_FAIL;
  std::printf("CreateDevice preserve=%d hr=%08lx\n", preserve ? 1 : 0, (unsigned long)hr);
  hashes("after");
  if (dev) dev->Release();
  if (d3d) d3d->Release();
  DestroyWindow(wnd);
  UnregisterClassA("mg76fpu", inst);
  hashes("after_release");
}
