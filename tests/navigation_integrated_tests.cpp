// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include <array>
#include <cmath>

namespace tests::navigation {
namespace {
using namespace battle;

Profile profile(random::Mode mode, int step, NavigationLayout layout = NavigationLayout::MultiRegion) {
    Profile result;
    result.mode = mode;
    result.step_ms = step;
    result.max_frames = 6000;
    result.layout = layout;
    return result;
}

struct Movement {
    std::array<CMatrixRobotAI *, 2> robots;
    std::array<size_t, 2> identities;
    std::array<unsigned, 2> zones{};
    std::array<unsigned, 2> regions{};
    unsigned collision_weight_samples = 0;
    unsigned conservative_footprint_samples = 0;

    Movement() {
        for (int i = 0; i < 2; ++i) {
            robots[i] = g_MatrixMap->GetPlayerSide()->GetCurGroup()->GetObjectByN(i)->AsRobot();
            identities[i] = robots[i]->UID;
        }
        observe();
    }
    void observe() {
        std::array<bool, 2> live_selected{};
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            if (!object->IsLiveRobot()) continue;
            const auto *robot = object->AsRobot();
            for (int i = 0; i < 2; ++i) {
                if (robot != robots[i]) continue;
                MG_CHECK(robot->UID == identities[i]); // Reject reuse of a retired unit's address.
                live_selected[i] = true;
            }
            if (!std::isfinite(robot->m_GroupSpeed))
                std::cout << "nonfinite group speed side=" << robot->GetSide() << " time=" << g_MatrixMap->GetTime()
                          << " pos=" << robot->m_PosX << ',' << robot->m_PosY << '\n';
            MG_CHECK(std::isfinite(robot->m_GroupSpeed));
            MG_CHECK(std::isfinite(robot->m_PosX) && std::isfinite(robot->m_PosY));
        }
        for (int i = 0; i < 2; ++i) {
            MG_CHECK(live_selected[i]); // Prove membership and identity before dereferencing retained pointers.
            auto &robot = *robots[i];
            MG_CHECK(robot.IsLiveRobot() && !robot.IsMustDie());
            MG_CHECK(std::isfinite(robot.m_PosX) && std::isfinite(robot.m_PosY));
            MG_CHECK(robot.m_PosX >= 20 && robot.m_PosX <= 620 && robot.m_PosY >= 20 && robot.m_PosY <= 620);
            const int x = robot.GetMapPosX(), y = robot.GetMapPosY();
            // Movement uses circular collision correction; its rounded position need
            // not retain the conservative square footprint used to search the path.
            if (!g_MatrixMap->IsAbsenceWall(RUK_CHASSIS_WHEEL - 1, 4, x, y)) ++conservative_footprint_samples;
            MG_CHECK(g_MatrixMap->IsAbsenceWall(RUK_CHASSIS_WHEEL - 1, 1,
                static_cast<int>(robot.m_PosX / GLOBAL_SCALE_MOVE), static_cast<int>(robot.m_PosY / GLOBAL_SCALE_MOVE)));
            const auto *cell = g_MatrixMap->MoveGet(x, y);
            MG_CHECK(cell->m_Zone >= 0 && cell->m_Zone < g_MatrixMap->m_RN.m_ZoneCnt);
            const int region = robot.GetRegion();
            MG_CHECK(region >= 0 && region < g_MatrixMap->m_RN.m_RegionCnt);
            zones[i] |= 1u << cell->m_Zone;
            regions[i] |= 1u << region;
            if (robot.GetColsWeight()) ++collision_weight_samples;
        }
    }
    void advance(Session &session, int duration, int step) {
        for (int elapsed = 0; elapsed < duration; elapsed += step) {
            try { session.advance(); }
            catch (const Base::CException &error) {
                std::wcerr << error.Info() << '\n';
                std::cerr << session.snapshot() << '\n';
                throw;
            }
            observe();
        }
        session.check_running();
    }
    void report(const Session &session) const {
        std::cout << session.metadata() << '\n' << session.snapshot() << '\n';
        std::cout << "{\"frames\":" << session.frame() << ",\"visited_zone_bits\":[" << zones[0] << ',' << zones[1]
                  << "],\"visited_region_bits\":[" << regions[0] << ',' << regions[1]
                  << "],\"unit_collision_weight_samples\":" << collision_weight_samples
                  << ",\"conservative_footprint_samples\":" << conservative_footprint_samples << "}\n";
    }
};

void arrived(const Session &session, float x, float y) {
    const auto first = session.robot(1), second = session.robot(2);
    MG_CHECK(first.in_place && second.in_place);
    MG_CHECK(first.x == x && first.y == y);
    MG_CHECK(std::hypot(first.x - second.x, first.y - second.y) >= 20);
}
}

void multiregion_movement() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session(profile(mode, step));
        Movement movement;
        MG_CHECK(g_MatrixMap->m_RN.m_ZoneCnt == 4 && g_MatrixMap->m_RN.m_RegionCnt == 2);
        MG_CHECK(!g_MatrixMap->CanMoveFromTo(RUK_CHASSIS_WHEEL - 1, 4, 8, 18, 48, 48, nullptr));
        session.ground_command(500, 500);
        movement.advance(session, 15000, step);
        movement.report(session);
        arrived(session, 500, 500);
        for (int i = 0; i < 2; ++i) {
            MG_CHECK(movement.regions[i] == 3);
            MG_CHECK((movement.zones[i] & 9) == 9); // Started upper-left and arrived lower-right.
            MG_CHECK(movement.zones[i] != 9); // At least one intermediate zone was crossed.
        }
        session.ground_command(120, 400);
        movement.advance(session, 15000, step);
        arrived(session, 120, 400);
        MG_CHECK(movement.robots[0]->GetRegion() == 0 && movement.robots[1]->GetRegion() == 0);
        movement.report(session);
    }
}

void multiregion_road_guidance() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session(profile(mode, step));
        Movement movement;
        session.ground_command(500, 500);
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto &group = side.m_PlayerGroup[movement.robots[0]->GetGroupLogic()];
        int regions[] = {0, 1};
        g_MatrixMap->m_RN.FindPathFromRegionPath(1 << (RUK_CHASSIS_WHEEL - 1), 2, regions, group.m_RoadPath);
        MG_CHECK(group.m_RoadPath->m_ListCnt == 1);
        MG_CHECK(group.m_RoadPath->m_Header[0].m_Cnt == 2 && group.m_RoadPath->m_Header[0].m_Dist == 40);
        const auto *road = group.m_RoadPath->m_Units[1].m_Road;
        MG_CHECK(road && road->m_ZoneCnt == 2 && road->m_PathCnt == 2);
        MG_CHECK(road->m_Zone[0] == 0 && road->m_Zone[1] == 1);
        // A real region route is supplied as initial group state. Robot ticks choose
        // the route-aware ZonePathCalc branch while crossing non-adjacent zones 0 and 3.
        movement.advance(session, 15000, step);
        arrived(session, 500, 500);
        MG_CHECK(group.m_RoadPath->m_ListCnt == 1 && group.m_RoadPath->m_Units[1].m_Road == road);
        MG_CHECK(movement.regions[0] == 3 && movement.regions[1] == 3);
        for (int i = 0; i < 4; ++i) MG_CHECK(!g_MatrixMap->m_RN.m_Zone[i].m_Access);
        movement.report(session);
    }
}

// Manual correctness probe, intentionally absent from CTest baseline registration.
// It must fail at nonfinite state, never accept that state as a gameplay reference.
void group_speed_nonfinite_probe() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session(profile(mode, step));
        std::cout << session.metadata() << '\n';
        Movement movement;
        session.ground_command(500, 500);
        movement.advance(session, 400, step);
        MG_CHECK(!session.robot(1).in_place);
        MG_CHECK(session.robot(1).x != 100 || session.robot(1).y != 200);
        session.ground_command(120, 400);
        movement.advance(session, 15000, step);
        arrived(session, 120, 400);
        MG_CHECK(movement.robots[0]->GetRegion() == 0);
        session.ground_command(500, 100);
        movement.advance(session, 15000, step);
        arrived(session, 500, 100);
        MG_CHECK(movement.robots[0]->GetRegion() == 1 && movement.robots[1]->GetRegion() == 1);
        movement.report(session);
    }
}

void unreachable_replacement() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session(profile(mode, step, NavigationLayout::Disconnected));
        Movement movement;
        session.ground_command(500, 300);
        movement.advance(session, 3000, step);
        MG_CHECK(!session.robot(1).in_place && !session.robot(2).in_place);
        MG_CHECK(session.robot(1).x == 100 && session.robot(1).y == 200);
        session.ground_command(120, 400);
        movement.advance(session, 15000, step);
        arrived(session, 120, 400);
        MG_CHECK(movement.regions[0] == 1 && movement.regions[1] == 1);
        movement.report(session);
    }
}
}
