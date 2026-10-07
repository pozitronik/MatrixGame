// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "random.hpp"

#include <array>
#include <cmath>
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

constexpr tests::Case cases[] = {
    {"base.random.unit_interval", unit_interval},
    {"base.random.fractional_scales", fractional_scales},
    {"base.random.original_sequence", original_sequence},
    {"base.random.seed_normalization", seed_normalization},
    {"base.random.floating_bounds", floating_bounds},
    {"base.random.integer_bounds", integer_bounds},
    {"base.random.index_bounds", index_bounds},
    {"base.random.shared_stream", shared_stream},
};

}  // namespace

int main(int argc, char **argv) { return tests::run(argc, argv, cases); }
