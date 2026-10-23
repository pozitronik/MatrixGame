// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_combat_geometry.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {
using namespace tests::battle;
enum class Variant { Unguided, Guided, LiveBlocker, DestroyedBlocker };
constexpr int hit_time = 6000;
constexpr int journey_time = 40000;
constexpr int settled_time = 35000;

const char *variant_name(Variant variant) {
    switch (variant) {
    case Variant::Unguided: return "unguided";
    case Variant::Guided: return "guided";
    case Variant::LiveBlocker: return "live-blocker";
    case Variant::DestroyedBlocker: return "destroyed-blocker";
    }
    throw std::logic_error("Unknown road journey variant");
}

using Core = std::unique_ptr<SObjectCore, void (*)(SObjectCore *)>;
struct Observation {
    std::vector<std::string> trace;
    std::array<unsigned, 2> zones{};
    std::array<int, 2> crossed{};
    std::array<int, 2> arrived{};
    float minimum_separation = 10000;
    int retired{};
    bool operator==(const Observation &) const = default;
};

std::string observe(const Session &session, SObjectCore *blocker_core) {
    std::ostringstream output;
    output << session.snapshot();
    if (blocker_core) {
        output << ":blocker=" << (blocker_core->m_Object != nullptr);
        if (blocker_core->m_Object) {
            const auto *robot = blocker_core->m_Object->AsRobot();
            output << ',' << int(robot->m_CurrState) << ',' << std::bit_cast<uint32_t>(robot->m_PosX)
                << ',' << std::bit_cast<uint32_t>(robot->m_PosY)
                << ',' << std::bit_cast<uint32_t>(robot->GetHitPoint());
        }
    }
    output << ":enemy_kills=" << g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL);
    return output.str();
}

Observation journey(random::Mode mode, int step, Variant variant) {
    const bool guided = variant != Variant::Unguided;
    const bool blocked = variant == Variant::LiveBlocker || variant == Variant::DestroyedBlocker;
    const bool kill = variant == Variant::DestroyedBlocker;
    tests::BattleCombatAssets assets;
    Profile profile{mode, 81, step, journey_time / step, false, true};
    profile.layout = tests::NavigationLayout::RoadRing;
    Session session(profile);
    assets.install();
    auto &side = *g_MatrixMap->GetPlayerSide();
    std::array<CMatrixRobotAI *, 2> actors{};
    for (int i = 0; i < 2; ++i) {
        actors[i] = side.GetCurGroup()->GetObjectByN(i)->AsRobot();
        MG_CHECK(session.matches(i + 1, actors[i]));
        actors[i]->m_PosX = 100;
        actors[i]->m_PosY = 360 + i * 40;
        actors[i]->MapPosCalc();
        actors[i]->RChange(MR_Matrix);
        actors[i]->RNeed(MR_Matrix);
        actors[i]->JoinToGroup();
    }
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (object->IsLiveRobot() && object->GetSide() != PLAYER_SIDE) {
            auto *robot = object->AsRobot();
            MG_CHECK(session.matches(3, robot) || session.matches(4, robot));
            robot->m_PosX = session.matches(3, robot) ? 540 : 600;
            robot->m_PosY = 50;
            robot->MapPosCalc();
            robot->RChange(MR_Matrix);
            robot->RNeed(MR_Matrix);
            robot->JoinToGroup();
            robot->SetMaxSpeed(0);
            robot->m_GroupSpeed = 0;
        }
    }
    CMatrixRobotAI *blocker = nullptr;
    Core blocker_core(nullptr, [](SObjectCore *value) { value->Release(); });
    if (blocked) {
        blocker = tests::BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 320, 540);
        blocker->SetGroupLogic(-1);
        blocker->SetMaxSpeed(0);
        blocker->m_GroupSpeed = 0;
        blocker->InitMaxHitpoint(100);
        blocker_core.reset(blocker->GetCore(DEBUG_CALL_INFO));
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {1000, 0, 0};
        g_MatrixMap->m_Difficulty.k_damage_enemy_to_player = 1;
    }
    std::cout << session.metadata("road-ring-v1") << "\nvariant=" << variant_name(variant) << std::endl;
    session.ground_command(500, 280);
    const int group_id = actors[0]->GetGroupLogic();
    MG_CHECK(group_id >= 0 && group_id < MAX_LOGIC_GROUP && actors[1]->GetGroupLogic() == group_id);
    auto &group = side.m_PlayerGroup[group_id];
    MG_CHECK(group.Order() == mpo_MoveTo && group.m_To == CPoint(48, 26));
    MG_CHECK(group.m_RoadPath->m_ListCnt == 0);
    if (guided) {
        // Supply a route through the real regional planner before the units'
        // first tick. Their zone/local paths are never written by the fixture.
        int regions[]{0, 1};
        g_MatrixMap->m_RN.FindPathFromRegionPath(1 << (RUK_CHASSIS_WHEEL - 1), 2, regions, group.m_RoadPath);
        MG_CHECK(group.m_RoadPath->m_ListCnt == 1 && group.m_RoadPath->m_Header[0].m_Cnt == 5);
        MG_CHECK(group.m_RoadPath->m_Header[0].m_Dist == 50);
        MG_CHECK(group.m_RoadPath->m_Units[0].m_Road == nullptr && group.m_RoadPath->m_Units[0].m_Crotch->m_Zone == 0);
        for (int i = 1; i < 5; ++i) {
            const auto &unit = group.m_RoadPath->m_Units[i];
            MG_CHECK(unit.m_Road && unit.m_Road->m_ZoneCnt == 2 && unit.m_Crotch->m_Zone == 16 - i);
        }
    }
    Observation result;
    result.trace.push_back(observe(session, blocker_core.get()));
    while (session.time() < journey_time) {
        if (blocked && session.time() == hit_time) {
            MG_CHECK(blocker_core->m_Object == blocker && blocker->IsLiveRobot());
            MG_CHECK(actors[0]->m_PosX > 120 && actors[0]->m_PosX < 320 && !actors[0]->PLIsInPlace());
            MG_CHECK(result.minimum_separation < 60);
            if (kill) {
                MG_CHECK(blocker->Damage(WEAPON_LASER, blocker->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
                MG_CHECK(!blocker->IsLiveRobot() && blocker->m_CurrState == ROBOT_DIP);
                MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == 1);
            }
        }
        session.advance();
        MG_CHECK(!g_MatrixMap->IsPaused());
        MG_CHECK(group.Order() == mpo_MoveTo && group.m_To == CPoint(48, 26));
        MG_CHECK(group.m_RoadPath->m_ListCnt == int(guided));
        for (int j = 0; j < 2; ++j) {
            MG_CHECK(session.matches(j + 1, actors[j]));
            const auto state = session.robot(j + 1);
            MG_CHECK(state.alive && std::isfinite(state.x) && std::isfinite(state.y));
            MG_CHECK(actors[j]->GetGroupLogic() == group_id);
            MG_CHECK(state.x >= 20 && state.x <= 620 && state.y >= 20 && state.y <= 620);
            MG_CHECK(g_MatrixMap->IsAbsenceWall(RUK_CHASSIS_WHEEL - 1, 1,
                                              int(state.x / GLOBAL_SCALE_MOVE), int(state.y / GLOBAL_SCALE_MOVE)));
            const int zone = g_MatrixMap->MoveGet(actors[j]->GetMapPosX(), actors[j]->GetMapPosY())->m_Zone;
            MG_CHECK(zone >= 0 && zone < 16);
            result.zones[j] |= 1u << zone;
            if (blocked && blocker_core->m_Object && blocker->IsLiveRobot())
                result.minimum_separation = std::min(result.minimum_separation,
                        std::hypot(state.x - blocker->m_PosX, state.y - blocker->m_PosY));
            if (!result.crossed[j] && state.x > 360)
                result.crossed[j] = session.time();
            if (!result.arrived[j] && state.in_place && state.x == 500 && state.y == 280 - j * 40)
                result.arrived[j] = session.time();
            if (session.time() >= settled_time)
                MG_CHECK(state.in_place && state.x == 500 && state.y == 280 - j * 40 &&
                         actors[j]->GetOrdersInPool() == 0);
        }
        MG_CHECK(session.robot(3).x == 540 && session.robot(3).y == 50);
        MG_CHECK(session.robot(4).x == 600 && session.robot(4).y == 50);
        if (blocked) {
            if (blocker_core->m_Object) {
                MG_CHECK(blocker_core->m_Object == blocker);
                MG_CHECK(blocker->IsLiveRobot() == (!kill || session.time() <= hit_time));
            }
            else if (!result.retired)
                result.retired = session.time();
        }
        for (int i = 0; i < 16; ++i)
            MG_CHECK(!g_MatrixMap->m_RN.m_Zone[i].m_Access);
        result.trace.push_back(observe(session, blocker_core.get()));
    }
    // Compare corridor interiors; adjacent endpoint zones can be entered during
    // formation settling. Exact per-frame positions are checked by replay below.
    const unsigned required = guided ? 0x7001u : 0x0039u;
    const unsigned forbidden = guided ? 0x0038u : 0x7000u;
    for (const unsigned visited : result.zones) {
        MG_CHECK((visited & required) == required);
        MG_CHECK((visited & forbidden) == 0);
    }
    for (int i = 0; i < 2; ++i)
        MG_CHECK(result.arrived[i] > 0 && result.arrived[i] < settled_time);
    MG_CHECK(side.GetStatValue(STAT_ROBOT_KILL) == 0);
    MG_CHECK(g_MatrixMap->GetSideById(2)->GetStatValue(STAT_ROBOT_KILL) == int(kill));
    if (kill)
        MG_CHECK(!blocker_core->m_Object && result.retired > hit_time && result.retired < journey_time);
    else if (blocked)
        MG_CHECK(blocker_core->m_Object == blocker && result.retired == 0);
    std::cout << "variant=" << variant_name(variant) << " zone_bits=" << result.zones[0] << ',' << result.zones[1]
        << " crossed=" << result.crossed[0] << ',' << result.crossed[1] << " arrived=" << result.arrived[0] << ','
        << result.arrived[1] << " min_separation=" << result.minimum_separation
        << " retired=" << result.retired << '\n';
    return result;
}

template<random::Mode Mode, int Step>
void road_journeys() {
    const auto ordinary = journey(Mode, Step, Variant::Unguided);
    MG_CHECK(ordinary == journey(Mode, Step, Variant::Unguided));
    const auto guided = journey(Mode, Step, Variant::Guided);
    MG_CHECK(guided == journey(Mode, Step, Variant::Guided));
    MG_CHECK(ordinary.zones != guided.zones && ordinary.trace != guided.trace);
}
template<random::Mode Mode, int Step>
void road_obstruction() {
    const auto alive = journey(Mode, Step, Variant::LiveBlocker);
    MG_CHECK(alive == journey(Mode, Step, Variant::LiveBlocker));
    const auto killed = journey(Mode, Step, Variant::DestroyedBlocker);
    MG_CHECK(killed == journey(Mode, Step, Variant::DestroyedBlocker));
    const size_t prefix = hit_time / Step + 1;
    MG_CHECK(std::equal(alive.trace.begin(), alive.trace.begin() + prefix, killed.trace.begin()));
    MG_CHECK(alive.trace != killed.trace);
    for (int i = 0; i < 2; ++i) {
        MG_CHECK(killed.crossed[i] > hit_time && killed.crossed[i] < alive.crossed[i]);
        MG_CHECK(killed.arrived[i] < alive.arrived[i]);
    }
}
constexpr tests::Case cases[]{
    {"game.navigation.road_guided_journey.park_miller10", road_journeys<random::Mode::ParkMiller, 10>},
    {"game.navigation.road_guided_journey.park_miller16", road_journeys<random::Mode::ParkMiller, 16>},
    {"game.navigation.road_guided_journey.legacy10", road_journeys<random::Mode::LegacyCRT, 10>},
    {"game.navigation.road_guided_journey.legacy16", road_journeys<random::Mode::LegacyCRT, 16>},
    {"game.navigation.road_destroyed_blocker.park_miller10", road_obstruction<random::Mode::ParkMiller, 10>},
    {"game.navigation.road_destroyed_blocker.park_miller16", road_obstruction<random::Mode::ParkMiller, 16>},
    {"game.navigation.road_destroyed_blocker.legacy10", road_obstruction<random::Mode::LegacyCRT, 10>},
    {"game.navigation.road_destroyed_blocker.legacy16", road_obstruction<random::Mode::LegacyCRT, 16>},
};
}  // namespace

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    const int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr)
        return EXIT_FAILURE;
#endif
    Base::CMain::BaseDeInit();
    return result;
}
