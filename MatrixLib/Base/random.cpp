#include "random.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <random>

namespace {

// Same 16807/2147483647 generator used by the original map simulation.
std::minstd_rand0 generator;
random::Mode active_mode = random::Mode::ParkMiller;

int active_maximum() {
    return active_mode == random::Mode::LegacyCRT ? RAND_MAX : random::maximum;
}

}  // namespace

namespace random
{

void seed(unsigned int val)
{
    seed(val, active_mode);
}

void seed(unsigned int val, Mode selected_mode)
{
    active_mode = selected_mode;
    if (active_mode == Mode::LegacyCRT) {
        std::srand(val);
    } else {
        generator.seed(val);
    }
}

Mode mode()
{
    return active_mode;
}

int Rnd()
{
    return active_mode == Mode::LegacyCRT ? std::rand() : static_cast<int>(generator() - 1);
}

double RndFloat()
{
    return static_cast<double>(Rnd()) / active_maximum();
}

int Rnd(int zmin, int zmax)
{
    if (zmin > zmax) {
        std::swap(zmin, zmax);
    }
    const auto width = static_cast<uint64_t>(static_cast<int64_t>(zmax) - zmin) + 1;
    const uint64_t source_width = static_cast<uint64_t>(active_maximum()) + 1;
    uint64_t value = static_cast<uint64_t>(Rnd());
    uint64_t capacity = source_width;
    while (width > capacity) {
        value = value * source_width + static_cast<uint64_t>(Rnd());
        capacity *= source_width;
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
    if (random::mode() == random::Mode::LegacyCRT) {
        const uint64_t draw = static_cast<uint64_t>(random::Rnd());
        if (n <= 0) return 0;
        // Positive rounding without floating conversion or overflowing n - 1.
        return static_cast<int>((draw * static_cast<uint64_t>(n - 1) + RAND_MAX / 2) / RAND_MAX);
    }
    return random::Rnd(0, n > 0 ? n - 1 : 0);
}
