// Throwaway #76 driver: hashes production random helpers compiled with a chosen compiler/flags.
#define _USE_MATH_DEFINES
#include "random.hpp"
#include <cinttypes>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
struct Fnv { uint64_t h = 1469598103934665603ULL;
  template<class T> void add(const T &v) { auto *b = reinterpret_cast<const unsigned char *>(&v); for (size_t i = 0; i < sizeof(v); ++i) { h ^= b[i]; h *= 1099511628211ULL; } } };
int main(int argc, char **argv) {
  const std::string m = argc > 1 ? argv[1] : "ParkMiller";
  const auto mode = m == "LegacyCRT" ? random::Mode::LegacyCRT : random::Mode::ParkMiller;
  struct Helper { const char *name; void (*fn)(Fnv &); };
  const Helper helpers[] = {
    {"Rnd()", [](Fnv &f) { f.add(random::Rnd()); }},
    {"RndFloat()", [](Fnv &f) { f.add(random::RndFloat()); }},
    {"RND(-3.7,11.2)", [](Fnv &f) { f.add(RND(-3.7, 11.2)); }},
    {"RND(0,M_PI)", [](Fnv &f) { f.add(RND(0, M_PI)); }},
    {"FRND(5.5)", [](Fnv &f) { f.add(FRND(5.5)); }},
    {"FRND(128)", [](Fnv &f) { f.add(FRND(128)); }},
    {"FSRND(M_PI)", [](Fnv &f) { f.add(FSRND(M_PI)); }},
    {"IRND(7)", [](Fnv &f) { f.add(IRND(7)); }},
    {"Rnd(-10,10)", [](Fnv &f) { f.add(random::Rnd(-10, 10)); }},
    {"Rnd(INT_MIN,INT_MAX)", [](Fnv &f) { f.add(random::Rnd(INT_MIN, INT_MAX)); }},
  };
  for (const auto &h : helpers) {
    random::seed(12345, mode); Fnv f;
    for (int i = 0; i < 1000000; ++i) h.fn(f);
    const int tail = random::Rnd();
    std::printf("%s %s hash=%016" PRIx64 " next_raw=%d\n", m.c_str(), h.name, f.h, tail);
  }
}
