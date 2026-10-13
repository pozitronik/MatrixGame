// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixObjectBuilding.hpp"
#include "Logic/MatrixEnvironment.h"
#include <array>

// The existing bomb-command fixture uses the same idle production effect.
// No weapon animation, firing, or map tick runs while it is attached.
struct RobotCommandFixture {
    static void equip(CMatrixRobotAI &robot) {
        MG_CHECK(robot.m_WeaponsCnt == 0);
        robot.m_Weapons[0].m_Unit = &robot.m_Unit[0];
        robot.m_Weapons[0].CreateEffect(0, nullptr, WEAPON_BIGBOOM);
        robot.m_WeaponsCnt = 1;
    }
    static void release(CMatrixRobotAI &robot) {
        robot.m_Weapons[0].Release();
        robot.m_WeaponsCnt = 0;
    }
};

namespace tests::ai {
namespace {
using namespace battle;
std::array<CMatrixRobotAI *, 4> units() {
    std::array<CMatrixRobotAI *, 4> result{};
    int count = 0;
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (!object->IsLiveRobot()) continue;
        MG_CHECK(count < 4);
        result[count++] = object->AsRobot();
    }
    MG_CHECK(count == 4);
    return result;
}
void position(CMatrixRobotAI &robot, float x, float y) {
    robot.m_PosX = x; robot.m_PosY = y; robot.MapPosCalc();
}
struct IdleBomb {
    CMatrixRobotAI &robot;
    explicit IdleBomb(CMatrixRobotAI &r) : robot(r) { RobotCommandFixture::equip(robot); }
    ~IdleBomb() { RobotCommandFixture::release(robot); }
};
}

void memory_cleanup() {
    Session session;
    const auto robots = units();
    CInfo env;
    env.AddToList(robots[0]); env.AddToList(robots[1]);
    env.m_Target = robots[0]; env.m_TargetAttack = robots[0];
    env.RemoveFromListSlowly(robots[0]);
    env.RemoveAllSlowely();
    MG_CHECK(env.GetEnemyCnt() == 1 && !env.SearchEnemy(robots[0]) && env.SearchEnemy(robots[1]));
    MG_CHECK(!env.m_Target && !env.m_TargetAttack);
    env.RemoveAllSlowely();
    MG_CHECK(env.GetEnemyCnt() == 1);
    env.m_Target = robots[1]; env.m_TargetAttack = robots[1];
    env.Reset();
    MG_CHECK(!env.m_Target && !env.m_TargetAttack && env.TargetType() == 0);
    MG_CHECK(env.GetEnemyCnt() == 1 && env.SearchEnemy(robots[1]));

    auto *first = g_MatrixMap->StaticAdd<CMatrixBuilding>();
    auto *second = g_MatrixMap->StaticAdd<CMatrixBuilding>();
    env.AddToList(first); env.AddToList(second);
    env.m_Target = first; env.m_TargetAttack = second;
    env.RemoveAllBuilding(second);
    MG_CHECK(env.GetEnemyCnt() == 2 && !env.SearchEnemy(first) && env.SearchEnemy(second));
    MG_CHECK(!env.m_Target && env.m_TargetAttack == second && env.SearchEnemy(robots[1]));
    env.RemoveAllBuilding(nullptr);
    MG_CHECK(env.GetEnemyCnt() == 1 && !env.m_TargetAttack && env.SearchEnemy(robots[1]));
}

void memory_clear_correctness() {
    Session session;
    CInfo env;
    env.AddToList(units()[0]);
    env.Clear();
    MG_CHECK(!env.m_FirstEnemy && !env.m_LastEnemy);
    std::cout << "cleared list count=" << env.GetEnemyCnt() << '\n';
    MG_CHECK(env.GetEnemyCnt() == 0);
    env.AddToList(units()[1]);
    MG_CHECK(env.GetEnemyCnt() == 1);
}

void place_bounds() {
    Session session;
    auto &map = *g_MatrixMap;
    MG_CHECK(map.m_RN.m_Place[0].m_Pos == CPoint(8, 8));
    MG_CHECK(map.FindPlace(CPoint(8, 8)) == 0);
    MG_CHECK(map.FindPlace(CPoint(11, 11)) == 0);
    MG_CHECK(map.FindPlace(CPoint(12, 11)) == -1);
    MG_CHECK(map.FindPlace(CPoint(11, 12)) == -1);
    MG_CHECK(map.FindPlace(CPoint(-1, 8)) == -1);
    MG_CHECK(map.FindPlace(CPoint(map.m_SizeMove.x, 8)) == -1);
}

void place_axis_correctness() {
    Session session;
    auto &map = *g_MatrixMap;
    MG_CHECK(map.m_RN.m_Place[1].m_Pos == CPoint(48, 8));
    MG_CHECK(map.m_RN.m_Place[2].m_Pos == CPoint(8, 48));
    const int inside = map.FindPlace(CPoint(48, 8));
    const int outside = map.FindPlace(CPoint(20, 48));
    std::cout << "place inside=" << inside << " outside=" << outside << '\n';
    MG_CHECK(inside == 1 && outside == -1);
}

void regroup_counts_correctness(bool split) {
    Session session;
    auto robots = units();
    auto *a = robots[0], *b = robots[1];
    MG_CHECK(a->GetSide() == b->GetSide());
    position(*a, 100, 200); position(*b, split ? 501 : 200, 200);
    a->SetTeam(0); b->SetTeam(0);
    a->SetGroupLogic(0); b->SetGroupLogic(split ? 0 : 1);
    auto &side = *g_MatrixMap->GetSideById(a->GetSide());
    side.Regroup();
    MG_CHECK((a->GetGroupLogic() != b->GetGroupLogic()) == split);
    std::array<int, MAX_LOGIC_GROUP> actual{};
    for (auto *robot : robots) {
        if (robot->GetSide() != side.m_Id) continue;
        MG_CHECK(robot->GetGroupLogic() >= 0 && robot->GetGroupLogic() < MAX_LOGIC_GROUP);
        ++actual[robot->GetGroupLogic()];
    }
    for (int i = 0; i < MAX_LOGIC_GROUP; ++i) {
        if (actual[i] != side.m_LogicGroup[i].RobotsCnt())
            std::cout << "group=" << i << " actual=" << actual[i] << " cached=" << side.m_LogicGroup[i].RobotsCnt() << '\n';
        MG_CHECK(actual[i] == side.m_LogicGroup[i].RobotsCnt());
    }
}
void regroup_split_correctness() { regroup_counts_correctness(true); }
void regroup_merge_correctness() { regroup_counts_correctness(false); }

// Crowded buffer capacity is tracked separately: https://github.com/pozitronik/MatrixGame/issues/136
void bomb_escape_planning() {
    // Each variant starts a fresh world; these are planning inputs, not teleports during movement.
    enum Variant { Manual, Automatic, ChassisBlocked, InsideRadius, AtRadius, OutsideRadius };
    constexpr const char *names[] = {"manual", "automatic", "chassis-blocked", "inside-radius", "at-radius", "outside-radius"};
    for (auto variant : {Manual, Automatic, ChassisBlocked, InsideRadius, AtRadius, OutsideRadius}) {
        std::cout << "bomb planning variant=" << names[variant] << std::endl;
        const bool automatic = variant != Manual;
        Session session;
        auto robots = units();
        auto *cover = robots[0], *escaping = robots[1], *bomber = robots[2];
        MG_CHECK(cover->GetSide() == PLAYER_SIDE && escaping->GetSide() == PLAYER_SIDE);
        position(*cover, 120, 100); position(*escaping, 180, 100); position(*bomber, 100, 100);
        if (variant >= InsideRadius) position(*escaping, 399.0f + float(variant - InsideRadius), 100);
        IdleBomb bomb(*bomber);
        MG_CHECK(bomber->HaveBomb());
        bomber->GetEnv()->AddToList(cover);
        cover->SetGroupLogic(0); escaping->SetGroupLogic(0);
        auto &side = *g_MatrixMap->GetPlayerSide();
        side.m_PlayerGroup[0].Order(automatic ? mpo_AutoAttack : mpo_MoveTo);
        auto &road = g_MatrixMap->m_RN;
        MG_CHECK(road.m_PlaceCnt == 4 && road.m_ZoneCnt == 1);
        MG_CHECK(road.m_Place[0].m_NearCnt == 2);
        MG_CHECK(road.m_Place[0].m_Near[0] == 1 && road.m_Place[0].m_Near[1] == 2);
        MG_CHECK(road.m_Place[1].m_Pos == CPoint(48, 8));
        for (auto *robot : robots) {
            CPoint unused;
            MG_CHECK(robot->GetEnv()->m_Place == -1 && !robot->GetMoveToCoords(unused));
        }
        // There are no occupied-place prefixes. Automatic visits place 0 and
        // enqueues 1/2, then accepts 1: at most three entries in the four-entry
        // PrepareBuf allocation. InsideRadius accepts its first place directly.
        if (variant == ChassisBlocked) {
            auto &start = g_MatrixMap->m_RN.m_Place[0];
            for (int i = 0; i < start.m_NearCnt; ++i)
                start.m_NearMove[i] = 1 << (escaping->m_Unit[0].u1.s1.m_Kind - 1);
        }
        escaping->MoveTo(20, 20);
        const auto draws = random::draw_count();
        side.EscapeFromBomb();
        MG_CHECK(random::draw_count() == draws);
        CPoint destination, previous;
        MG_CHECK(!cover->GetReturnCoords(previous));
        MG_CHECK(escaping->GetMoveToCoords(destination));
        if (!automatic || variant == ChassisBlocked || variant >= AtRadius) {
            MG_CHECK(!escaping->GetReturnCoords(previous) && destination == CPoint(20, 20));
            continue;
        }
        const CPoint expected(48, 8);
        MG_CHECK(escaping->GetReturnCoords(previous) && previous == CPoint(20, 20));
        MG_CHECK(destination == expected);
        side.EscapeFromBomb();
        MG_CHECK(random::draw_count() == draws);
        MG_CHECK(escaping->GetMoveToCoords(destination) && destination == expected);
        MG_CHECK(escaping->GetReturnCoords(previous) && previous == CPoint(20, 20));
        MG_CHECK(session.time() == 0);
    }
}
}
