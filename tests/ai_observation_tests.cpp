// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_geometry.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObjectCannon.hpp"
#include "MatrixObjectBuilding.hpp"
#include "Interface/CConstructor.h"
#include <algorithm>
#include <array>
#include "Logic/MatrixEnvironment.h"

namespace tests::ai {
namespace {
using namespace battle;

CMatrixRobotAI *robot(float x, float y) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot() && object->AsRobot()->m_PosX == x && object->AsRobot()->m_PosY == y)
            return object->AsRobot();
    throw std::runtime_error("fixture robot not found");
}

// Partition the existing synthetic map before any side has allocated observations.
// These tests query regions; they do not run movement across this boundary.
void two_regions() {
    auto &map = *g_MatrixMap;
    auto &road = map.m_RN;
    MG_CHECK(road.m_RegionCnt == 1 && road.AddRegion() == 1);
    road.m_Region[1] = {};
    road.SetZoneCntMax(2);
    road.m_ZoneCnt = 2;
    for (int i = 0; i < 2; ++i) {
        auto &zone = road.m_Zone[i];
        zone = {};
        zone.m_Region = i;
        zone.m_Rect = CRect(i ? 32 : 2, 2, i ? 62 : 32, 62);
        zone.m_Center = CPoint(i ? 48 : 16, 32);
        zone.m_PlaceCnt = 2;
        zone.m_Place[0] = i;
        zone.m_Place[1] = i + 2;
        auto &region = road.m_Region[i];
        region.m_Center = zone.m_Center;
        region.m_PlaceCnt = region.m_PlaceAllCnt = 2;
        region.m_Place[0] = region.m_PlaceAll[0] = i;
        region.m_Place[1] = region.m_PlaceAll[1] = i + 2;
        region.m_NearCnt = 1;
        region.m_Near[0] = 1 - i;
    }
    for (int i = 0; i < 4; ++i) road.m_Place[i].m_Region = i % 2;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x)
        map.MoveGet(x, y)->m_Zone = x < 32 ? 0 : 1;
}

void initial_position(CMatrixRobotAI &robot, float x, float y) {
    robot.m_PosX = x; robot.m_PosY = y;
    robot.MapPosCalc();
}

void prefer(CMatrixSideUnit &side, EMatrixLogicActionType first, EMatrixLogicActionType second,
            int first_region = 0, int second_region = 0) {
    SMatrixLogicAction a{}, b{};
    a.m_Type = first; a.m_Region = first_region;
    b.m_Type = second; b.m_Region = second_region;
    MG_CHECK(side.CompareAction(0, &a, &b) > 0);
    MG_CHECK(side.CompareAction(0, &b, &a) < 0);
    std::cout << "{\"decision\":\"threat-ranking\",\"trigger\":\"configured-threat\",\"team_strength\":"
              << side.m_Team[0].m_Strength << ",\"war\":" << side.m_Team[0].IsWar()
              << ",\"preferred_action\":" << int(first) << ",\"other_action\":" << int(second) << "}\n";
}

void region_roads() {
    auto &network = g_MatrixMap->m_RN;
    CMatrixCrotch *nodes[4];
    for (int i = 0; i < 4; ++i) {
        nodes[i] = network.AddCrotch();
        nodes[i]->m_Region = i == 0 ? 0 : i == 3 ? 1 : -1;
        nodes[i]->m_Zone = i == 3 ? 1 : 0;
        nodes[i]->m_Center = CPoint(i == 0 ? 16 : i == 3 ? 48 : 32, i == 2 ? 48 : 8);
    }
    for (auto pair : {std::pair{0, 1}, {1, 3}, {0, 2}, {2, 3}}) {
        auto *road = network.AddRoad();
        road->m_Start = nodes[pair.first]; road->m_End = nodes[pair.second];
        road->m_Dist = 10;
        road->m_Start->AddRoad(road); road->m_End->AddRoad(road);
    }
    network.m_Region[0].m_CrotchCnt = network.m_Region[1].m_CrotchCnt = 1;
    network.m_Region[0].m_Crotch[0] = nodes[0];
    network.m_Region[1].m_Crotch[0] = nodes[3];
}

CMatrixBuilding *building(int side, float x, float y, EBuildingType kind = BUILDING_TITAN) {
    auto *object = g_MatrixMap->StaticAdd<CMatrixBuilding>();
    object->m_Side = side; object->m_Pos = D3DXVECTOR2(x, y); object->m_Kind = kind;
    object->AddLT();
    MG_CHECK(object->IsLiveBuilding());
    return object;
}

void check_route(const SMatrixPlayerGroup &group, bool fresh = true) {
    MG_CHECK(group.m_RegionPathCnt == 2);
    MG_CHECK(group.m_RegionPath[0] == 0 && group.m_RegionPath[1] == 1);
    MG_CHECK(group.m_RoadPath->m_ListCnt == 1);
    MG_CHECK(group.m_RoadPath->m_Header[0].m_Cnt == 3);
    const auto *units = group.m_RoadPath->m_Units;
    MG_CHECK(units[0].m_Crotch->m_Region == 0 && units[2].m_Crotch->m_Region == 1);
    MG_CHECK(units[1].m_Road->m_Dist + units[2].m_Road->m_Dist == 20);
    // ClearFast/AddList currently retain the previous distance accumulator.
    // Keep replacement topology covered; accepting that accumulator needs defect disposition.
    if (fresh) MG_CHECK(group.m_RoadPath->m_Header[0].m_Dist == 20);
}

struct ProductionDefinitions {
    ProductionDefinitions() {
        MG_CHECK(!SSpecialBot::m_AIRobotTypeList && SSpecialBot::m_AIRobotTypeCnt == 0);
        std::fill(std::begin(g_Config.m_Price), std::end(g_Config.m_Price), 0);
        std::fill(std::begin(g_Config.m_WeaponStrengthAI), std::end(g_Config.m_WeaponStrengthAI), 0);
        g_Config.m_WeaponStrengthAI[RUK_WEAPON_MACHINEGUN] = 50;
        for (int r = 0; r < MAX_RESOURCES; ++r) {
            g_Config.m_Price[WEAPON1_TITAN + (RUK_WEAPON_MACHINEGUN - 1) * MAX_RESOURCES + r] = 60;
            g_Config.m_Price[WEAPON1_TITAN + (RUK_WEAPON_REPAIR - 1) * MAX_RESOURCES + r] = 60;
            g_Config.m_Timings[r] = 1000;
        }
        std::array<SRobotWeaponMatrix, ROBOT_ARMOR_CNT> capacities{};
        capacities[RUK_ARMOR_MONOSTACK - 1].common = 3;
        CBlockPar input;
        input.ParAdd(L"1", L"P,1,GGR");
        input.ParAdd(L"1", L"P,1,GR");
        SSpecialBot::LoadAIRobotType(input, capacities);
        MG_CHECK(SSpecialBot::m_AIRobotTypeCnt == 2);
        MG_CHECK(SSpecialBot::m_AIRobotTypeList[0].m_Strength == 100);
        MG_CHECK(SSpecialBot::m_AIRobotTypeList[1].m_Strength == 50);
        for (int r = 0; r < MAX_RESOURCES; ++r) {
            MG_CHECK(SSpecialBot::m_AIRobotTypeList[0].m_Resources[r] == 180);
            MG_CHECK(SSpecialBot::m_AIRobotTypeList[1].m_Resources[r] == 120);
        }
    }
    ~ProductionDefinitions() { SSpecialBot::ClearAIRobotType(); }
};

void budget(CMatrixSideUnit &side, int amount) {
    side.SetResourceForceUp(100);
    side.m_WaitResMul = 1;
    for (int r = 0; r < MAX_RESOURCES; ++r) side.SetResourceAmount(static_cast<ERes>(r), amount);
}

void no_purchase(CMatrixSideUnit &side, CMatrixBuilding &base, int amount) {
    MG_CHECK(base.m_BS.GetItemsCnt() == 0);
    MG_CHECK(side.m_BuildRobotLast == -1 && side.m_BuildRobotLast2 == -1 && side.m_BuildRobotLast3 == -1);
    for (int r = 0; r < MAX_RESOURCES; ++r)
        MG_CHECK(side.GetResourcesAmount(static_cast<ERes>(r)) == amount);
    std::cout << "{\"decision\":\"production-budget\",\"trigger\":\"resource-forecast\",\"resource_each\":"
              << amount << ",\"planned_design\":" << side.m_WaitResForBuildRobot
              << ",\"queued\":" << base.m_BS.GetItemsCnt() << ",\"rng_draws\":" << random::draw_count() << "}\n";
}

void objective_record(const char *trigger, const SMatrixPlayerGroup &group) {
    std::cout << "{\"decision\":\"automatic-objective\",\"trigger\":\"" << trigger
              << "\",\"player_intent\":\"MoveTo\",\"has_target\":" << (group.m_Obj ? "true" : "false")
              << ",\"region_path_count\":" << group.m_RegionPathCnt
              << ",\"road_path_count\":" << group.m_RoadPath->m_ListCnt << "}\n";
}
}

void radio_group() {
    for (bool same_group : {false, true}) {
        Session session;
        two_regions();
        auto *owner = robot(100, 200), *sender = robot(100, 260), *enemy = robot(500, 500);
        initial_position(*sender, 500, 260);
        owner->SetTeam(0); sender->SetTeam(1);
        owner->SetGroupLogic(0); sender->SetGroupLogic(same_group ? 0 : 1);
        MG_CHECK(owner->GetRegion() == 0 && sender->GetRegion() == 1);
        sender->GetEnv()->AddToList(enemy);
        const auto draws = random::draw_count();
        owner->GatherInfo(1);
        MG_CHECK(bool(owner->GetEnv()->SearchEnemy(enemy)) == same_group);
        MG_CHECK(owner->GetEnv()->GetEnemyCnt() == (same_group ? 1 : 0));
        MG_CHECK(random::draw_count() == draws);
        std::cout << "radio same_group=" << same_group << " same_team=0 same_region=0 sightings="
                  << owner->GetEnv()->GetEnemyCnt() << '\n';
    }
}

void radio_team_region() {
    for (bool same_team : {false, true}) for (bool same_region : {false, true}) {
        Session session;
        two_regions();
        auto *owner = robot(100, 200), *sender = robot(100, 260), *enemy = robot(500, 500);
        initial_position(*sender, same_region ? 100.0f : 500.0f, 260);
        owner->SetGroupLogic(0); sender->SetGroupLogic(1);
        owner->SetTeam(0); sender->SetTeam(same_team ? 0 : 1);
        sender->GetEnv()->AddToList(enemy);
        owner->GatherInfo(1);
        MG_CHECK(bool(owner->GetEnv()->SearchEnemy(enemy)) == (same_team && same_region));
        MG_CHECK(owner->GetEnv()->GetEnemyCnt() == (same_team && same_region ? 1 : 0));
        std::cout << "radio same_group=0 same_team=" << same_team << " same_region=" << same_region
                  << " sightings=" << owner->GetEnv()->GetEnemyCnt() << '\n';
    }
}

void radio_filters() {
    Session session;
    auto *owner = robot(100, 200), *sender = robot(100, 260);
    auto *first = robot(500, 500), *second = robot(540, 540);
    session.ground_command(500, 300);
    const int group = owner->GetGroupLogic();
    MG_CHECK(sender->GetGroupLogic() == group);
    sender->GetEnv()->AddToList(first);
    sender->GetEnv()->AddToList(second);
    sender->GetEnv()->RemoveFromListSlowly(second);
    sender->GetEnv()->AddToList(owner); // Invalid friendly observation must not propagate.
    owner->GetEnv()->AddIgnore(first);
    owner->GatherInfo(1);
    MG_CHECK(owner->GetEnv()->GetEnemyCnt() == 1);
    MG_CHECK(owner->GetEnv()->SearchEnemy(first) && !owner->GetEnv()->IsIgnore(first));
    MG_CHECK(!owner->GetEnv()->SearchEnemy(second) && !owner->GetEnv()->SearchEnemy(owner));
    owner->GetEnv()->RemoveFromListSlowly(first);
    owner->GatherInfo(1);
    MG_CHECK(owner->GetEnv()->GetEnemyCnt() == 1 && owner->GetEnv()->SearchEnemy(first)->m_DelSlowly == 0);
    sender->GetEnv()->AddToListSlowly(second);
    owner->GatherInfo(1);
    MG_CHECK(owner->GetEnv()->GetEnemyCnt() == 2 && owner->GetEnv()->SearchEnemy(second));
    MG_CHECK(g_MatrixMap->GetPlayerSide()->m_PlayerGroup[group].Order() == mpo_MoveTo);
}

void threat_priorities() {
    Session session;
    two_regions();
    auto &side = *g_MatrixMap->GetPlayerSide();
    // A configured turret contributes real strength to the regional observer.
    // No cannon tick, geometry load or firing is needed for this ranking boundary.
    auto *cannon = g_MatrixMap->StaticAdd<CMatrixCannon>();
    cannon->m_Num = 1; cannon->SetSide(2);
    cannon->m_Pos = D3DXVECTOR2(500, 300);
    cannon->InitMaxHitpoint(100);
    g_Config.m_CannonsProps[0].m_Strength = 100;
    cannon->AddLT();
    MG_CHECK(cannon->GetStrength() == 100);
    side.PGCalcStat();
    auto &team = side.m_Team[0];
    team.SetWar(false); team.SetWaitUnion(false); team.m_RegionNearDanger = 1;
    for (float strength : {99.0f, 100.0f, 101.0f}) {
        team.m_Strength = strength;
        if (strength < 100) {
            prefer(side, mlat_Defence, mlat_Attack);
            prefer(side, mlat_Forward, mlat_Defence, 1, 0);
            prefer(side, mlat_Defence, mlat_Capture, 0, 1);
        } else {
            prefer(side, mlat_Attack, mlat_Defence);
            prefer(side, mlat_Defence, mlat_Forward, 0, 1);
            prefer(side, mlat_Capture, mlat_Defence, 1, 0);
        }
    }
    for (float strength : {49.0f, 50.0f, 51.0f}) {
        team.m_Strength = strength;
        if (strength < 50) prefer(side, mlat_Defence, mlat_Retreat);
        else prefer(side, mlat_Retreat, mlat_Defence);
    }
    for (float strength : {79.0f, 81.0f}) {
        team.m_Strength = strength;
        if (strength < 80) {
            prefer(side, mlat_Retreat, mlat_Attack);
            prefer(side, mlat_Retreat, mlat_Forward);
        } else {
            prefer(side, mlat_Attack, mlat_Retreat);
            prefer(side, mlat_Forward, mlat_Retreat);
        }
    }
    team.m_Strength = 0;
    team.SetWar(true);
    prefer(side, mlat_Retreat, mlat_Defence);
    prefer(side, mlat_Capture, mlat_Defence);
    prefer(side, mlat_Forward, mlat_Defence);
}

void automatic_attack_route() {
    for (bool blocked : {false, true}) {
        Session session;
        two_regions(); region_roads();
        const byte restriction = blocked ? (1 << (RUK_CHASSIS_WHEEL - 1)) : 0;
        g_MatrixMap->m_RN.m_Region[0].m_NearMove[0] = restriction;
        g_MatrixMap->m_RN.m_Region[1].m_NearMove[0] = restriction;
        session.ground_command(200, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        const int index = robot(100, 200)->GetGroupLogic();
        auto &group = side.m_PlayerGroup[index];
        MG_CHECK(group.m_Obj == nullptr);
        side.PGFindAttackTarget(index);
        objective_record(blocked ? "attack-route-blocked" : "attack-route-open", group);
        if (blocked) {
            MG_CHECK(group.m_Obj == nullptr && group.m_RegionPathCnt == 0);
            MG_CHECK(group.m_RoadPath->m_ListCnt == 0);
        } else {
            MG_CHECK(group.m_Obj == robot(500, 500));
            check_route(group);
            MG_CHECK(session.remove(3));
            side.TaktPL(index); // Player groups retire raw target references at their next logic pass.
            MG_CHECK(group.m_Obj == nullptr);
            side.PGFindAttackTarget(index);
            MG_CHECK(group.m_Obj == robot(540, 540));
            objective_record("attack-target-retired", group);
            check_route(group, false);
            std::cout << "replacement_route actual_edge_sum=20 cached_distance="
                      << group.m_RoadPath->m_Header[0].m_Dist << '\n';
        }
        MG_CHECK(group.Order() == mpo_MoveTo);
    }
}

void automatic_capture_route() {
    for (bool local : {false, true}) for (bool blocked : {false, true}) {
        Session session;
        two_regions(); region_roads();
        const byte restriction = blocked ? (1 << (RUK_CHASSIS_WHEEL - 1)) : 0;
        g_MatrixMap->m_RN.m_Region[0].m_NearMove[0] = restriction;
        g_MatrixMap->m_RN.m_Region[1].m_NearMove[0] = restriction;
        auto *remote_factory = building(0, 500, 300);
        auto *local_factory = local ? building(2, 200, 300) : nullptr;
        session.ground_command(200, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        const int index = robot(100, 200)->GetGroupLogic();
        auto &group = side.m_PlayerGroup[index];
        side.PGFindCaptureFactory(index);
        objective_record(local ? "local-factory-available" : blocked ? "capture-route-blocked" : "remote-factory-available", group);
        MG_CHECK(group.m_Obj == (local ? local_factory : blocked ? nullptr : remote_factory));
        if (!local && !blocked) check_route(group);
        else MG_CHECK(group.m_RegionPathCnt == 0 && group.m_RoadPath->m_ListCnt == 0);
        MG_CHECK(group.Order() == mpo_MoveTo);
    }
}

void automatic_defence_route() {
    for (bool has_asset : {false, true}) {
        Session session;
        two_regions(); region_roads();
        if (has_asset) building(PLAYER_SIDE, 500, 300);
        session.ground_command(200, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        const int index = robot(100, 200)->GetGroupLogic();
        auto &group = side.m_PlayerGroup[index];
        side.PGFindDefenceTarget(index);
        objective_record(has_asset ? "friendly-asset-threatened" : "no-friendly-asset", group);
        MG_CHECK(group.m_Obj == (has_asset ? robot(500, 500) : nullptr));
        if (has_asset) {
            MG_CHECK(group.m_Region == 1);
            check_route(group);
        } else MG_CHECK(group.m_RegionPathCnt == 0 && group.m_RoadPath->m_ListCnt == 0);
    }
}

void production_wait_choice() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Profile profile; profile.mode = mode;
        Session session(profile);
        ProductionDefinitions definitions;
        auto *base = building(PLAYER_SIDE, 100, 100, BUILDING_BASE);
        auto &side = *g_MatrixMap->GetPlayerSide();
        side.PGCalcStat();
        budget(side, 150); // Weak design is affordable; 10 seconds of income permits the stronger one.
        const auto draws = random::draw_count();
        side.BuildRobot();
        MG_CHECK(side.m_WaitResForBuildRobot == 0);
        MG_CHECK(random::draw_count() == draws + 1);
        no_purchase(side, *base, 150);
        side.BuildRobot();
        MG_CHECK(side.m_WaitResForBuildRobot == 0 && random::draw_count() == draws + 1);
        no_purchase(side, *base, 150);
    }
}

void production_wait_limits() {
    for (int planned : {0, 1}) {
        Session session;
        ProductionDefinitions definitions;
        auto *base = building(PLAYER_SIDE, 100, 100, BUILDING_BASE);
        auto &side = *g_MatrixMap->GetPlayerSide();
        side.PGCalcStat();
        budget(side, 100);
        side.m_WaitResForBuildRobot = planned;
        const auto draws = random::draw_count();
        side.BuildRobot();
        // The weak design needs less than 10 seconds; the stronger design exceeds the wait horizon.
        MG_CHECK(side.m_WaitResForBuildRobot == (planned == 1 ? 1 : -1));
        MG_CHECK(random::draw_count() == draws);
        no_purchase(side, *base, 100);
    }
}

void production_unaffordable() {
    for (int amount : {0, 89, 100}) {
        Session session;
        ProductionDefinitions definitions;
        auto *base = building(PLAYER_SIDE, 100, 100, BUILDING_BASE);
        auto &side = *g_MatrixMap->GetPlayerSide();
        side.PGCalcStat();
        budget(side, amount);
        const auto draws = random::draw_count();
        side.BuildRobot();
        MG_CHECK(side.m_WaitResForBuildRobot == -1);
        MG_CHECK(random::draw_count() == draws);
        no_purchase(side, *base, amount);
    }
}
}
