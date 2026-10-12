// Throwaway #76 microbenchmark: cost of a draw counter and of per-tick trace hashing.
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include "random.hpp"
struct Fnv { uint64_t h = 1469598103934665603ULL;
  void bytes(const void *p, size_t n) { auto *b = static_cast<const unsigned char *>(p); for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; } } };
int main() {
  using clk = std::chrono::steady_clock;
  std::vector<double> t_rnd, t_frnd, t_hash;
  volatile int sink = 0; volatile float fsink = 0;
  std::vector<unsigned char> state(200 * 64, 1);  // 200 objects x 64 observed bytes
  for (int rep = 0; rep < 15; ++rep) {
    random::seed(1, random::Mode::ParkMiller);
    auto a = clk::now(); for (int i = 0; i < 10000000; ++i) sink += random::Rnd(); auto b = clk::now();
    t_rnd.push_back(std::chrono::duration<double, std::nano>(b - a).count() / 1e7);
    a = clk::now(); for (int i = 0; i < 10000000; ++i) fsink += FRND(5.0); b = clk::now();
    t_frnd.push_back(std::chrono::duration<double, std::nano>(b - a).count() / 1e7);
    a = clk::now(); Fnv f; for (int i = 0; i < 1000; ++i) { state[i % state.size()]++; f.bytes(state.data(), state.size()); } b = clk::now();
    sink += int(f.h);
    t_hash.push_back(std::chrono::duration<double, std::micro>(b - a).count() / 1000);
  }
  auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
  std::printf("Rnd ns/call median=%.3f min=%.3f | FRND ns/call median=%.3f | FNV of 12800 bytes us/hash median=%.2f\n",
              med(t_rnd), *std::min_element(t_rnd.begin(), t_rnd.end()), med(t_frnd), med(t_hash));
}
