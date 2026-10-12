// Throwaway #76: dumps raw bits of production floating helpers for ulp comparison.
#define _USE_MATH_DEFINES
#include "random.hpp"
#include <cmath>
#include <cstdio>
int main(int, char **argv) {
  random::seed(12345, random::Mode::ParkMiller);
  FILE *f = std::fopen(argv[1], "wb");
  for (int i = 0; i < 1000000; ++i) { double a = random::RndFloat(); std::fwrite(&a, 8, 1, f); }
  random::seed(12345, random::Mode::ParkMiller);
  for (int i = 0; i < 1000000; ++i) { double a = RND(-3.7, 11.2); std::fwrite(&a, 8, 1, f); }
  random::seed(12345, random::Mode::ParkMiller);
  for (int i = 0; i < 1000000; ++i) { float a = FSRND(M_PI); std::fwrite(&a, 4, 1, f); }
  std::fclose(f);
}
