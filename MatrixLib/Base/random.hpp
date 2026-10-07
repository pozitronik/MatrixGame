#pragma once

namespace random
{

void seed(unsigned int val);

// Park-Miller output shifted to [0, 2147483645]. All helpers share this stream.
inline constexpr int maximum = 2147483645;
int Rnd();
// Floating ranges include both endpoints; reversed bounds are normalized.
double RndFloat();
// Inclusive integer range. Ordinary ranges retain the legacy modulo mapping.
int Rnd(int zmin, int zmax);
double RndFloat(double zmin, double zmax);

} // namespace random

double RND(double from, double to);
float FRND(double x);
float FSRND(double x);
// Select an index in [0, n). Non-positive counts return zero.
int IRND(int n);
