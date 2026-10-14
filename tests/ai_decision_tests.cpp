// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_geometry.hpp"
#include "test_support.hpp"
#include "command_world.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObjectBuilding.hpp"
#include "Logic/MatrixEnvironment.h"
#include "Logic/MatrixRule.h"
#include <array>
#include <vector>

namespace tests::ai {
void automatic_reinforcement_designs();
void automatic_reinforcement_buildings();
void automatic_reinforcement_choices();
void automatic_reinforcement_delivery();
void capture_guard_interruption();
void capture_guard_filters();
void visual_acquisition_range();
void visual_enemy_range();
void visual_filters();
void visual_obstruction();
void visual_retirement_range();
void armed_friendly_line();
void armed_target_retention();
void repair_acquisition_range();
void repair_target_retention();
void repair_explicit_priority();
void memory_cleanup();
void memory_clear_correctness();
void place_bounds();
void place_axis_correctness();
void regroup_split_correctness();
void regroup_merge_correctness();
void bomb_escape_planning();
void strategic_member_loss();
void strategic_base_loss();
void strategic_forward();
void strategic_objective_replacement();
void strategic_objective_ties();
void strategic_empty_team_split();
void uneven_team_correctness();
void radio_group();
void radio_team_region();
void radio_filters();
void threat_priorities();
void automatic_attack_route();
void reused_route_distance();
void automatic_capture_route();
void automatic_defence_route();
void production_wait_choice();
void production_wait_limits();
void production_unaffordable();
}

namespace {
using namespace tests;
using namespace tests::battle;

CMatrixRobotAI *robot(float x, float y) {
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic())
        if (object->IsRobot() && object->AsRobot()->m_PosX == x && object->AsRobot()->m_PosY == y)
            return object->AsRobot();
    throw std::runtime_error("fixture robot not found");
}

void enemy_memory() {
    Session session;
    auto *owner = robot(100, 200);
    auto *first = robot(500, 500), *second = robot(540, 540);
    auto &env = *owner->GetEnv();
    MG_CHECK(env.GetEnemyCnt() == 0 && env.TargetType() == 0);
    env.AddToListSlowly(first);
    env.AddToListSlowly(second);
    env.AddToListSlowly(first);
    MG_CHECK(env.GetEnemyCnt() == 2);
    MG_CHECK(env.GetEnemyByKind(ENEMY_ANY) == first);
    env.m_Target = first; env.m_TargetAttack = first;
    MG_CHECK(env.TargetType() == 1);
    env.RemoveFromListSlowly(first);
    env.RemoveFromListSlowly(first);
    MG_CHECK(env.SearchEnemy(first) && env.SearchEnemy(first)->m_DelSlowly == 2);
    env.AddToListSlowly(first);
    MG_CHECK(env.SearchEnemy(first)->m_DelSlowly == 0);
    env.RemoveFromListSlowly(first);
    env.RemoveFromListSlowly(first);
    env.RemoveFromListSlowly(first);
    MG_CHECK(!env.SearchEnemy(first) && env.GetEnemyCnt() == 1);
    MG_CHECK(!env.m_Target && !env.m_TargetAttack);
    MG_CHECK(env.GetEnemyByKind(ENEMY_ANY) == second);
    env.RemoveFromList(second);
    MG_CHECK(env.GetEnemyCnt() == 0 && !env.m_FirstEnemy && !env.m_LastEnemy);
}

void memory_target_retirement() {
    Session session;
    auto &env = *robot(100, 200)->GetEnv();
    auto *target = robot(500, 500);
    env.AddToList(target);
    env.m_Target = target; env.m_TargetAttack = target;
    MG_CHECK(session.remove(3));
    MG_CHECK(env.GetEnemyCnt() == 0 && !env.m_FirstEnemy && !env.m_LastEnemy);
    MG_CHECK(!env.m_Target && !env.m_TargetAttack);
}

void ignore_expiry() {
    Session session;
    // A standalone CInfo observes map time without GatherInfo rewriting its list.
    CInfo env;
    auto *target = robot(500, 500);
    env.AddIgnore(target);
    MG_CHECK(env.IsIgnore(target));
    for (int i = 0; i < 499; ++i) session.advance();
    MG_CHECK(session.time() == 4990 && env.IsIgnore(target));
    session.advance();
    MG_CHECK(session.time() == 5000 && !env.IsIgnore(target));
    env.AddIgnore(target);
    MG_CHECK(env.IsIgnore(target) && env.m_IgnoreCnt == 1);
    session.advance();
    env.AddIgnore(target);
    MG_CHECK(env.m_IgnoreTime[0] == 5010);
    env.AddToListSlowly(target);
    MG_CHECK(!env.IsIgnore(target) && env.SearchEnemy(target));
    env.RemoveFromList(target);
}

void bad_positions() {
    CInfo env;
    for (int i = 0; i < 8; ++i) env.AddBadPlace(i);
    env.AddBadPlace(3);
    MG_CHECK(env.m_BadPlaceCnt == 8);
    for (int i = 0; i < 8; ++i) MG_CHECK(env.IsBadPlace(i));
    MG_CHECK(!env.IsBadPlace(8));
    for (int i = 0; i < 16; ++i) env.AddBadCoord(CPoint(i, i * 2));
    env.AddBadCoord(CPoint(3, 6));
    MG_CHECK(env.m_BadCoordCnt == 16);
    MG_CHECK(env.IsBadCoord(CPoint(3, 6)));
    MG_CHECK(!env.IsBadCoord(CPoint(6, 3)));
    // Full-capacity eviction uses overlapping CopyMemory; it needs separate defect handling.
}

void regroup_thresholds() {
    for (float distance : {299.0f, 300.0f, 400.0f, 401.0f}) {
        for (bool initially_together : {false, true}) {
            Session session;
            auto *a = robot(100, 200), *b = robot(100, 260);
            // Initial placement only; Regroup reads world positions, without advancing movement.
            b->m_PosX = a->m_PosX + distance; b->m_PosY = a->m_PosY;
            b->MapPosCalc();
            a->SetTeam(0); b->SetTeam(0);
            a->SetGroupLogic(0); b->SetGroupLogic(initially_together ? 0 : 1);
            auto &side = *g_MatrixMap->GetPlayerSide();
            side.Regroup();
            const bool together = initially_together ? distance <= 400 : distance < 300;
            MG_CHECK((a->GetGroupLogic() == b->GetGroupLogic()) == together);
            MG_CHECK(a->GetTeam() == 0 && b->GetTeam() == 0);
            std::cout << "regroup distance=" << distance << " initially_together=" << initially_together
                      << " groups=" << a->GetGroupLogic() << ',' << b->GetGroupLogic() << '\n';
        }
    }
}

void regroup_teams_and_actions() {
    Session session;
    auto *a = robot(100, 200), *b = robot(100, 260);
    a->SetTeam(0); b->SetTeam(1);
    a->SetGroupLogic(0); b->SetGroupLogic(1);
    auto &side = *g_MatrixMap->GetPlayerSide();
    side.Regroup();
    MG_CHECK(a->GetGroupLogic() != b->GetGroupLogic());
    b->SetTeam(0);
    side.m_LogicGroup[a->GetGroupLogic()].m_Action.m_Type = mlat_None;
    side.m_LogicGroup[b->GetGroupLogic()].m_Action.m_Type = mlat_Attack;
    side.m_LogicGroup[b->GetGroupLogic()].m_Action.m_Region = 0;
    side.Regroup();
    MG_CHECK(a->GetGroupLogic() == b->GetGroupLogic());
    MG_CHECK(side.m_LogicGroup[a->GetGroupLogic()].m_Action.m_Type == mlat_Attack);
    MG_CHECK(side.m_LogicGroup[a->GetGroupLogic()].m_Action.m_Region == 0);
}

SMatrixLogicAction action(EMatrixLogicActionType type, int path = 0) {
    SMatrixLogicAction a{};
    a.m_Type = type; a.m_Region = 0; a.m_RegionPathCnt = path;
    return a;
}

void action_priorities() {
    Session session;
    auto &side = *g_MatrixMap->GetSideById(2);
    side.TaktHL(); // Allocate and populate real region observations.
    auto &team = side.m_Team[0];
    team.SetWar(false); team.SetWaitUnion(false); team.m_RegionNearDanger = -1;
    for (const auto &pair : {std::pair{mlat_Attack, mlat_Forward}, {mlat_Attack, mlat_Capture},
                            {mlat_Attack, mlat_Intercept}, {mlat_Capture, mlat_Forward},
                            {mlat_Retreat, mlat_Capture}, {mlat_Retreat, mlat_Intercept}}) {
        auto preferred = action(pair.first), other = action(pair.second);
        MG_CHECK(side.CompareAction(0, &preferred, &other) > 0);
        MG_CHECK(side.CompareAction(0, &other, &preferred) < 0);
    }
    auto defend = action(mlat_Defence), attack = action(mlat_Attack);
    MG_CHECK(side.CompareAction(0, &attack, &defend) > 0);
    team.SetWaitUnion(true);
    MG_CHECK(side.CompareAction(0, &defend, &attack) > 0);
    team.SetWar(true);
    MG_CHECK(side.CompareAction(0, &attack, &defend) > 0);
    auto nearer = action(mlat_Forward, 2), farther = action(mlat_Forward, 3);
    MG_CHECK(side.CompareAction(0, &nearer, &farther) > 0);
}

void action_ties() {
    Session session;
    auto &side = *g_MatrixMap->GetSideById(2);
    auto &team = side.m_Team[0];
    auto a = action(mlat_Attack, 2), b = action(mlat_Attack, 3);
    MG_CHECK(side.CompareAction(0, &a, &b) == 0);
    team.m_ActionList[0] = a; team.m_ActionList[1] = b; team.m_ActionCnt = 2;
    side.BestAction(0);
    MG_CHECK(team.m_ActionCnt == 1 && team.m_ActionList[0].m_RegionPathCnt == 2);
    team.m_ActionList[0] = b; team.m_ActionList[1] = a; team.m_ActionCnt = 2;
    side.BestAction(0);
    MG_CHECK(team.m_ActionCnt == 1 && team.m_ActionList[0].m_RegionPathCnt == 3);
    team.m_ActionCnt = 0;
    side.BestAction(0);
    MG_CHECK(team.m_ActionCnt == 0);
}

void tactical_target_choice() {
    for (bool reverse : {false, true}) {
        Session session;
        auto *owner = robot(100, 200);
        auto *east = BattleGeometry::create_robot(*g_MatrixMap, 2, 200, 200);
        auto *north = BattleGeometry::create_robot(*g_MatrixMap, 2, 100, 100);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto &env = *owner->GetEnv();
        env.AddToList(reverse ? north : east);
        env.AddToList(reverse ? east : north);
        const int group = owner->GetGroupLogic();
        MG_CHECK(!side.FirePL(group)); // Selection runs, but the chassis has no weapon.
        MG_CHECK(env.m_TargetAttack == (reverse ? north : east));
        std::cout << session.metadata() << '\n';
        std::cout << "{\"decision\":\"unarmed-target-choice\",\"trigger\":\"equal-distance-candidates\","
                  << "\"player_intent\":\"MoveTo\",\"selected_target\":\"" << (reverse ? "north" : "east")
                  << "\",\"can_fire\":false}\n";
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_MoveTo);
        auto *retired = env.m_TargetAttack;
        g_MatrixMap->StaticDelete(retired);
        MG_CHECK(!env.m_TargetAttack);
        MG_CHECK(!side.FirePL(group));
        MG_CHECK(env.m_TargetAttack == (reverse ? east : north));
        std::cout << "{\"decision\":\"unarmed-target-choice\",\"trigger\":\"selected-target-retired\","
                  << "\"player_intent\":\"MoveTo\",\"selected_target\":\"" << (reverse ? "east" : "north")
                  << "\",\"can_fire\":false}\n";
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_MoveTo);
    }
}

void tactical_blocked_line() {
    Session session;
    auto *owner = robot(100, 200);
    auto *clear = BattleGeometry::create_robot(*g_MatrixMap, 2, 200, 200);
    auto *blocked = BattleGeometry::create_robot(*g_MatrixMap, 2, 100, 300);
    // The original second friendly robot at (100,260) blocks the equal-distance vertical line.
    session.ground_command(500, 300);
    auto &env = *owner->GetEnv();
    env.AddToList(blocked); env.AddToList(clear);
    auto &side = *g_MatrixMap->GetPlayerSide();
    MG_CHECK(!side.FirePL(owner->GetGroupLogic()));
    MG_CHECK(env.m_TargetAttack == clear);
    env.RemoveFromList(clear);
    MG_CHECK(!env.m_TargetAttack);
    MG_CHECK(!side.FirePL(owner->GetGroupLogic()));
    MG_CHECK(env.m_TargetAttack == blocked);
    MG_CHECK(side.m_PlayerGroup[owner->GetGroupLogic()].Order() == mpo_MoveTo);
}

// The percentage is map input, protected in production. This subclass changes
// setup only and is destroyed through its exact type, not through a base pointer.
struct AutoMap : CMatrixMapLogic {
    void maintenance(int percent, int remaining) {
        m_MaintenancePRC = percent;
        m_MaintenanceTime = remaining;
    }
};
struct AutoWorld {
    CHeap heap;
    CBlockPar data;
    EnginePtr<AutoMap> map;
    const DWORD previous_flags = g_Flags;
    AutoWorld() {
        MG_CHECK(!g_MatrixMap && !g_MatrixHeap && !g_MatrixData);
        g_MatrixHeap = &heap; g_MatrixData = &data; g_Flags = 0;
        try {
            CacheInit();
            map = make_engine_object<AutoMap>();
            g_MatrixMap = map.get();
            map->m_Side = HNew(g_MatrixHeap) CMatrixSideUnit;
            map->m_SideCnt = 1;
            map->m_PlayerSide = map->m_Side;
            map->m_PlayerSide->m_Id = PLAYER_SIDE;
            map->m_PlayerSide->SetStatus(SS_ACTIVE);
            map->m_PlayerSide->InitPlayerSide();
        } catch (...) { clear(); throw; }
    }
    ~AutoWorld() { clear(); }
    void clear() {
        map.reset();
#ifdef _DEBUG
        CHelper::ClearAll();
#endif
        CacheDeinit();
        g_MatrixMap = nullptr; g_MatrixHeap = nullptr; g_MatrixData = nullptr;
        g_Flags = previous_flags;
    }
};

void automatic_reinforcement_gate() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        AutoWorld world;
        auto &map = *world.map;
        auto &side = *map.GetPlayerSide();
        random::seed(79, mode);
        map.maintenance(0, 0);
        SETFLAG(map.m_Flags, MMFLAG_AUTOMATIC_MODE);
        for (int i = 0; i < 20; ++i) side.LogicTakt(10);
        MG_CHECK(random::draw_count() == 0);
        map.maintenance(100, 1);
        for (int i = 0; i < 20; ++i) side.LogicTakt(10);
        MG_CHECK(random::draw_count() == 0);
        map.maintenance(100, 0);
        RESETFLAG(map.m_Flags, MMFLAG_AUTOMATIC_MODE);
        for (int i = 0; i < 20; ++i) side.LogicTakt(10);
        MG_CHECK(random::draw_count() == 0);
        // In both modes seed 79 enters the probabilistic scan on its first draw.
        MG_CHECK(FRND(1) < .05f);
        random::seed(79, mode);
        SETFLAG(map.m_Flags, MMFLAG_AUTOMATIC_MODE);
        for (int i = 0; i < 64; ++i) {
            side.LogicTakt(10);
            MG_CHECK(random::draw_count() == static_cast<unsigned>(i + 1));
            MG_CHECK(map.m_AD_Obj_cnt == 0);
        }
        SETFLAG(map.m_Flags, MMFLAG_FULLAUTO);
        side.LogicTakt(10);
        MG_CHECK(random::draw_count() == 65);
        RESETFLAG(map.m_Flags, MMFLAG_AUTOMATIC_MODE);
        side.LogicTakt(10);
        MG_CHECK(random::draw_count() == 65);
        MG_CHECK(map.BeforeMaintenanceTime() == 0);
    }
}

void strategic_tick() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        Profile p; p.mode = mode;
        Session session(p);
        auto &enemy = *g_MatrixMap->GetSideById(2);
        auto *third = robot(500, 500), *fourth = robot(540, 540);
        for (int i = 0; i < 20; ++i) session.advance();
        MG_CHECK(third->GetTeam() == 0 && fourth->GetTeam() == 0);
        MG_CHECK(third->GetGroupLogic() == fourth->GetGroupLogic());
        const auto &group = enemy.m_LogicGroup[third->GetGroupLogic()];
        std::cout << session.metadata() << '\n';
        std::cout << "computer action=" << int(group.m_Action.m_Type) << " region=" << group.m_Action.m_Region
                  << " team_action=" << int(enemy.m_Team[0].m_Action.m_Type) << '\n';
        MG_CHECK(group.RobotsCnt() == 2);
        MG_CHECK(enemy.m_Team[0].m_RobotCnt == 2);
        MG_CHECK(group.m_Action.m_Region == 0);
        MG_CHECK(group.m_Action.m_Type == mlat_Defence);
        CPoint destination;
        MG_CHECK(third->GetMoveToCoords(destination));
        MG_CHECK(destination == CPoint(8, 8));
        MG_CHECK(fourth->GetMoveToCoords(destination));
        MG_CHECK(destination == CPoint(48, 8));
    }
}

void interruptibility() {
    Session session;
    auto *player = robot(100, 200), *computer = robot(500, 500);
    auto *building = g_MatrixMap->StaticAdd<CMatrixBuilding>();
    MG_CHECK(player->CanBreakOrder() && computer->CanBreakOrder());
    player->CaptureFactory(building); computer->CaptureFactory(building);
    MG_CHECK(player->CanBreakOrder());
    MG_CHECK(!computer->CanBreakOrder());
    SETFLAG(g_MatrixMap->m_Flags, MMFLAG_AUTOMATIC_MODE);
    MG_CHECK(player->CanBreakOrder());
    SETFLAG(g_MatrixMap->m_Flags, MMFLAG_FULLAUTO);
    MG_CHECK(!player->CanBreakOrder());
    player->RemoveOrder(ROT_CAPTURE_FACTORY); computer->RemoveOrder(ROT_CAPTURE_FACTORY);
    MG_CHECK(player->CanBreakOrder() && computer->CanBreakOrder());
    for (auto state : {ROBOT_IN_SPAWN, ROBOT_BASE_MOVEOUT, ROBOT_BASE_CAPTURE}) {
        player->m_CurrState = state;
        MG_CHECK(!player->CanBreakOrder());
    }
    player->m_CurrState = ROBOT_SUCCESSFULLY_BUILD;
    MG_CHECK(player->CanBreakOrder());
}
}
int main(int argc, char **argv) {
    const tests::Case cases[] = {
        {"game.ai.automatic_reinforcement_designs", tests::ai::automatic_reinforcement_designs},
        {"game.ai.automatic_reinforcement_buildings", tests::ai::automatic_reinforcement_buildings},
        {"game.ai.automatic_reinforcement_choices", tests::ai::automatic_reinforcement_choices},
        {"game.ai.automatic_reinforcement_delivery", tests::ai::automatic_reinforcement_delivery},
        {"game.ai.capture_guard_interruption", tests::ai::capture_guard_interruption},
        {"game.ai.capture_guard_filters", tests::ai::capture_guard_filters},
        {"game.ai.visual_acquisition_range", tests::ai::visual_acquisition_range},
        {"game.ai.visual_enemy_range", tests::ai::visual_enemy_range},
        {"game.ai.visual_filters", tests::ai::visual_filters},
        {"game.ai.visual_obstruction", tests::ai::visual_obstruction},
        {"game.ai.visual_retirement_range", tests::ai::visual_retirement_range},
        {"game.ai.armed_friendly_line", tests::ai::armed_friendly_line},
        {"game.ai.armed_target_retention", tests::ai::armed_target_retention},
        {"game.ai.repair_acquisition_range", tests::ai::repair_acquisition_range},
        {"game.ai.repair_target_retention", tests::ai::repair_target_retention},
        {"game.ai.repair_explicit_priority", tests::ai::repair_explicit_priority},
        {"game.ai.memory_cleanup", tests::ai::memory_cleanup},
        {"game.ai.place_bounds", tests::ai::place_bounds},
        {"game.ai.bomb_escape_planning", tests::ai::bomb_escape_planning},
        // Correctness regression pending https://github.com/pozitronik/MatrixGame/issues/137
        {"probe.ai.memory_clear_reuse", tests::ai::memory_clear_correctness},
        {"game.ai.place_axis", tests::ai::place_axis_correctness},
        // Correctness regression pending https://github.com/pozitronik/MatrixGame/issues/140
        {"probe.ai.regroup_split_counts", tests::ai::regroup_split_correctness},
        // Correctness regression pending https://github.com/pozitronik/MatrixGame/issues/140
        {"probe.ai.regroup_merge_counts", tests::ai::regroup_merge_correctness},
        {"game.ai.enemy_memory", enemy_memory},
        {"game.ai.memory_target_retirement", memory_target_retirement},
        {"game.ai.ignore_expiry", ignore_expiry},
        {"game.ai.bad_positions", bad_positions},
        {"game.ai.regroup_thresholds", regroup_thresholds},
        {"game.ai.regroup_teams_and_actions", regroup_teams_and_actions},
        {"game.ai.action_priorities", action_priorities},
        {"game.ai.action_ties", action_ties},
        {"game.ai.interruptibility", interruptibility},
        {"game.ai.automatic_reinforcement_gate", automatic_reinforcement_gate},
        {"game.ai.strategic_tick", strategic_tick},
        {"game.ai.tactical_target_choice", tactical_target_choice},
        {"game.ai.tactical_blocked_line", tactical_blocked_line},
        {"game.ai.strategic_member_loss", tests::ai::strategic_member_loss},
        {"game.ai.strategic_base_loss", tests::ai::strategic_base_loss},
        {"game.ai.strategic_forward", tests::ai::strategic_forward},
        {"game.ai.strategic_objective_replacement", tests::ai::strategic_objective_replacement},
        {"game.ai.strategic_objective_ties", tests::ai::strategic_objective_ties},
        {"game.ai.strategic_empty_team_split", tests::ai::strategic_empty_team_split},
        // Correctness regression pending https://github.com/pozitronik/MatrixGame/issues/135
        {"probe.ai.uneven_team_redistribution", tests::ai::uneven_team_correctness},
        {"game.ai.radio_group", tests::ai::radio_group},
        {"game.ai.radio_team_region", tests::ai::radio_team_region},
        {"game.ai.radio_filters", tests::ai::radio_filters},
        {"game.ai.threat_priorities", tests::ai::threat_priorities},
        {"game.ai.automatic_attack_route", tests::ai::automatic_attack_route},
        // Opt-in correctness probe; deliberately absent from CTest until fixed.
        // Correctness regression pending https://github.com/pozitronik/MatrixGame/issues/138
        {"probe.ai.reused_route_distance", tests::ai::reused_route_distance},
        {"game.ai.automatic_capture_route", tests::ai::automatic_capture_route},
        {"game.ai.automatic_defence_route", tests::ai::automatic_defence_route},
        {"game.ai.production_wait_choice", tests::ai::production_wait_choice},
        {"game.ai.production_wait_limits", tests::ai::production_wait_limits},
        {"game.ai.production_unaffordable", tests::ai::production_unaffordable},
    };
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    Base::CMain::BaseInit();
    SetUnhandledExceptionFilter(nullptr);
    std::set_terminate([] { std::abort(); });
    int result = tests::run(argc, argv, cases);
#ifdef MEM_SPY_ENABLE
    if (Base::SMemHeader::first_mem_block != nullptr) {
        std::cerr << "AI decision scenario leaked tracked allocations.\n";
        result = EXIT_FAILURE;
    }
#endif
    Base::CMain::BaseDeInit();
    return result;
}
