// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "command_world.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include <array>
#include <cmath>
#include <iostream>

namespace {
using namespace tests;
using namespace tests::battle;
void zone_routes() {
    CommandWorld world;
    auto &map = *world.map;
    auto &road = map.m_RN;
    road.SetZoneCntMax(5);
    road.m_ZoneCnt = 5;
    for (int i = 0; i < 5; ++i) road.m_Zone[i] = {};
    auto link = [&](int a, int b) {
        road.m_Zone[a].m_NearZone[road.m_Zone[a].m_NearZoneCnt++] = b;
        road.m_Zone[b].m_NearZone[road.m_Zone[b].m_NearZoneCnt++] = a;
    };
    link(0, 1); link(1, 3); link(0, 2); link(2, 3);
    int path[8]{};
    auto find = [&](int chassis, int start, int end) {
        return map.FindPathInZone(chassis, start, end, nullptr, 0, path, false);
    };
    MG_CHECK(find(1, 0, 3) == 3);
    MG_CHECK(path[0] == 0 && path[1] == 1 && path[2] == 3);
    road.m_Zone[1].m_Move = 1 << 1;
    MG_CHECK(find(1, 0, 3) == 3 && path[1] == 2);
    MG_CHECK(find(2, 0, 3) == 3 && path[1] == 1);
    road.m_Zone[2].m_NearZoneMove[1] = 1 << 1;
    road.m_Zone[3].m_NearZoneMove[1] = 1 << 1;
    MG_CHECK(find(1, 0, 3) == 0);
    MG_CHECK(find(2, 0, 3) == 3);
    MG_CHECK(find(1, 0, 4) == 0);
    MG_CHECK(find(1, 0, 0) == 0);
    // Restore the initial graph and verify failed searches do not contaminate it.
    road.m_Zone[1].m_Move = 0;
    MG_CHECK(find(1, 0, 3) == 3 && path[1] == 1);
    for (int i = 0; i < 5; ++i) MG_CHECK(!road.m_Zone[i].m_Access);
}

void chassis_traversal() {
    Profile p; p.layout = NavigationLayout::Open;
    Session session(p);
    auto &map = *g_MatrixMap;
    // A synthetic terrain restriction applies independently to every footprint.
    // This tests traversal masks, not generation of masks from water/slope assets.
    for (int blocked = 0; blocked < 5; ++blocked) {
        for (int y = 2; y < 62; ++y) for (int x = 25; x < 36; ++x) {
            auto &cell = *map.MoveGet(x, y);
            cell.m_Stop = 0;
            for (int size = 1; size <= 5; ++size) cell.m_Stop |= DWORD{1} << (blocked + 6 * (size - 1));
        }
        for (int chassis = 0; chassis < 5; ++chassis) {
            std::array<CPoint, MatrixPathMoveMax> path;
            int zone = 0;
            const int count = map.FindLocalPath(chassis, 4, 8, 28, &zone, 1, 48, 28,
                path.data(), 0, nullptr, nullptr, nullptr, nullptr, false);
            MG_CHECK((count == 0) == (chassis == blocked));
            MG_CHECK(map.CanMoveFromTo(chassis, 4, 8, 28, 48, 28, nullptr) == (chassis != blocked));
            if (count) MG_CHECK(count == 41 && path[0] == CPoint(8, 28) && path[count - 1] == CPoint(48, 28));
        }
    }
}
void road_routes() {
    CHeap heap;
    CMatrixRoadNetwork road(&heap);
    std::array<CMatrixCrotch *, 5> nodes;
    const int regions[] = {0, -1, 2, 1, 3};
    for (int i = 0; i < 4; ++i) {
        const int region = road.AddRegion();
        road.m_Region[region] = {};
    }
    for (int i = 0; i < 5; ++i) {
        nodes[i] = road.AddCrotch();
        nodes[i]->m_Region = regions[i];
        if (regions[i] >= 0) {
            auto &region = road.m_Region[regions[i]];
            region.m_Crotch[region.m_CrotchCnt++] = nodes[i];
        }
    }
    auto link = [&](int a, int b, int length) {
        auto *edge = road.AddRoad();
        edge->m_Start = nodes[a]; edge->m_End = nodes[b]; edge->m_Dist = length;
        nodes[a]->AddRoad(edge); nodes[b]->AddRoad(edge);
        return edge;
    };
    auto *first = link(0, 1, 10);
    link(1, 3, 20);
    auto *alternative = link(0, 2, 1);
    link(2, 3, 1);
    CMatrixRoadRoute route(&road);
    int region_path[] = {0, 1};
    auto find = [&] {
        route.Clear();
        road.FindPathFromRegionPath(1 << 1, 2, region_path, &route);
        MG_CHECK(route.m_ListCnt == 1 && route.m_Header[0].m_Cnt == 3);
        MG_CHECK(route.m_Units[0].m_Crotch == nodes[0] && route.m_Units[2].m_Crotch == nodes[3]);
    };
    find();
    MG_CHECK(route.m_Units[1].m_Crotch == nodes[1] && route.m_Header[0].m_Dist == 30);
    first->m_Move = 1 << 1;
    find(); // The second search pass permits crossing another region.
    MG_CHECK(route.m_Units[1].m_Crotch == nodes[2] && route.m_Header[0].m_Dist == 2);
    alternative->m_Move = 1 << 1;
    find(); // Last-resort road search ignores movement masks; local search still owns traversability.
    MG_CHECK(route.m_Units[1].m_Crotch == nodes[1] && route.m_Header[0].m_Dist == 30);
    route.Clear();
    region_path[1] = 3;
    road.FindPathFromRegionPath(1 << 1, 2, region_path, &route);
    MG_CHECK(route.m_ListCnt == 0 && route.m_Header == nullptr);
}

// Player and AI groups reuse route storage through ClearFast. Each rebuilt route must match a fresh one.
void road_route_reuse() {
    CHeap heap;
    CMatrixRoadNetwork road(&heap);
    std::array<CMatrixCrotch *, 5> nodes;
    const int regions[] = {0, -1, 2, 1, 3};
    for (int i = 0; i < 4; ++i) {
        const int region = road.AddRegion();
        road.m_Region[region] = {};
    }
    for (int i = 0; i < 5; ++i) {
        nodes[i] = road.AddCrotch();
        nodes[i]->m_Region = regions[i];
        if (regions[i] >= 0) {
            auto &region = road.m_Region[regions[i]];
            region.m_Crotch[region.m_CrotchCnt++] = nodes[i];
        }
    }
    auto link = [&](int a, int b, int length) {
        auto *edge = road.AddRoad();
        edge->m_Start = nodes[a]; edge->m_End = nodes[b]; edge->m_Dist = length;
        nodes[a]->AddRoad(edge); nodes[b]->AddRoad(edge);
        return edge;
    };
    auto *first = link(0, 1, 10);
    link(1, 3, 20);
    auto *alternative = link(0, 2, 1);
    link(2, 3, 1);
    CMatrixRoadRoute fresh(&road), reused(&road);
    int region_path[] = {0, 1};
    auto search = [&](int count, int distance) {
        fresh.Clear();
        road.FindPathFromRegionPath(1 << 1, count, region_path, &fresh);
        reused.ClearFast();
        road.FindPathFromRegionPath(1 << 1, count, region_path, &reused);
        MG_CHECK(reused.m_ListCnt == fresh.m_ListCnt);
        for (int list = 0; list < fresh.m_ListCnt; ++list) {
            MG_CHECK(reused.m_Header[list].m_Cnt == fresh.m_Header[list].m_Cnt);
            MG_CHECK(reused.m_Header[list].m_Dist == fresh.m_Header[list].m_Dist);
            for (int i = 0; i < fresh.m_Header[list].m_Cnt; ++i) {
                const auto &expected = fresh.m_Units[list * road.m_CrotchCnt + i];
                const auto &actual = reused.m_Units[list * road.m_CrotchCnt + i];
                MG_CHECK(actual.m_Road == expected.m_Road && actual.m_Crotch == expected.m_Crotch);
            }
        }
        if (distance < 0) {
            MG_CHECK(fresh.m_ListCnt == 0);
        }
        else {
            MG_CHECK(fresh.m_ListCnt == 1 && fresh.m_Header[0].m_Dist == distance);
        }
    };
    for (int cycle = 0; cycle < 3; ++cycle) {
        first->m_Move = 0;
        alternative->m_Move = 0;
        search(2, 30);
        first->m_Move = 1 << 1;
        search(2, 2);
        alternative->m_Move = 1 << 1;
        search(2, 30);
    }
    first->m_Move = 0;
    alternative->m_Move = 0;
    search(2, 30);
    search(1, -1); // A single-region path leaves cleared storage empty.
    region_path[1] = 3;
    search(2, -1); // A disconnected destination releases the reused storage.
    region_path[1] = 1;
    search(2, 30);
    search(2, 30);
    // A three-region route into reused storage accumulates distance across both of its legs.
    int three[] = {0, 2, 1};
    reused.ClearFast();
    road.FindPathFromRegionPath(1 << 1, 3, three, &reused);
    MG_CHECK(reused.m_ListCnt == 1 && reused.m_Header[0].m_Cnt == 3 && reused.m_Header[0].m_Dist == 2);
}

// CalcMaxSpeed slows formation leaders that are more than 180 units (COLLIDE_BOT_R * 10) ahead.
void formation_speed_boundaries() {
    for (float spread : {0.0f, 100.0f, 179.0f, 180.0f, 181.0f, 360.0f}) for (bool mixed : {false, true}) {
        Profile p; p.layout = NavigationLayout::Open;
        Session session(p);
        session.ground_command(620, 200); // Forms the player group; exact orders follow.
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto *trailing = side.GetCurGroup()->GetObjectByN(0)->AsRobot();
        auto *leading = side.GetCurGroup()->GetObjectByN(1)->AsRobot();
        const int group = trailing->GetGroupLogic();
        MG_CHECK(group >= 0 && leading->GetGroupLogic() == group && side.m_PlayerGroup[group].m_RobotCnt > 0);
        trailing->SetMaxSpeed(0.6f);
        leading->SetMaxSpeed(mixed ? 0.4f : 0.6f);
        // Initial placement only. Destination cell (60,18) is world (620,200), so the formation
        // direction is exactly +X and the projections differ by exactly the spread.
        trailing->m_PosX = 100; trailing->m_PosY = 200; trailing->MapPosCalc();
        leading->m_PosX = 100 + spread; leading->m_PosY = 200; leading->MapPosCalc();
        trailing->MoveTo(60, 18);
        leading->MoveTo(60, 18);
        MG_CHECK(trailing->GetEnv()->GetEnemyCnt() == 0 && leading->GetEnv()->GetEnemyCnt() == 0);
        side.CalcMaxSpeed();
        MG_CHECK(std::isfinite(trailing->m_GroupSpeed) && std::isfinite(leading->m_GroupSpeed));
        MG_CHECK(trailing->m_GroupSpeed == trailing->GetMaxSpeed());
        // Above the threshold the leader is the farthest robot, so its interpolation factor is 0.6.
        const float factor = spread > 180 ? 0.6f : 1.0f;
        MG_CHECK(leading->m_GroupSpeed == leading->GetMaxSpeed() * factor);
        std::cout << "formation spread=" << spread << " mixed=" << mixed << " speeds=" << trailing->m_GroupSpeed
                  << ',' << leading->m_GroupSpeed << '\n';
    }
}

void collision_boundaries() {
    for (float distance : {35.0f, 36.0f, 37.0f, 71.0f, 72.0f, 73.0f}) {
        Profile p; p.layout = NavigationLayout::Open;
        Session session(p);
        MG_CHECK(session.remove(2));
        auto *moving = g_MatrixMap->GetPlayerSide()->GetCurGroup()->GetObjectByN(0)->AsRobot();
        // Velocity is one world unit. Distances refer to the predicted position.
        auto *ahead = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 101 + distance, 200);
        ahead->m_Velocity = D3DXVECTOR3(1, 0, 0);
        ahead->m_GroupSpeed = 0.6f;
        moving->m_GroupSpeed = 0.6f;
        const auto correction = moving->RobotToObjectCollision(D3DXVECTOR3(1, 0, 0), 10);
        MG_CHECK(correction.y == 0 && correction.z == 0);
        MG_CHECK(correction.x == (distance < 36 ? -1.0f : 0.0f));
        MG_CHECK(moving->GetCols() == (distance < 36 ? 1 : 0));
        MG_CHECK(moving->GetColsWeight() == (distance < 36 ? 20 : 0));
        MG_CHECK(ahead->GetColsWeight() == (distance < 36 ? 20 : 0));
        MG_CHECK(moving->m_ColSpeed == (distance < 72 ? 0.3f : 100.0f));
        MG_CHECK(moving->m_PosX == 100 && ahead->m_PosX == 101 + distance);
    }
}
void collision_wait_threshold() {
    for (int initial : {979, 980}) {
        Profile p; p.layout = NavigationLayout::Open;
        Session session(p);
        MG_CHECK(session.remove(2));
        auto *moving = g_MatrixMap->GetPlayerSide()->GetCurGroup()->GetObjectByN(0)->AsRobot();
        auto *blocker = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 136, 200);
        moving->SetColsWeight(initial);
        moving->RobotToObjectCollision(D3DXVECTOR3(1, 0, 0), 10);
        MG_CHECK(moving->GetColsWeight() == (initial == 979 ? 999 : 0));
        MG_CHECK(bool(blocker->FindOrderLikeThat(ROT_MOVE_RETURN)) == (initial == 980));
        if (initial == 980) {
            MG_CHECK(blocker->FindOrderLikeThat(ROT_MOVE_TO));
            CPoint destination;
            MG_CHECK(blocker->GetMoveToCoords(destination));
            MG_CHECK(destination != CPoint(blocker->GetMapPosX(), blocker->GetMapPosY()));
        }
    }
}

void moving_target() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Profile p; p.layout = NavigationLayout::Open; p.mode = mode;
        Session session(p);
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 500, 200);
        target->SetMaxSpeed(0.3f);
        const int pursuit = side.SelGroupToLogicGroup();
        side.PGOrderAttack(pursuit, CPoint(48, 18), target);
        side.PGOrderMoveTo(side.RobotToLogicGroup(target), CPoint(48, 38));
        auto *pursuer = side.GetCurGroup()->GetObjectByN(0)->AsRobot();
        CPoint old_place;
        MG_CHECK(pursuer->GetMoveToCoords(old_place));
        bool reassigned = false;
        for (int frame = 0; frame < 850; ++frame) {
            session.advance();
            MG_CHECK(side.m_PlayerGroup[pursuit].m_Obj == target);
            MG_CHECK(std::isfinite(target->m_PosX) && std::isfinite(target->m_PosY));
            CPoint place;
            if (pursuer->GetMoveToCoords(place) && place != old_place) reassigned = true;
        }
        std::cout << "moving target " << target->m_PosX << ',' << target->m_PosY
                  << " pursuer " << session.robot(1).x << ',' << session.robot(1).y << '\n';
        MG_CHECK(target->m_PosY > 250 && reassigned);
        MG_CHECK(session.robot(1).x > 100);
        g_MatrixMap->StaticDelete(target);
        for (int frame = 0; frame < 70; ++frame) session.advance();
        MG_CHECK(side.m_PlayerGroup[pursuit].m_Obj == nullptr);
        session.ground_command(120, 400);
        for (int frame = 0; frame < 1500; ++frame) session.advance();
        MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        MG_CHECK(session.robot(1).x == 120 && session.robot(1).y == 400);
    }
}
void replacement_near_blocker() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Profile p; p.layout = NavigationLayout::Open; p.mode = mode;
        Session session(p);
        auto *blocker = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 300, 220);
        blocker->SetMaxSpeed(0);
        blocker->m_GroupSpeed = 0;
        blocker->SetGroupLogic(-1);
        session.ground_command(500, 230);
        for (int frame = 0; frame < 300; ++frame) session.advance();
        MG_CHECK(session.robot(1).x > 200 && session.robot(1).x < 400);
        session.ground_command(120, 400);
        for (int frame = 0; frame < 1500; ++frame) {
            session.advance();
            MG_CHECK(std::isfinite(blocker->m_PosX) && std::isfinite(blocker->m_PosY));
        }
        MG_CHECK(blocker->GetMaxSpeed() == 0);
        MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        MG_CHECK(session.robot(1).x == 120 && session.robot(1).y == 400);
        MG_CHECK(session.robot(2).x == 80 && session.robot(2).y == 360);
    }
}

void dense_flow() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Profile p; p.layout = NavigationLayout::Open; p.mode = mode;
        Session session(p);
        auto &side = *g_MatrixMap->GetPlayerSide();
        std::array<CMatrixRobotAI *, 8> robots;
        robots[0] = side.GetCurGroup()->GetObjectByN(0)->AsRobot();
        robots[1] = side.GetCurGroup()->GetObjectByN(1)->AsRobot();
        for (int i = 2; i < 8; ++i)
            robots[i] = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE,
                i < 4 ? 100.0f : 540.0f, 200.0f + (i % 4) * 60);
        for (int i = 0; i < 8; ++i)
            side.PGOrderMoveTo(side.RobotToLogicGroup(robots[i]), CPoint(i < 4 ? 48 : 8, 18 + (i % 4) * 6));
        int collision_samples = 0;
        float minimum = 1000;
        for (int frame = 0; frame < 2500; ++frame) {
            session.advance();
            for (int i = 0; i < 8; ++i) {
                MG_CHECK(std::isfinite(robots[i]->m_PosX) && std::isfinite(robots[i]->m_PosY));
                if (robots[i]->GetCols()) ++collision_samples;
                for (int j = 0; j < i; ++j)
                    minimum = std::min(minimum, std::hypot(robots[i]->m_PosX - robots[j]->m_PosX,
                                                         robots[i]->m_PosY - robots[j]->m_PosY));
            }
        }
        std::cout << "dense collision_samples " << collision_samples << " minimum " << minimum << '\n';
        for (int i = 0; i < 8; ++i) {
            std::cout << "robot " << i << ' ' << robots[i]->m_PosX << ',' << robots[i]->m_PosY
                      << " in_place " << robots[i]->PLIsInPlace() << '\n';
            MG_CHECK(robots[i]->PLIsInPlace());
            MG_CHECK(i < 4 ? robots[i]->m_PosX > 400 : robots[i]->m_PosX < 200);
        }
        MG_CHECK(collision_samples > 0 && minimum > 0);
    }
}
void terrain_speed() {
    for (bool water : {false, true}) for (bool backwards : {false, true})
        for (float slope : {-0.5f, 0.0f, 0.25f, 0.5f, 0.75f}) {
            Profile p; p.layout = NavigationLayout::Open;
            Session session(p);
            auto *robot = g_MatrixMap->GetPlayerSide()->GetCurGroup()->GetObjectByN(0)->AsRobot();
            g_Config.m_ItemChars[CHASSIS2_MOVE_SPEED] = 0.5f;
            g_Config.m_ItemChars[CHASSIS2_MOVE_WATER_CORR] = 0.5f;
            g_Config.m_ItemChars[CHASSIS2_MOVE_SLOPE_CORR_UP] = 0.5f;
            g_Config.m_ItemChars[CHASSIS2_MOVE_SLOPE_CORR_DOWN] = 3;
            robot->CalcRobotMass();
            auto *unit = g_MatrixMap->UnitGetTest(5, 10);
            MG_CHECK(unit != nullptr && unit->IsFlat());
            unit->a1 = water ? -3.0f : 0.0f;
            MG_CHECK(robot->Z_From_Pos() == unit->a1 && !robot->IsMustDie());
            auto *core = robot->GetCore(DEBUG_CALL_INFO);
            core->m_Matrix._21 = std::sqrt(1 - slope * slope);
            core->m_Matrix._22 = 0;
            core->m_Matrix._23 = backwards ? -slope : slope;
            core->Release();
            robot->m_GroupSpeed = 0.5f;
            robot->m_ColSpeed = 100;
            bool rotate = true;
            MG_CHECK(robot->Seek(D3DXVECTOR3(backwards ? 0 : 500, 200, 0), rotate, false, backwards));
            const float dry = slope == -0.5f ? 1.0f : slope == 0 ? 0.5f : slope == 0.25f ? 0.25f : 0.0f;
            const float speed = water ? dry * 0.5f : dry;
            std::cout << "terrain " << water << ' ' << backwards << ' ' << slope << " velocity "
                      << std::hexfloat << robot->m_Velocity.x << ',' << robot->m_Velocity.y << " expected " << speed << std::defaultfloat << '\n';
            MG_CHECK(robot->m_Speed == speed);
            // D3DX's unit-vector normalization may round one ulp below one.
            MG_CHECK(std::abs(robot->m_Velocity.x - (backwards ? -speed : speed)) <= 0.00000012f);
            MG_CHECK(robot->m_Velocity.y == 0 && robot->m_Velocity.z == 0);
            MG_CHECK(robot->m_PosX == 100 && robot->m_PosY == 200);
        }
}
int sloped_journey(random::Mode mode, int step, float gradient) {
    Profile profile{mode, 81, step, 5000, false};
    profile.layout = NavigationLayout::Open;
    Session session(profile);
    MG_CHECK(session.remove(2));
    auto *robot = g_MatrixMap->GetPlayerSide()->GetCurGroup()->GetObjectByN(0)->AsRobot();
    // A continuous plane supplies both height triangles and vertex normals.
    // Navigation masks remain open; the production tick derives chassis tilt.
    D3DXVECTOR3 normal(-gradient, 0, 1);
    D3DXVec3Normalize(&normal, &normal);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        auto &unit = *g_MatrixMap->UnitGet(x, y);
        unit.SetType(CELLFLAG_LAND);
        unit.a1 = unit.a2 = gradient;
        unit.b1 = unit.b2 = 0;
        unit.c1 = unit.c2 = 200 + gradient * x * GLOBAL_SCALE;
    }
    for (int y = 0; y <= 32; ++y) for (int x = 0; x <= 32; ++x) {
        auto &point = *g_MatrixMap->PointGet(x, y);
        point.z = point.z_land = 200 + gradient * x * GLOBAL_SCALE;
        point.n = normal;
    }
    g_Config.m_ItemChars[CHASSIS2_MOVE_SPEED] = 0.6f;
    g_Config.m_ItemChars[CHASSIS2_MOVE_SLOPE_CORR_UP] = 0.5f;
    g_Config.m_ItemChars[CHASSIS2_MOVE_SLOPE_CORR_DOWN] = 3;
    robot->CalcRobotMass();
    session.ground_command(500, 200);
    int samples = 0;
    while (session.frame() < profile.max_frames) {
        session.advance();
        const auto state = session.robot(1);
        MG_CHECK(state.alive && std::isfinite(state.x) && std::isfinite(state.y));
        MG_CHECK(!robot->IsMustDie() && state.order == mpo_MoveTo);
        MG_CHECK(state.destination_x == 48 && state.destination_y == 18);
        MG_CHECK(std::abs(robot->Z_From_Pos() - (200 + gradient * state.x)) < 0.001f);
        if (state.x > 240 && state.x < 360) {
            MG_CHECK(robot->m_Speed > 0);
            if (gradient > 0) MG_CHECK(robot->m_Speed < 0.4f);
            else if (gradient < 0) MG_CHECK(robot->m_Speed > 0.8f);
            else MG_CHECK(std::abs(robot->m_Speed - 0.6f) < 0.00001f);
            ++samples;
        }
        if (robot->PLIsInPlace()) break;
    }
    MG_CHECK(robot->PLIsInPlace() && samples > 0);
    MG_CHECK(std::abs(robot->m_PosX - 500) < 1 && std::abs(robot->m_PosY - 200) < 1);
    std::cout << session.metadata("continuous-slope-journey-v1") << " gradient=" << gradient
              << " arrival_ms=" << session.time() << " samples=" << samples << '\n';
    return session.time();
}
void sloped_terrain_journeys() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const int flat = sloped_journey(mode, step, 0);
        const int uphill = sloped_journey(mode, step, 0.25f);
        const int downhill = sloped_journey(mode, step, -0.25f);
        MG_CHECK(uphill > flat && flat > downhill);
    }
}

void water_depth() {
    for (float depth : {-101.0f, -102.0f, -103.0f}) {
        Profile p; p.layout = NavigationLayout::Open;
        Session session(p);
        auto *robot = g_MatrixMap->GetPlayerSide()->GetCurGroup()->GetObjectByN(0)->AsRobot();
        g_MatrixMap->UnitGetTest(5, 10)->a1 = depth;
        MG_CHECK(robot->Z_From_Pos() == depth);
        MG_CHECK(robot->IsMustDie() == (depth < -102));
    }
}
const Case cases[] = {
    {"game.navigation.sloped_terrain_journeys", sloped_terrain_journeys},
    {"game.navigation.zone_routes", zone_routes},
    {"game.navigation.chassis_traversal", chassis_traversal},
    {"game.navigation.road_routes", road_routes},
    {"game.navigation.road_route_reuse", road_route_reuse},
    {"game.navigation.formation_speed_boundaries", formation_speed_boundaries},
    {"game.navigation.collision_boundaries", collision_boundaries},
    {"game.navigation.collision_wait_threshold", collision_wait_threshold},
    {"game.navigation.moving_target", moving_target},
    {"game.navigation.replacement_near_blocker", replacement_near_blocker},
    {"game.navigation.dense_flow", dense_flow},
    {"game.navigation.terrain_speed", terrain_speed},
    {"game.navigation.water_depth", water_depth},
};
}
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
