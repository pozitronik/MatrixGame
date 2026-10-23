// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "NavigationWork.hpp"
#include "MatrixRobot.hpp"
#include "navigation_work_scene.hpp"

#include <array>
#include <vector>

namespace {
using namespace tests::battle;
using NavigationWork::Counters;
using NavigationWork::Capture;
constexpr std::array fields{
    &Counters::local_searches, &Counters::local_cells_initialized, &Counters::local_queue_entries,
    &Counters::local_failures, &Counters::zone_searches, &Counters::robot_collision_queries,
    &Counters::robot_collision_candidates, &Counters::terrain_collision_queries,
    &Counters::terrain_mask_cells, &Counters::terrain_geometry_tests};

struct UnwindProbe {};

void scopes() {
    MG_CHECK(NavigationWork::active() == nullptr);
    Counters outer{}, inner{};
    outer.local_searches = 17;
    {
        Capture capture(outer);
        MG_CHECK(NavigationWork::active() == &outer && outer.local_searches == 17);
        try {
            Capture nested(inner);
            MG_CHECK(NavigationWork::active() == &inner && inner == Counters{});
            throw UnwindProbe{};
        }
        catch (const UnwindProbe &) {}
        MG_CHECK(NavigationWork::active() == &outer && outer.local_searches == 17);
    }
    MG_CHECK(NavigationWork::active() == nullptr);
}

void search_work() {
    Profile profile;
    profile.audio = false;
    Session session(profile);
    auto &map = *g_MatrixMap;
    CPoint path[MatrixPathMoveMax];
    int zone = 0;
    Counters work{};
    const auto search = [&](int x, int y) {
        return map.FindLocalPath(RUK_CHASSIS_WHEEL - 1, 4, 8, 8, &zone, 1, x, y, path,
                                 0, nullptr, nullptr, nullptr, nullptr, false);
    };
    MG_CHECK(search(48, 8) == 41 && work == Counters{});
    {
        Capture capture(work);
        MG_CHECK(search(48, 8) == 41);
        MG_CHECK(work.local_searches == 1 && work.local_cells_initialized == 3600);
        // Pin queued entries, including relaxed re-entries, rather than processed cells.
        MG_CHECK(work.local_queue_entries == 1943 && work.local_failures == 0);
        // The target is outside this zone's legal footprint search rectangle.
        MG_CHECK(search(62, 62) == 0);
        MG_CHECK(work.local_searches == 2 && work.local_cells_initialized == 7200);
        MG_CHECK(work.local_queue_entries == 4895 && work.local_failures == 1);
        std::cout << "search route_queue_entries=1943 accumulated_queue_entries="
                  << work.local_queue_entries << std::endl;
        int zones[2]{};
        MG_CHECK(map.FindPathInZone(RUK_CHASSIS_WHEEL - 1, 0, 0, nullptr, 0, zones, false) == 0);
        MG_CHECK(work.zone_searches == 1);
    }
    const auto final = work;
    MG_CHECK(search(48, 8) == 41 && work == final);
    MG_CHECK(NavigationWork::active() == nullptr);
}

void avoidance_work() {
    Profile profile;
    profile.audio = false;
    Session session(profile);
    auto *first = g_MatrixMap->GetPlayerSide()->GetCurGroup()->GetObjectByN(0)->AsRobot();
    auto *second = g_MatrixMap->GetPlayerSide()->GetCurGroup()->GetObjectByN(1)->AsRobot();
    second->m_PosX = 130;
    second->m_PosY = 200;
    second->MapPosCalc();
    second->RChange(MR_Matrix);
    second->RNeed(MR_Matrix);
    second->JoinToGroup();
    Counters work{};
    {
        Capture capture(work);
        first->RobotToObjectCollision(D3DXVECTOR3(0, 0, 0), 10);
        MG_CHECK(work.robot_collision_queries == 1 && work.robot_collision_candidates == 1);
        // Sample non-free terrain cells next to the original central obstacle.
        first->m_PosX = 270;
        first->m_PosY = 300;
        first->MapPosCalc();
        first->RChange(MR_Matrix);
        first->RNeed(MR_Matrix);
        first->JoinToGroup();
        D3DXVECTOR3 correction(0, 0, 0);
        first->SphereRobotToAABBObstacleCollision(correction, D3DXVECTOR3(0, 0, 0));
        MG_CHECK(work.terrain_collision_queries == 1 && work.terrain_mask_cells == 72);
        // Free cells skipped by the four sweeps must not count as geometry tests.
        MG_CHECK(work.terrain_geometry_tests == 30);
        std::cout << "avoidance robot_candidates=" << work.robot_collision_candidates
                  << " mask_cells=" << work.terrain_mask_cells
                  << " geometry_tests=" << work.terrain_geometry_tests << std::endl;
    }
    MG_CHECK(NavigationWork::active() == nullptr);
}

struct Journey {
    std::vector<std::string> snapshots;
    std::vector<Counters> frames;
    Counters total;
};

Journey journey(random::Mode mode, int step, bool collect) {
    MG_CHECK(NavigationWork::active() == nullptr);
    Profile profile{mode, 79, step, 15000 / step, false, true};
    Session session(profile);
    std::cout << session.metadata("navigation-work-preservation-v1") << "\ncollect=" << collect << std::endl;
    Journey result;
    session.ground_command(500, 300);
    result.snapshots.push_back(session.snapshot());
    while (session.frame() < profile.max_frames) {
        Counters frame{};
        if (collect) {
            Capture capture(frame);
            session.advance();
        }
        else session.advance();
        MG_CHECK(NavigationWork::active() == nullptr);
        result.frames.push_back(frame);
        for (auto field : fields) result.total.*field += frame.*field;
        result.snapshots.push_back(session.snapshot());
    }
    for (int id : {1, 2}) {
        const auto robot = session.robot(id);
        MG_CHECK(robot.alive && robot.in_place && robot.x > 400);
    }
    std::cout << "total=";
    for (auto field : fields) std::cout << result.total.*field << ',';
    std::cout << '\n';
    return result;
}

template<random::Mode Mode, int Step>
void preservation() {
    const auto inactive = journey(Mode, Step, false);
    const auto collected = journey(Mode, Step, true);
    const auto repeated = journey(Mode, Step, true);
    MG_CHECK(inactive.snapshots == collected.snapshots && collected.snapshots == repeated.snapshots);
    MG_CHECK(collected.frames == repeated.frames && collected.total == repeated.total);
    const auto &work = collected.total;
    MG_CHECK(work.local_searches > 0 && work.local_cells_initialized > 0 && work.local_queue_entries > 0);
    MG_CHECK(work.zone_searches > 0 && work.robot_collision_queries > 0 && work.robot_collision_candidates > 0);
    MG_CHECK(work.terrain_collision_queries > 0 && work.terrain_mask_cells > 0 && work.terrain_geometry_tests > 0);
}

template<tests::navigation_work::Kind Kind, random::Mode Mode, int Step>
void crowded_preservation() {
    using namespace tests::navigation_work;
    const Options options{Kind, Mode, Step};
    const auto execute = [&](bool collect) {
        Scene scene(options);
        std::cout << scene.metadata() << "\ncollect=" << collect << std::endl;
        auto result = scene.run(collect, true);
        print(result);
        return result;
    };
    const auto inactive = execute(false);
    const auto collected = execute(true);
    const auto repeated = execute(true);
    MG_CHECK(inactive.states == collected.states && collected.states == repeated.states);
    MG_CHECK(inactive.events == collected.events && collected.events == repeated.events);
    MG_CHECK(collected.frames == repeated.frames && collected.total == repeated.total);
    MG_CHECK(collected.intervals == repeated.intervals);
    MG_CHECK(collected.frames.size() == static_cast<size_t>(collected.events.frames));
    const auto &work = collected.total;
    MG_CHECK(work.local_searches > 0 && work.local_cells_initialized > 0 && work.local_queue_entries > 0);
    MG_CHECK(work.zone_searches > 0 && work.robot_collision_queries > 0 && work.robot_collision_candidates > 0);
    MG_CHECK(work.terrain_collision_queries > 0 && work.terrain_mask_cells > 0);
    MG_CHECK(work.local_failures > 0);
    if constexpr (Kind == tests::navigation_work::Kind::CrowdedDestination) MG_CHECK(work.terrain_geometry_tests > 0);
    MG_CHECK(collected.events.final_rng_draws == collected.events.initial_rng_draws);

    // Reconstruct the documented intervals from frame indices, independently of the scene's time dispatch.
    constexpr std::array<size_t, 3> ends{4000 / Step, 12000 / Step, 32000 / Step};
    constexpr std::array<size_t, 3> populations = Step == 10
        ? std::array<size_t, 3>{400, 800, 2000} : std::array<size_t, 3>{250, 500, 1250};
    MG_CHECK(collected.frames.size() == ends.back());
    size_t begin = 0;
    for (size_t interval = 0; interval < ends.size(); ++interval) {
        Counters expected{};
        MG_CHECK(ends[interval] - begin == populations[interval]);
        for (size_t frame = begin; frame < ends[interval]; ++frame)
            for (auto field : fields) expected.*field += collected.frames[frame].*field;
        MG_CHECK(expected == collected.intervals[interval]);
        begin = ends[interval];
    }
#ifdef __MINGW32__
    // Measured x86 MinGW totals; other compilers retain within-profile preservation and repeat checks.
    constexpr Counters expected = [] {
        if constexpr (Kind == tests::navigation_work::Kind::CrowdedDestination) {
            if constexpr (Step == 10) return Counters{86, 309600, 276385, 5, 86, 16998, 17823, 16998, 611928, 51264};
            else return Counters{77, 277200, 247567, 4, 77, 21258, 22883, 21258, 765288, 65020};
        } else {
            if constexpr (Step == 10) return Counters{193, 694800, 692835, 74, 193, 14933, 16191, 14933, 537588, 0};
            else return Counters{179, 644400, 648120, 66, 179, 19154, 21963, 19154, 689544, 0};
        }
    }();
    MG_CHECK(work == expected);
#endif
    Counters sum{};
    for (const auto &frame : collected.frames)
        for (auto field : tests::navigation_work::fields) sum.*field += frame.*field;
    MG_CHECK(sum == work);
    sum = {};
    for (const auto &interval : collected.intervals)
        for (auto field : tests::navigation_work::fields) sum.*field += interval.*field;
    MG_CHECK(sum == work);
}

constexpr tests::Case cases[]{
    {"game.navigation.work_scopes", scopes},
    {"game.navigation.work_search", search_work},
    {"game.navigation.work_avoidance", avoidance_work},
    {"game.navigation.work_preservation.park_miller10", preservation<random::Mode::ParkMiller, 10>},
    {"game.navigation.work_preservation.park_miller16", preservation<random::Mode::ParkMiller, 16>},
    {"game.navigation.work_preservation.legacy10", preservation<random::Mode::LegacyCRT, 10>},
    {"game.navigation.work_preservation.legacy16", preservation<random::Mode::LegacyCRT, 16>},
    {"game.navigation.work_crowded.park_miller10",
     crowded_preservation<tests::navigation_work::Kind::CrowdedDestination, random::Mode::ParkMiller, 10>},
    {"game.navigation.work_crowded.park_miller16",
     crowded_preservation<tests::navigation_work::Kind::CrowdedDestination, random::Mode::ParkMiller, 16>},
    {"game.navigation.work_crowded.legacy10",
     crowded_preservation<tests::navigation_work::Kind::CrowdedDestination, random::Mode::LegacyCRT, 10>},
    {"game.navigation.work_crowded.legacy16",
     crowded_preservation<tests::navigation_work::Kind::CrowdedDestination, random::Mode::LegacyCRT, 16>},
    {"game.navigation.work_opposing.park_miller10",
     crowded_preservation<tests::navigation_work::Kind::OpposingFlow, random::Mode::ParkMiller, 10>},
    {"game.navigation.work_opposing.park_miller16",
     crowded_preservation<tests::navigation_work::Kind::OpposingFlow, random::Mode::ParkMiller, 16>},
    {"game.navigation.work_opposing.legacy10",
     crowded_preservation<tests::navigation_work::Kind::OpposingFlow, random::Mode::LegacyCRT, 10>},
    {"game.navigation.work_opposing.legacy16",
     crowded_preservation<tests::navigation_work::Kind::OpposingFlow, random::Mode::LegacyCRT, 16>},
};
}  // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) return EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
