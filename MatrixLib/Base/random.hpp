#pragma once

namespace random
{

enum class Mode { ParkMiller, LegacyCRT };

// Reseeding preserves the selected mode; a new battle selects it explicitly.
void seed(unsigned int val);
void seed(unsigned int val, Mode selected_mode);
Mode mode();

// Park-Miller's maximum. Legacy CRT output uses RAND_MAX instead.
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
// Select an index in [0, n). Legacy CRT retains rounded endpoint weighting.
// Non-positive counts return zero. Every call consumes one raw draw.
int IRND(int n);
