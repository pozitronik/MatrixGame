#include "random.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace {

// Same 16807/2147483647 generator used by the original map simulation.
std::minstd_rand0 generator;

}  // namespace

namespace random
{

void seed(unsigned int val)
{
    generator.seed(val);
}

int Rnd()
{
    return static_cast<int>(generator() - 1);
}

double RndFloat()
{
    return static_cast<double>(Rnd()) / maximum;
}

int Rnd(int zmin, int zmax)
{
    if (zmin > zmax) {
        std::swap(zmin, zmax);
    }
    const auto width = static_cast<uint64_t>(static_cast<int64_t>(zmax) - zmin) + 1;
    constexpr uint64_t source_width = static_cast<uint64_t>(maximum) + 1;
    uint64_t value = static_cast<uint64_t>(Rnd());
    if (width > source_width) {
        value = value * source_width + static_cast<uint64_t>(Rnd());
    }
    return static_cast<int>(static_cast<int64_t>(zmin) + static_cast<int64_t>(value % width));
}

double RndFloat(double zmin, double zmax)
{
    if (zmin > zmax) {
        std::swap(zmin, zmax);
    }
    return std::lerp(zmin, zmax, RndFloat());
}

} // namespace random

double RND(double from, double to)
{
    return random::RndFloat(from, to);
}

float FRND(double x)
{
    return static_cast<float>(RND(0, x));
}

float FSRND(double x)
{
    return static_cast<float>(RND(-std::abs(x), std::abs(x)));
}

int IRND(int n)
{
    return random::Rnd(0, n > 0 ? n - 1 : 0);
}
