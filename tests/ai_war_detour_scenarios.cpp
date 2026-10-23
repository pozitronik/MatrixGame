// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "Logic/MatrixEnvironment.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <sstream>
#include <vector>

namespace {
using namespace tests;
using namespace tests::battle;
enum class Change { None, Retirement, Move };
struct ReleaseCore { void operator()(SObjectCore *core) const { core->Release(); } };
using Core = std::unique_ptr<SObjectCore, ReleaseCore>;

Profile profile(random::Mode mode, int step) {
    Profile result{mode, 84, step, 24000 / step, false};
    result.layout = NavigationLayout::Open;
    return result;
}
struct Scene {
    BattleCombatAssets assets;
    Session session;
    std::array<CMatrixRobotAI *, 4> robots{};
    std::array<float, 4> initial_health{};
    Core target_core;
    explicit Scene(random::Mode mode, int step) : session(profile(mode, step)) {
        assets.install();
        auto &map = *g_MatrixMap;
        auto &side = *map.GetPlayerSide();
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
            if (object->IsRobot()) for (unsigned id = 1; id <= robots.size(); ++id)
                if (session.matches(id, object->AsRobot())) robots[id - 1] = object->AsRobot();
        for (auto *robot : robots) MG_CHECK(robot);
        const auto position = [&](CMatrixRobotAI &robot, float x, float y) {
            robot.m_PosX = x; robot.m_PosY = y;
            robot.SetMaxSpeed(0); robot.m_GroupSpeed = 0;
            robot.MapPosCalc(); robot.RChange(MR_Matrix); robot.RNeed(MR_Matrix);
            // Setup relocates existing meshes at the same map time.
            robot.m_CalcBoundsLastTime = map.GetTime() - 100;
            robot.JoinToGroup();
            MG_CHECK(std::abs(robot.GetGeoCenter().x - x) < .001f);
            MG_CHECK(std::abs(robot.GetGeoCenter().y - y) < .001f);
        };
        position(*robots[1], 100, 100);
        position(*robots[2], 240, 280);
        position(*robots[3], 600, 600);
        // The original open layout has four sparse places. Supply nearby firing
        // places and all unobstructed connections, then rebuild its spatial index.
        const CPoint places[]{{8, 18}, {16, 22}, {16, 24}, {48, 8}};
        for (int i = 0; i < 4; ++i) {
            auto &place = map.m_RN.m_Place[i];
            place.m_Pos = places[i];
            place.m_NearCnt = 0;
            for (int j = 0; j < 4; ++j) if (i != j) {
                MG_CHECK(map.CanMoveFromTo(RUK_CHASSIS_WHEEL - 1, 4,
                    places[i].x, places[i].y, places[j].x, places[j].y, nullptr));
                place.m_Near[place.m_NearCnt++] = j;
            }
        }
        map.m_RN.InitPL(64, 64);
        g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 160;
        g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {2, 0, 0};
        g_Config.m_Overheat[WEAPON_LASER_HEAT] = 10;
        g_Config.m_Overheat[WEAPON_LASER_COOL] = 10;
        g_Config.m_Overheat[WCP_LASER] = 100;
        g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = .04f;
        arm_battle_robot(*robots[0], RUK_WEAPON_LASER);
        MG_CHECK(robots[0]->GetMaxFireDist() == 160);
        robots[2]->InitMaxHitpoint(200);
        target_core.reset(robots[2]->GetCore(DEBUG_CALL_INFO));
        side.CreateGroupFromCurrent(robots[0]);
        side.Select(ROBOT, nullptr);
        MG_CHECK(side.GetCurGroup()->GetRobotsCnt() == 1);
        g_MatrixData->BlockGet(L"Chars")->BlockGet(L"ChassisSounds")->BlockGet(L"2")
            ->ParSetAdd(L"Patrol", L"Sound.Synthetic");
        // Patrol takes its From point from the preceding planned Move endpoint.
        // Both are ordinary commands; no tactical target/place is injected.
        session.ground_command(500, 100);
        side.PGOrderPatrol(side.RobotToLogicGroup(robots[0]), CPoint(48, 38));
        const auto &order = side.m_PlayerGroup[robots[0]->GetGroupLogic()];
        MG_CHECK(order.Order() == mpo_Patrol && order.m_From == CPoint(48, 8) && order.m_To == CPoint(48, 38));
        CPoint move;
        MG_CHECK(robots[0]->GetMoveToCoords(move) && move == CPoint(48, 8));
        for (size_t i = 0; i < robots.size(); ++i) {
            initial_health[i] = robots[i]->GetHitPoint();
            MG_CHECK(initial_health[i] > 0);
        }
    }
};
struct Journey {
    std::vector<std::string> frames;
    int war = -1, detour = -1, reached_firing_place = -1, first_hit = -1;
    int terminal = -1, changed = -1, resumed = -1, arrival = -1, pulses = 0;
};
Journey journey(random::Mode mode, int step, Change change) {
    Scene scene(mode, step);
    auto &session = scene.session;
    auto &map = *g_MatrixMap;
    auto &side = *map.GetPlayerSide();
    auto *observer = scene.robots[0], *target = scene.robots[2];
    const auto &core = scene.target_core;
    std::cout << session.metadata("reachable-war-detours-v1") << '\n';
    std::cout << "change=" << int(change) << " change_time_ms=800 initial_leg=48,8 patrol_to=48,38\n";
    Journey result;
    const int max_frames = 24000 / step;
    result.frames.reserve(max_frames);
    CPoint firing_place(-1, -1);
    float health = target->GetHitPoint();
    bool fired_on_detour = false;
    while (session.frame() < max_frames) {
        if (session.time() == 800 && change != Change::None) {
            MG_CHECK(result.detour > 0 && result.first_hit > 0 && result.reached_firing_place < 0);
            MG_CHECK(core->m_Object == target && target->IsLiveRobot());
            CPoint move;
            MG_CHECK(observer->GetMoveToCoords(move) && move == firing_place);
            result.changed = session.time();
            if (change == Change::Retirement) {
                MG_CHECK(session.remove(3));
                MG_CHECK(!core->m_Object && !observer->GetEnv()->m_Target && !observer->GetEnv()->m_TargetAttack);
            } else {
                session.ground_command(500, 400);
                const auto state = session.robot(1);
                MG_CHECK(state.order == mpo_MoveTo && state.destination_x == 48 && state.destination_y == 38);
                MG_CHECK(!side.m_PlayerGroup[observer->GetGroupLogic()].IsWar());
                MG_CHECK(observer->GetMoveToCoords(move) && move == CPoint(48, 38));
            }
        }
        const auto *firing_target = observer->GetEnv()->m_Target;
        session.advance();
        const auto draws = random::draw_count();
        const auto state = session.robot(1);
        MG_CHECK(state.alive && std::isfinite(state.x) && std::isfinite(state.y));
        MG_CHECK(state.x >= 0 && state.x < 640 && state.y >= 0 && state.y < 640);
        for (unsigned id : {2u, 4u}) MG_CHECK(session.robot(id).alive);
        MG_CHECK(scene.robots[1]->m_PosX == 100 && scene.robots[1]->m_PosY == 100);
        MG_CHECK(scene.robots[3]->m_PosX == 600 && scene.robots[3]->m_PosY == 600);
        MG_CHECK(scene.robots[1]->GetHitPoint() == scene.initial_health[1]);
        MG_CHECK(scene.robots[3]->GetHitPoint() == scene.initial_health[3]);
        MG_CHECK(observer->GetHitPoint() == scene.initial_health[0]);
        auto &env = *observer->GetEnv();
        const auto &order = side.m_PlayerGroup[observer->GetGroupLogic()];
        MG_CHECK(order.Order() == (result.changed >= 0 && change == Change::Move ? mpo_MoveTo : mpo_Patrol));
        MG_CHECK(order.m_To == CPoint(48, 38));
        if (order.Order() == mpo_Patrol) MG_CHECK(order.m_From == CPoint(48, 8));
        MG_CHECK(env.m_PlaceNotFound < 0);
        CPoint move(-1, -1);
        const bool moving = observer->GetMoveToCoords(move);
        if (order.IsWar()) {
            MG_CHECK(!(result.changed >= 0 && change == Change::Move));
            if (result.war < 0) result.war = session.time();
            if (env.m_Place >= 0 && moving && move != CPoint(48, 8) &&
                core->m_Object == target && target->IsLiveRobot()) {
                MG_CHECK(env.m_Place < map.m_RN.m_PlaceCnt);
                MG_CHECK(map.m_RN.m_Place[env.m_Place].m_Pos == move);
                MG_CHECK(env.m_TargetAttack == target && core->m_Object == target && target->IsLiveRobot());
                MG_CHECK(move == CPoint(16, 22));
                if (result.detour < 0) { result.detour = session.time(); firing_place = move; }
                MG_CHECK(move == firing_place);
            }
        }
        if (result.detour >= 0 && (result.changed < 0 || change == Change::Retirement) &&
            !moving && observer->m_PosX == float(firing_place.x * 10 + 20) &&
            observer->m_PosY == float(firing_place.y * 10 + 20) && result.reached_firing_place < 0)
            result.reached_firing_place = session.time();
        if (core->m_Object && target->GetHitPoint() < health) {
            MG_CHECK(firing_target == target);
            MG_CHECK(target->m_PosX == 240 && target->m_PosY == 280);
            ++result.pulses;
            health = target->GetHitPoint();
            MG_CHECK(std::abs(health - float(200 - 2 * result.pulses) / 10) < .00001f);
            if (result.first_hit < 0) result.first_hit = session.time();
            fired_on_detour |= result.detour > 0 && result.changed < 0;
        }
        if ((!core->m_Object || !target->IsLiveRobot()) && result.terminal < 0) result.terminal = session.time();
        if (result.terminal >= 0 && session.time() > result.terminal + step) {
            MG_CHECK(!env.m_Target && !env.m_TargetAttack && env.GetEnemyCnt() == 0);
            MG_CHECK(!observer->FindOrderLikeThat(ROT_FIRE));
        }
        const CPoint destination = change == Change::Move ? CPoint(48, 38) : CPoint(48, 8);
        if (moving && move == destination && result.detour > 0 &&
            ((change == Change::Move && result.changed >= 0) || result.terminal >= 0) && result.resumed < 0)
            result.resumed = session.time();
        const bool arrived = change == Change::Move
            ? result.changed >= 0 && state.in_place && state.x == 500 && state.y == 400
            : result.terminal >= 0 && result.resumed >= 0 && state.x > 490 && std::abs(state.y - 100) < 5;
        std::ostringstream detail;
        detail << "{\"time_ms\":" << session.time() << ",\"war\":" << order.IsWar()
               << ",\"place\":" << env.m_Place << ",\"failed_at\":" << env.m_PlaceNotFound
               << ",\"enemy_count\":" << env.GetEnemyCnt() << ",\"target\":" << (env.m_Target == target)
               << ",\"attack_target\":" << (env.m_TargetAttack == target) << ",\"core_live\":" << bool(core->m_Object)
               << ",\"patrol_from\":[" << order.m_From.x << ',' << order.m_From.y << "],\"move\":["
               << move.x << ',' << move.y << "]}";
        result.frames.push_back(session.snapshot() + "\n" + detail.str());
        MG_CHECK(random::draw_count() == draws);
        if (session.time() % 1000 == 0 || arrived) std::cout << detail.str() << '\n';
        if (arrived) { result.arrival = session.time(); break; }
    }
    MG_CHECK(result.war > 0 && result.detour >= result.war && result.first_hit > 0 && fired_on_detour);
    MG_CHECK(result.resumed > result.detour && result.arrival > result.resumed);
    MG_CHECK(!observer->GetEnv()->m_Target && !observer->GetEnv()->m_TargetAttack);
    MG_CHECK(!observer->FindOrderLikeThat(ROT_FIRE));
    if (change == Change::None) {
        MG_CHECK(result.pulses == 100 && health == 0);
        MG_CHECK(result.reached_firing_place > result.detour && result.reached_firing_place < result.terminal);
        MG_CHECK(result.resumed - result.terminal <= 100 + step);
    } else if (change == Change::Retirement) {
        MG_CHECK(result.changed == 800 && result.terminal == 800 + step && !core->m_Object);
        MG_CHECK(result.pulses > 0 && result.pulses < 100);
        MG_CHECK(result.reached_firing_place > result.changed && result.resumed >= result.reached_firing_place);
        MG_CHECK(result.resumed - result.reached_firing_place <= 100 + step);
    } else {
        MG_CHECK(result.changed == 800 && result.terminal == -1 && target->IsLiveRobot());
        MG_CHECK(result.pulses > 0 && result.pulses < 100 && health > 0);
        MG_CHECK(result.resumed == 800 + step);
    }
    std::cout << "milestones war=" << result.war << " detour=" << result.detour << " firing_place="
              << firing_place.x << ',' << firing_place.y << " reached=" << result.reached_firing_place
              << " first_hit=" << result.first_hit << " terminal=" << result.terminal << " changed=" << result.changed
              << " resumed=" << result.resumed << " arrival=" << result.arrival << " pulses=" << result.pulses
              << " health=" << health << '\n';
    return result;
}
template<random::Mode Mode, int Step, Change Event>
void run() {
    const auto first = journey(Mode, Step, Event);
    const auto replay = journey(Mode, Step, Event);
    MG_CHECK(first.frames == replay.frames);
    if constexpr (Event != Change::None) {
        const auto control = journey(Mode, Step, Change::None);
        const size_t prefix = 800 / Step;
        MG_CHECK(std::equal(first.frames.begin(), first.frames.begin() + prefix, control.frames.begin()));
    }
}
constexpr Case cases[]{
    {"game.ai.war_detour.control.park_miller10", run<random::Mode::ParkMiller,10,Change::None>},
    {"game.ai.war_detour.retirement.park_miller10", run<random::Mode::ParkMiller,10,Change::Retirement>},
    {"game.ai.war_detour.replacement.park_miller10", run<random::Mode::ParkMiller,10,Change::Move>},
    {"game.ai.war_detour.control.park_miller16", run<random::Mode::ParkMiller,16,Change::None>},
    {"game.ai.war_detour.retirement.park_miller16", run<random::Mode::ParkMiller,16,Change::Retirement>},
    {"game.ai.war_detour.replacement.park_miller16", run<random::Mode::ParkMiller,16,Change::Move>},
    {"game.ai.war_detour.control.legacy10", run<random::Mode::LegacyCRT,10,Change::None>},
    {"game.ai.war_detour.retirement.legacy10", run<random::Mode::LegacyCRT,10,Change::Retirement>},
    {"game.ai.war_detour.replacement.legacy10", run<random::Mode::LegacyCRT,10,Change::Move>},
    {"game.ai.war_detour.control.legacy16", run<random::Mode::LegacyCRT,16,Change::None>},
    {"game.ai.war_detour.retirement.legacy16", run<random::Mode::LegacyCRT,16,Change::Retirement>},
    {"game.ai.war_detour.replacement.legacy16", run<random::Mode::LegacyCRT,16,Change::Move>},
};
}
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
