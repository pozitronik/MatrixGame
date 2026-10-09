// MatrixGame - licensed under GPLv2 or any later version.
#include "test_support.hpp"
#include "RandomConfiguration.hpp"
#include "MatrixMap.hpp"
#include "Helper.hpp"

#include <memory>
#include <cmath>
#include <limits>

namespace {
struct MapDeleter {
    void operator()(CMatrixMapLogic *map) const { HDelete(CMatrixMapLogic, map, g_MatrixHeap); }
};

struct World {
    Base::CHeap heap;
    Base::CBlockPar data;
    std::unique_ptr<CMatrixMapLogic, MapDeleter> map;
    World() {
        g_MatrixHeap = &heap;
        g_MatrixData = &data;
        CacheInit();
    }
    ~World() {
        map.reset();
#ifdef _DEBUG
        CHelper::ClearAll();
#endif
        CacheDeinit();
        g_MatrixMap = nullptr;
        g_MatrixData = nullptr;
        g_MatrixHeap = nullptr;
    }
    void create_map() {
        map.reset(HNew(g_MatrixHeap) CMatrixMapLogic);
        g_MatrixMap = map.get();
    }
};

void shared_map_helpers() {
    for (const auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        World world;
        Base::CBlockPar options;
        options.ParAdd(L"RandomGenerator", mode == random::Mode::LegacyCRT ? L"LegacyCRT" : L"ParkMiller");
        MG_CHECK(RandomConfiguration::initialize(1, options).valid);
        world.create_map();
        // The production map constructor consumes the first integer draw.
        const int second = mode == random::Mode::LegacyCRT ? 18467 : 282475248;
        MG_CHECK(world.map->Rnd() == second);
        MG_CHECK(random::mode() == mode && g_D3DD == nullptr);
        random::seed(9);
        const int first = random::Rnd();
        const int next = random::Rnd();
        random::seed(9);
        MG_CHECK(world.map->Rnd() == first);
        MG_CHECK(random::Rnd() == next);
        random::seed(9);
        const double fraction = random::RndFloat();
        random::seed(9);
        MG_CHECK(std::abs(world.map->RndFloat() - fraction) <= 2 * std::numeric_limits<double>::epsilon());
        MG_CHECK(random::Rnd() == next);
        random::seed(9);
        const double ranged = RND(-0.125, 0.125);
        random::seed(9);
        MG_CHECK(std::abs(world.map->RndFloat(-0.125, 0.125) - ranged) <= 2 * std::numeric_limits<double>::epsilon());
        MG_CHECK(random::Rnd() == next);
        random::seed(9);
        const int integer = random::Rnd(-10, 10);
        random::seed(9);
        MG_CHECK(world.map->Rnd(-10, 10) == integer);
        MG_CHECK(random::Rnd() == next);
    }
}

constexpr tests::Case cases[] = {{"game.random.shared_map_helpers", shared_map_helpers}};
} // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block) {
        std::cerr << "Random map test leaked tracked heap allocations.\n";
        return EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
