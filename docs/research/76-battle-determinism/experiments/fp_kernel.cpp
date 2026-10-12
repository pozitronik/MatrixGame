// Throwaway #76 kernel. Replicas (not production code) of two engine arithmetic shapes:
// smoke puff rise from MatrixEffectSmokeAndFire.cpp (CMatrixEffectSmoke::Takt) and a
// float position/velocity integration with normalization typical of movement code.
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>
struct Fnv { uint64_t h = 1469598103934665603ULL;
  void add(float v) { unsigned char b[4]; std::memcpy(b, &v, 4); for (int i = 0; i < 4; ++i) { h ^= b[i]; h *= 1099511628211ULL; } } };
volatile float g_step = 10.0f;  // prevent constant folding of the schedule
int main() {
  const float speed = 1.0f / 25.0f;
  Fnv f1, f2;
  float z = 0.0f, orig = 0.0f;
  int first1 = -1;
  float zt[400];
  for (int i = 0; i < 400; ++i) {
    float step = g_step;
    float dtime = speed * step;
    z += dtime * speed * (5 + (z - orig));
    zt[i] = z;
    f1.add(z);
  }
  float px = 0, py = 0, vx = 1.3f, vy = -0.7f, tx = 900.0f, ty = 400.0f;
  for (int i = 0; i < 20000; ++i) {
    float step = g_step;
    float dx = tx - px, dy = ty - py;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len > 1e-3f) { dx /= len; dy /= len; }
    vx = vx * 0.98f + dx * 0.05f * (step / 10.0f);
    vy = vy * 0.98f + dy * 0.05f * (step / 10.0f);
    px += vx * step * 0.1f; py += vy * step * 0.1f;
    if (len < 5.0f) { tx = -tx * 0.5f + py; ty = ty * 0.75f - px * 0.25f; }
    f2.add(px); f2.add(py);
  }
  std::printf("smoke_rise hash=%016" PRIx64 " z[399]=%.9g\n", f1.h, zt[399]);
  std::printf("movement   hash=%016" PRIx64 " px=%.9g py=%.9g\n", f2.h, px, py);
  (void)first1;
}
