// Throwaway #76: set the x87 control word that D3D9 installs, without any device.
#define _USE_MATH_DEFINES
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <float.h>
#include "random.hpp"
static unsigned short cw() { unsigned short c; __asm__ __volatile__("fnstcw %0" : "=m"(c)); return c; }
struct Fnv { uint64_t h = 1469598103934665603ULL;
  template<class T> void add(const T &v) { auto *b = reinterpret_cast<const unsigned char *>(&v); for (size_t i = 0; i < sizeof(v); ++i) { h ^= b[i]; h *= 1099511628211ULL; } } };
int main() {
  _control87(_PC_24, _MCW_PC);
  random::seed(12345, random::Mode::ParkMiller); Fnv a; for (int i = 0; i < 1000000; ++i) a.add(random::RndFloat());
  random::seed(12345, random::Mode::ParkMiller); Fnv b; for (int i = 0; i < 1000000; ++i) b.add(RND(-3.7, 11.2));
  random::seed(12345, random::Mode::ParkMiller); Fnv c; for (int i = 0; i < 1000000; ++i) c.add(FSRND(M_PI));
  std::printf("pc24 cw=%04x RndFloat=%016" PRIx64 " RND=%016" PRIx64 " FSRND=%016" PRIx64 "\n", cw(), a.h, b.h, c.h);
}
