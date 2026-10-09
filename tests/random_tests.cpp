// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "random.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace {

void unit_interval() {
    random::seed(42);
    std::array<bool, 4> visited{};
    for (int i = 0; i < 512; ++i) {
        const double value = random::RndFloat();
        MG_CHECK(value >= 0.0 && value <= 1.0);
        const int band = value == 1.0 ? 3 : static_cast<int>(value * 4);
        visited[band] = true;
    }
    for (bool present : visited) {
        MG_CHECK(present);
    }
}

void fractional_scales() {
    random::seed(42);
    bool positive = false;
    bool negative = false;
    for (int i = 0; i < 512; ++i) {
        const float value = FSRND(0.125);
        MG_CHECK(value >= -0.125f && value <= 0.125f);
        positive = positive || value > 0;
        negative = negative || value < 0;
    }
    MG_CHECK(positive && negative);
}

void original_sequence() {
    constexpr std::array<int, 10> expected = {16806, 282475248, 1622650072, 984943657, 1144108929,
                                             470211271, 101027543, 1457850877, 1458777922, 2007237708};
    random::seed(1);
    for (int value : expected) {
        MG_CHECK(random::Rnd() == value);
    }
    random::seed(1);
    for (int value : expected) {
        MG_CHECK(random::Rnd() == value);
    }
}

void seed_normalization() {
    for (unsigned int value : {0U, 1U, 2147483647U, std::numeric_limits<unsigned int>::max()}) {
        random::seed(value);
        MG_CHECK(random::Rnd() == 16806);
    }
}

void floating_bounds() {
    // These inverse-generator seeds select the exact two endpoints.
    random::seed(1407677000);
    MG_CHECK(random::RndFloat() == 0.0);
    random::seed(739806647);
    MG_CHECK(random::RndFloat() == 1.0);
    random::seed(1407677000);
    MG_CHECK(random::RndFloat(-3.75, -1.25) == -3.75);
    random::seed(739806647);
    MG_CHECK(random::RndFloat(-1.25, -3.75) == -1.25);
    random::seed(23);
    for (int i = 0; i < 512; ++i) {
        MG_CHECK(random::RndFloat(7.25, 7.25) == 7.25);
        const double value = RND(-1.25, -3.75);
        MG_CHECK(value >= -3.75 && value <= -1.25);
        const double large = random::RndFloat(-std::numeric_limits<double>::max(),
                                              std::numeric_limits<double>::max());
        MG_CHECK(std::isfinite(large));
        const float small = FRND(0.125);
        MG_CHECK(small >= 0 && small <= 0.125f);
    }
}

void integer_bounds() {
    random::seed(17);
    std::array<int, 512> forward{};
    for (auto &value : forward) {
        value = random::Rnd(-10, 10);
        MG_CHECK(value >= -10 && value <= 10);
    }
    random::seed(17);
    for (int value : forward) {
        MG_CHECK(random::Rnd(10, -10) == value);
    }
    bool positive = false;
    bool negative = false;
    for (int i = 0; i < 512; ++i) {
        MG_CHECK(random::Rnd(-7, -7) == -7);
        const int value = random::Rnd(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        positive = positive || value > 0;
        negative = negative || value < 0;
    }
    MG_CHECK(positive && negative);
}

void index_bounds() {
    random::seed(17);
    std::array<bool, 7> visited{};
    for (int i = 0; i < 512; ++i) {
        const int index = IRND(7);
        MG_CHECK(index >= 0 && index < 7);
        visited[index] = true;
        MG_CHECK(IRND(1) == 0);
        MG_CHECK(IRND(0) == 0);
        MG_CHECK(IRND(-1) == 0);
    }
    for (bool present : visited) {
        MG_CHECK(present);
    }
}

void shared_stream() {
    random::seed(9);
    random::Rnd();
    const int expected = random::Rnd();
    random::seed(9);
    FRND(0.125);
    MG_CHECK(random::Rnd() == expected);
    random::seed(9);
    MG_CHECK(random::Rnd(4, 4) == 4);
    MG_CHECK(random::Rnd() == expected);
    random::seed(9);
    MG_CHECK(random::RndFloat(4.5, 4.5) == 4.5);
    MG_CHECK(random::Rnd() == expected);
}

void legacy_sequence() {
    constexpr std::array<int, 10> expected{41, 18467, 6334, 26500, 19169, 15724, 11478, 29358, 26962, 24464};
    random::seed(1, random::Mode::LegacyCRT);
    for (int value : expected) MG_CHECK(random::Rnd() == value);
    random::seed(1);
    MG_CHECK(random::mode() == random::Mode::LegacyCRT);
    for (int value : expected) MG_CHECK(random::Rnd() == value);
    random::seed(1, random::Mode::ParkMiller);
    MG_CHECK(random::Rnd() == 16806);
}

void legacy_seeds() {
    random::seed(0, random::Mode::LegacyCRT);
    MG_CHECK(random::Rnd() == 38);
    random::seed(std::numeric_limits<unsigned int>::max());
    MG_CHECK(random::Rnd() == 35);
    random::seed(1);
    MG_CHECK(random::Rnd() == 41);
}

void legacy_bounds() {
    random::seed(1, random::Mode::LegacyCRT);
    unit_interval();
    fractional_scales();
    integer_bounds();
    index_bounds();
    shared_stream();
    MG_CHECK(random::mode() == random::Mode::LegacyCRT);
}

void legacy_floating_bounds() {
    random::seed(2708534849U, random::Mode::LegacyCRT);
    MG_CHECK(random::RndFloat() == 0.0);
    random::seed(4028364353U);
    MG_CHECK(random::RndFloat() == 1.0);
    random::seed(2708534849U);
    MG_CHECK(RND(-1.25, -3.75) == -3.75);
    random::seed(4028364353U);
    MG_CHECK(FRND(0.125) == 0.125f);
    random::seed(2708534849U);
    MG_CHECK(FSRND(-0.125) == -0.125f);
    for (int i = 0; i < 512; ++i) {
        MG_CHECK(random::RndFloat(7.25, 7.25) == 7.25);
        const double value = RND(-1.25, -3.75);
        MG_CHECK(value >= -3.75 && value <= -1.25);
        MG_CHECK(std::isfinite(random::RndFloat(-std::numeric_limits<double>::max(),
                                               std::numeric_limits<double>::max())));
        const float fraction = FRND(0.125);
        MG_CHECK(fraction >= 0 && fraction <= 0.125f);
    }
}

void legacy_width_consumption() {
    random::seed(1, random::Mode::LegacyCRT);
    MG_CHECK(random::Rnd(0, 32767) == 41);
    MG_CHECK(random::Rnd() == 18467);
    random::seed(1);
    MG_CHECK(random::Rnd(0, 32768) == 18426);
    MG_CHECK(random::Rnd() == 6334);
    random::seed(1);
    MG_CHECK(random::Rnd(std::numeric_limits<int>::max(), std::numeric_limits<int>::min()) == -468608834);
    MG_CHECK(random::Rnd() == 26500);
    for (int count : {0, -1, std::numeric_limits<int>::min(), 1}) {
        random::seed(1);
        MG_CHECK(IRND(count) == 0);
        MG_CHECK(random::Rnd() == 18467);
    }
}

void legacy_index_mapping() {
    static_assert(RAND_MAX == 32767, "The supported Windows CRT uses 15-bit output.");
    std::array<int, 4> counts{};
    for (uint32_t draw = 0; draw <= 32767; ++draw) {
        // Choose the CRT state with this next raw output; enumerate values, not samples.
        const uint32_t seed = ((draw << 16) - 2531011U) * 3115528533U;
        random::seed(seed, random::Mode::LegacyCRT);
        const int index = IRND(4);
        MG_CHECK(index >= 0 && index < 4);
        ++counts[index];
    }
    MG_CHECK((counts == std::array<int, 4>{5462, 10922, 10922, 5462}));
    random::seed(2708534849U);
    MG_CHECK(IRND(std::numeric_limits<int>::max()) == 0);
    random::seed(4028364353U);
    MG_CHECK(IRND(std::numeric_limits<int>::max()) == std::numeric_limits<int>::max() - 1);
}

void legacy_crt_interop() {
    random::seed(1, random::Mode::LegacyCRT);
    MG_CHECK(std::rand() == 41);
    MG_CHECK(random::Rnd() == 18467);
    std::srand(0);
    MG_CHECK(random::Rnd() == 38);
    MG_CHECK(random::mode() == random::Mode::LegacyCRT);
}

constexpr tests::Case cases[] = {
    {"base.random.unit_interval", unit_interval},
    {"base.random.fractional_scales", fractional_scales},
    {"base.random.original_sequence", original_sequence},
    {"base.random.seed_normalization", seed_normalization},
    {"base.random.floating_bounds", floating_bounds},
    {"base.random.integer_bounds", integer_bounds},
    {"base.random.index_bounds", index_bounds},
    {"base.random.shared_stream", shared_stream},
    {"base.random.legacy_sequence", legacy_sequence},
    {"base.random.legacy_seeds", legacy_seeds},
    {"base.random.legacy_bounds", legacy_bounds},
    {"base.random.legacy_floating_bounds", legacy_floating_bounds},
    {"base.random.legacy_width_consumption", legacy_width_consumption},
    {"base.random.legacy_index_mapping", legacy_index_mapping},
    {"base.random.legacy_crt_interop", legacy_crt_interop},
};

}  // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
