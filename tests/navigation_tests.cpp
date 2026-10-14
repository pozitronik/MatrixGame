// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "MatrixMap.hpp"
#include "MatrixRobot.hpp"
#include "MatrixFlyer.hpp"
#include "MatrixSide.hpp"
#include "Effects/MatrixEffectLandscapeSpot.hpp"
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <vector>

namespace tests::navigation {
void multiregion_movement();
void multiregion_road_guidance();
void group_speed_nonfinite_probe();
void unreachable_replacement();
}

namespace {
using namespace tests;
using namespace tests::battle;
constexpr int wheel = RUK_CHASSIS_WHEEL - 1;

Profile profile(NavigationLayout layout, random::Mode mode = random::Mode::ParkMiller, int step = 10) {
    Profile p;
    p.layout = layout;
    p.mode = mode;
    p.step_ms = step;
    return p;
}

struct Route {
    std::vector<CPoint> points;
    int reached_cells;
};

Route route(CPoint start, CPoint end, int size = 4, int chassis = wheel,
            int others = 0, int *sizes = nullptr, CPoint **paths = nullptr,
            int *counts = nullptr, CPoint *destinations = nullptr) {
    auto &map = *g_MatrixMap;
    std::array<CPoint, MatrixPathMoveMax> buffer;
    int zone = 0;
    const auto draws = random::draw_count();
    const int count = map.FindLocalPath(chassis, size, start.x, start.y, &zone, 1,
        end.x, end.y, buffer.data(), others, sizes, paths, counts, destinations, false);
    MG_CHECK(count >= 0 && count < MatrixPathMoveMax);
    MG_CHECK(random::draw_count() == draws);
    int reached = 0;
    // This production search resets [0,60) for size four. Read only that initialized rectangle.
    const int right = std::min(62 + size, 64 - size);
    for (int y = 0; y < right; ++y) for (int x = 0; x < right; ++x)
        if (map.MoveGet(x, y)->m_Find >= 0) ++reached;
    std::cout << "{\"route_points\":" << count << ",\"reached_cells\":" << reached << "}\n";
    if (count) {
        MG_CHECK(buffer.front() == start);
        MG_CHECK(buffer[count - 1] == end);
        for (int i = 1; i < count; ++i) {
            MG_CHECK(std::abs(buffer[i].x - buffer[i - 1].x) + std::abs(buffer[i].y - buffer[i - 1].y) == 1);
            MG_CHECK(map.IsAbsenceWall(chassis, size, buffer[i].x, buffer[i].y));
        }
    }
    return {{buffer.begin(), buffer.begin() + count}, reached};
}

void local_routes() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        {
            Session session(profile(NavigationLayout::Open, mode));
            std::cout << session.metadata() << '\n';
            const auto straight = route({8, 28}, {48, 28});
            MG_CHECK(straight.points.size() == 41);
            for (int i = 0; i < 41; ++i) MG_CHECK(straight.points[i] == CPoint(8 + i, 28));
            MG_CHECK(g_MatrixMap->CanMoveFromTo(wheel, 4, 8, 28, 48, 28, nullptr));
            MG_CHECK(route({2, 2}, {2, 55}).points.size() == 54);
        }
        {
            Session session(profile(NavigationLayout::CentralObstacle, mode));
            const auto detour = route({8, 28}, {48, 28});
            MG_CHECK(detour.points.size() == 65);
            MG_CHECK(detour.reached_cells == 2951);
            MG_CHECK(!g_MatrixMap->CanMoveFromTo(wheel, 4, 8, 28, 48, 28, nullptr));
            bool above = false;
            for (const auto p : detour.points) if (p.y <= 16) above = true;
            MG_CHECK(above);
            auto optimized = detour.points;
            const int count = g_MatrixMap->OptimizeMovePath(wheel, 4, static_cast<int>(optimized.size()), optimized.data());
            MG_CHECK(count >= 3 && count < static_cast<int>(detour.points.size()));
            MG_CHECK(optimized.front() == CPoint(8, 28));
            MG_CHECK(optimized[count - 1] == CPoint(48, 28));
            for (int i = 1; i < count; ++i)
                MG_CHECK(g_MatrixMap->CanMoveFromTo(wheel, 4, optimized[i - 1].x, optimized[i - 1].y, optimized[i].x, optimized[i].y, nullptr));
            MG_CHECK(route({8, 28}, {30, 28}).points.empty());
        }
        {
            Session session(profile(NavigationLayout::DeadEnd, mode));
            const auto escape = route({30, 24}, {30, 8});
            MG_CHECK(escape.points.size() == 77);
            bool through_mouth = false;
            for (const auto p : escape.points) if (p.y >= 44) through_mouth = true;
            MG_CHECK(through_mouth);
        }
    }
}

void passage_width() {
    Session session(profile(NavigationLayout::Corridor));
    MG_CHECK(route({8, 28}, {48, 28}, 4).points.size() == 41);
    MG_CHECK(route({8, 28}, {48, 28}, 5).points.empty());
    for (int chassis = 0; chassis < 5; ++chassis) {
        MG_CHECK(g_MatrixMap->IsAbsenceWall(chassis, 4, 28, 28));
        MG_CHECK(!g_MatrixMap->IsAbsenceWall(chassis, 5, 28, 28));
    }
}

void placement() {
    Session session(profile(NavigationLayout::Open));
    auto &map = *g_MatrixMap;
    int x = 20, y = 20;
    MG_CHECK(map.PlaceFindNear(wheel, 4, x, y, 0, nullptr, nullptr));
    MG_CHECK(x == 20 && y == 20);
    int size = 4;
    CPoint occupied(20, 20);
    MG_CHECK(map.PlaceFindNear(wheel, 4, x, y, 1, &size, &occupied));
    std::cout << "occupied alternative " << x << ',' << y << '\n';
    MG_CHECK(x == 16 && y == 16);
    x = 0; y = 0;
    MG_CHECK(map.PlaceFindNear(wheel, 4, x, y, 0, nullptr, nullptr));
    MG_CHECK(x == 2 && y == 2);
    size = 64; occupied = CPoint(0, 0); x = 20; y = 20;
    MG_CHECK(!map.PlaceFindNear(wheel, 4, x, y, 1, &size, &occupied));
    MG_CHECK(x == 20 && y == 20);
}

void region_routes() {
    CHeap heap;
    CMatrixRoadNetwork road(&heap);
    for (int i = 0; i < 5; ++i) {
        const int index = road.AddRegion();
        road.m_Region[index] = SMatrixRegion{};
    }
    auto connect = [&](int from, int to, byte mask = 0) {
        auto &r = road.m_Region[from];
        const int n = r.m_NearCnt++;
        r.m_Near[n] = to; r.m_NearMove[n] = mask;
    };
    connect(0, 1); connect(1, 0); connect(1, 3); connect(3, 1);
    connect(0, 2); connect(2, 0); connect(2, 3); connect(3, 2);
    road.FindPathInRegionInit();
    int path[8]{};
    MG_CHECK(road.FindPathInRegionRun(1 << wheel, 0, 3, path, 8, false) == 3);
    std::cout << "region choice " << path[0] << ',' << path[1] << ',' << path[2] << '\n';
    MG_CHECK(path[0] == 0 && path[1] == 2 && path[2] == 3);
    road.m_Region[0].m_NearMove[1] = road.m_Region[2].m_NearMove[0] = 1 << wheel;
    MG_CHECK(road.FindPathInRegionRun(1 << wheel, 0, 3, path, 8, false) == 3);
    MG_CHECK(path[1] == 1);
    MG_CHECK(road.FindPathInRegionRun(1, 0, 3, path, 8, false) == 3);
    MG_CHECK(path[1] == 2);
    MG_CHECK(road.FindPathInRegionRun(1 << wheel, 0, 4, path, 8, false) == 0);
    MG_CHECK(road.FindPathInRegionRun(1 << wheel, 0, 0, path, 8, false) == 0);
}

void destination_weights() {
    Session session(profile(NavigationLayout::Open));
    CPoint traffic[] = {{28, 28}, {32, 28}};
    CPoint *paths[] = {traffic};
    int size = 4, count = 2;
    CPoint destination(38, 28);
    const auto crowded = route({8, 28}, {48, 28}, 4, wheel, 1, &size, paths, &count, &destination);
    MG_CHECK(crowded.points.size() == 49);
    MG_CHECK(g_MatrixMap->MoveGet(28, 28)->m_Weight == 30);
    MG_CHECK(g_MatrixMap->MoveGet(38, 28)->m_Weight == 200);
    MG_CHECK(g_MatrixMap->MoveGet(20, 28)->m_Weight == 5);
    bool avoids_destination = true;
    for (auto p : crowded.points) if (p.x >= 35 && p.x < 42 && p.y >= 25 && p.y < 32) avoids_destination = false;
    MG_CHECK(avoids_destination);
    MG_CHECK(crowded.reached_cells == 2693);
    const auto clear = route({8, 28}, {48, 28});
    MG_CHECK(clear.points.size() == 41);
    MG_CHECK(g_MatrixMap->MoveGet(38, 28)->m_Weight == 5);
}

CMatrixRobotAI *robot_at(float x, float y) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot() && object->AsRobot()->m_PosX == x && object->AsRobot()->m_PosY == y)
            return object->AsRobot();
    throw std::runtime_error("fixture robot not found");
}

void carrier_lifetime() {
    Session session;
    auto &map = *g_MatrixMap;
    auto *first = robot_at(100, 200);
    auto *second = robot_at(100, 260);
    auto *carrier = map.StaticAdd<CMatrixFlyer>();
    MG_CHECK(!carrier->CarryingRobot());
    MG_CHECK(first->Carry(carrier, true));
    MG_CHECK(carrier->GetCarryingRobot() == first);
    MG_CHECK(first->m_CurrState == ROBOT_CARRYING);
    MG_CHECK(carrier->GetCarryData()->m_RobotMassFactor == 1);
    MG_CHECK(second->Carry(carrier, false));
    MG_CHECK(first->m_CurrState == ROBOT_FALLING);
    MG_CHECK(!first->IsMustDie());
    MG_CHECK(carrier->GetCarryingRobot() == second);
    MG_CHECK(second->m_CurrState == ROBOT_CARRYING);
    MG_CHECK(carrier->GetCarryData()->m_RobotMassFactor == 0);
    MG_CHECK(second->Carry(nullptr));
    MG_CHECK(!carrier->CarryingRobot());
    MG_CHECK(second->m_CurrState == ROBOT_FALLING);
    MG_CHECK(first->Carry(carrier, true));
    map.StaticDelete(carrier);
    MG_CHECK(first->m_CurrState == ROBOT_FALLING);
    MG_CHECK(first->IsMustDie());
    MG_CHECK(!second->IsMustDie());
}

void carrier_drop_landing() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        Session session(profile(NavigationLayout::Open, mode, step));
        auto *robot = robot_at(100, 200);
        auto *carrier = g_MatrixMap->StaticAdd<CMatrixFlyer>();
        MG_CHECK(robot->Carry(carrier, true));
        // Declared release height; this case starts after the flight reaches its drop point.
        auto *core = robot->GetCore(DEBUG_CALL_INFO);
        core->m_Matrix._43 = 80;
        core->Release();
        MG_CHECK(robot->Carry(nullptr));
        MG_CHECK(!carrier->CarryingRobot());
        g_MatrixMap->StaticDelete(carrier);
        MG_CHECK(!robot->IsMustDie());
        float previous_height = 80;
        int landed_at = -1;
        for (int frame = 0; frame < 200; ++frame) {
            session.advance();
            const float height = robot->GetMatrix()._43;
            MG_CHECK(std::isfinite(height) && height >= 0 && height <= previous_height);
            MG_CHECK(robot->m_PosX == 100 && robot->m_PosY == 200);
            previous_height = height;
            if (robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD) {
                landed_at = session.time();
                break;
            }
            MG_CHECK(robot->m_CurrState == ROBOT_FALLING);
        }
        MG_CHECK(landed_at == (step == 10 ? 970 : 976));
        MG_CHECK(previous_height == 0 && !robot->IsMustDie());
        auto &side = *g_MatrixMap->GetPlayerSide();
        side.PGOrderMoveTo(side.RobotToLogicGroup(robot), CPoint(30, 18));
        for (int frame = 0; frame < 1500; ++frame) session.advance();
        MG_CHECK(robot->PLIsInPlace());
        MG_CHECK(robot->m_PosX == 320 && robot->m_PosY == 200);
        MG_CHECK(robot->m_CurrState == ROBOT_SUCCESSFULLY_BUILD);
        std::cout << session.metadata("synthetic-carrier-drop-v1") << '\n';
        std::cout << "drop step_ms " << step << " landed_at_ms " << landed_at << '\n';
    }
}

void carrier_cap() {
    Session session;
    auto &map = *g_MatrixMap;
    MG_CHECK(MAX_ALWAYS_DRAW_OBJ == 16);
    MG_CHECK(map.m_AD_Obj_cnt == 0);
    for (int i = 0; i < MAX_ALWAYS_DRAW_OBJ; ++i) {
        auto *carrier = map.StaticAdd<CMatrixFlyer>();
        map.m_AD_Obj[map.m_AD_Obj_cnt++] = carrier;
    }
    const auto before = random::draw_count();
    auto *last = CMatrixMapStatic::GetFirstLogic();
    map.GetPlayerSide()->OrderFlyer(D3DXVECTOR2(100, 100), FO_GIVE_BOT, 0, 0, CPoint(0, 0), 0);
    MG_CHECK(map.m_AD_Obj_cnt == 16);
    MG_CHECK(CMatrixMapStatic::GetFirstLogic() == last);
    MG_CHECK(random::draw_count() == before);
    // No FlyerOrders block exists: the early cap rejection must precede resource lookup.
    while (map.m_AD_Obj_cnt) map.StaticDelete(map.m_AD_Obj[map.m_AD_Obj_cnt - 1]);
}

std::vector<std::string> group_run(NavigationLayout layout, random::Mode mode, int step, int run = 0) {
    Session session(profile(layout, mode, step));
    const auto directory = std::filesystem::current_path() / "navigation-traces";
    std::filesystem::create_directories(directory);
    const auto name = std::string(navigation_layout_name(layout)) + "-" +
        (mode == random::Mode::ParkMiller ? "pm" : "crt") + "-" + std::to_string(step) + "-" + std::to_string(run);
    std::ofstream trace(directory / (name + ".jsonl"));
    MG_CHECK(trace.good());
    trace << session.metadata(std::string("unarmed-chassis-") + navigation_layout_name(layout)) << '\n';
    session.ground_command(500, 300);
    trace << "{\"type\":\"command\",\"frame\":0,\"name\":\"context-ground(500,300)\"}\n";
    trace.flush();
    std::vector<std::string> snapshots;
    bool crossed = false;
    for (int i = 0; i < 15000 / step; ++i) {
        session.advance();
        snapshots.push_back(session.snapshot());
        trace << "{\"type\":\"snapshot\",\"state\":" << snapshots.back() << "}\n";
        trace.flush();
        MG_CHECK(trace.good());
        for (unsigned id : {1u, 2u}) {
            const auto r = session.robot(id);
            MG_CHECK(r.alive && std::isfinite(r.x) && std::isfinite(r.y));
            MG_CHECK(r.x >= 20 && r.x <= 620 && r.y >= 20 && r.y <= 620);
            if (r.x > 360) crossed = true;
        }
    }
    session.check_running();
    std::cout << session.metadata(std::string("unarmed-chassis-") + navigation_layout_name(layout)) << '\n' << session.snapshot() << '\n';
    if (layout == NavigationLayout::Disconnected) {
        MG_CHECK(!crossed);
        MG_CHECK(!session.robot(1).in_place && !session.robot(2).in_place);
        MG_CHECK(session.robot(1).x == 100 && session.robot(1).y == 200);
        MG_CHECK(session.robot(2).x == 100 && session.robot(2).y == 260);
    } else {
        MG_CHECK(crossed);
        MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        const auto a = session.robot(1), b = session.robot(2);
        MG_CHECK(std::hypot(a.x - b.x, a.y - b.y) >= 20);
        MG_CHECK(a.x == 500 && a.y == 300);
        MG_CHECK(b.x == 460 && b.y == 260);
    }
    trace << "{\"type\":\"complete\",\"frame\":" << session.frame() << ",\"time_ms\":" << session.time() << "}\n";
    trace.flush();
    MG_CHECK(trace.good());
    return snapshots;
}
void group_movement() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int step : {10, 16}) {
            const auto first = group_run(NavigationLayout::Open, mode, step, 0);
            MG_CHECK(first == group_run(NavigationLayout::Open, mode, step, 1));
        }
}
void opposing_flow() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session(profile(NavigationLayout::Open, mode));
        auto *third = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 500, 200);
        auto *fourth = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 540, 260);
        third->SetGroupLogic(-1);
        fourth->SetGroupLogic(-1);
        auto &side = *g_MatrixMap->GetPlayerSide();
        session.ground_command(500, 230);
        side.PGOrderMoveTo(side.RobotToLogicGroup(third), CPoint(8, 18));
        side.PGOrderMoveTo(side.RobotToLogicGroup(fourth), CPoint(8, 24));
        float minimum = 1000;
        int moving_frames = 0;
        float previous_x = third->m_PosX;
        for (int frame = 0; frame < 1500; ++frame) {
            session.advance();
            for (unsigned id : {1u, 2u}) for (auto *opposite : {third, fourth}) {
                const auto a = session.robot(id);
                MG_CHECK(std::isfinite(a.x) && std::isfinite(a.y));
                MG_CHECK(std::isfinite(opposite->m_PosX) && std::isfinite(opposite->m_PosY));
                minimum = std::min(minimum, std::hypot(a.x - opposite->m_PosX, a.y - opposite->m_PosY));
            }
            if (third->m_PosX != previous_x) ++moving_frames;
            previous_x = third->m_PosX;
        }
        std::cout << session.metadata() << '\n' << session.snapshot() << '\n';
        std::cout << "minimum separation " << minimum << " moving frames " << moving_frames << '\n';
        MG_CHECK(session.robot(1).x > 400 && session.robot(2).x > 400);
        MG_CHECK(third->m_PosX < 200 && fourth->m_PosX < 200);
        MG_CHECK(minimum > 1 && minimum < 80);
        MG_CHECK(moving_frames > 0);
    }
}

struct TrackEffectAccess : CMatrixEffect {
    static SSpotProperties install() {
        const auto previous = m_SpotProperties[SPOT_SOLE_TRACK];
        m_SpotProperties[SPOT_SOLE_TRACK] = m_SpotProperties[SPOT_SOLE_WHEEL];
        return previous;
    }
    static void restore(SSpotProperties previous) { m_SpotProperties[SPOT_SOLE_TRACK] = previous; }
};
struct TrackEffects {
    SSpotProperties previous = TrackEffectAccess::install();
    ~TrackEffects() { TrackEffectAccess::restore(previous); }
};

void mixed_chassis() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session(profile(NavigationLayout::Open, mode));
        TrackEffects effects;
        g_Config.m_ItemChars[CHASSIS3_MOVE_SPEED] = .3f;
        g_Config.m_ItemChars[CHASSIS3_ROTATION_SPEED] = .04f;
        g_Config.m_ItemChars[CHASSIS3_STRUCTURE] = 1000;
        g_Config.m_ItemChars[CHASSIS3_MOVE_WATER_CORR] = .5f;
        g_Config.m_ItemChars[CHASSIS3_MOVE_SLOPE_CORR_DOWN] = 2.5f;
        g_Config.m_ItemChars[CHASSIS3_MOVE_SLOPE_CORR_UP] = .7f;
        g_MatrixData->BlockGetAdd(L"Chars")->BlockGetAdd(L"ChassisSounds")->BlockGetAdd(L"3")->ParSetAdd(L"MoveTo", L"Sound.Synthetic");
        auto *tracked = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 320, RUK_CHASSIS_TRACK);
        g_MatrixMap->GetPlayerSide()->GetCurSelGroup()->AddObject(tracked, -4);
        MG_CHECK(tracked->GetMaxSpeed() == .3f);
        MG_CHECK(robot_at(100, 200)->GetMaxSpeed() == .6f);
        session.ground_command(500, 300);
        MG_CHECK(tracked->GetGroupLogic() == session.robot(1).group);
        for (int frame = 0; frame < 2500; ++frame) {
            session.advance();
            MG_CHECK(std::isfinite(tracked->m_PosX) && std::isfinite(tracked->m_PosY));
        }
        std::cout << session.metadata() << '\n';
        std::cout << "mixed wheel/track position " << tracked->m_PosX << ',' << tracked->m_PosY << '\n';
        MG_CHECK(session.robot(1).in_place && session.robot(2).in_place && tracked->PLIsInPlace());
        MG_CHECK(tracked->m_PosX > 400);
        MG_CHECK(tracked->GetOrdersInPool() == 0);
    }
}

void removed_blocker() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Session session(profile(NavigationLayout::Open, mode));
        auto *blocker = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 300, 220);
        blocker->SetGroupLogic(-1);
        session.ground_command(500, 230);
        for (int frame = 0; frame < 1500; ++frame) {
            if (frame == 300) g_MatrixMap->StaticDelete(blocker);
            session.advance();
            MG_CHECK(std::isfinite(session.robot(1).x) && std::isfinite(session.robot(2).x));
        }
        MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        MG_CHECK(session.robot(1).x == 500 && session.robot(1).y == 230);
        MG_CHECK(session.robot(2).x == 460 && session.robot(2).y == 190);
    }
}

void corridor_movement() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        group_run(NavigationLayout::Corridor, mode, 10);
}
void disconnected() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        group_run(NavigationLayout::Disconnected, mode, 10);
}
}
namespace tests::navigation { void delivery_flights(); void delivery_carrier_damage(); }
int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const tests::Case cases[] = {
        {"game.navigation.delivery_flights", tests::navigation::delivery_flights},
        {"game.navigation.delivery_carrier_damage", tests::navigation::delivery_carrier_damage},
        {"game.navigation.local_routes", local_routes},
        {"game.navigation.passage_width", passage_width},
        {"game.navigation.placement", placement},
        {"game.navigation.region_routes", region_routes},
        {"game.navigation.destination_weights", destination_weights},
        {"game.navigation.group_movement", group_movement},
        {"game.navigation.disconnected", disconnected},
        {"game.navigation.opposing_flow", opposing_flow},
        {"game.navigation.corridor_movement", corridor_movement},
        {"game.navigation.mixed_chassis", mixed_chassis},
        {"game.navigation.removed_blocker", removed_blocker},
        {"game.navigation.carrier_lifetime", carrier_lifetime},
        {"game.navigation.carrier_cap", carrier_cap},
        {"game.navigation.carrier_drop_landing", carrier_drop_landing},
        {"game.navigation.multiregion_movement", tests::navigation::multiregion_movement},
        {"game.navigation.multiregion_road_guidance", tests::navigation::multiregion_road_guidance},
        {"game.navigation.group_speed_finite", tests::navigation::group_speed_nonfinite_probe},
        {"game.navigation.unreachable_replacement", tests::navigation::unreachable_replacement},
    };
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) result = EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
