// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "battle_geometry.hpp"
#include "test_support.hpp"
#include "command_world.hpp"
#include <algorithm>
#include "MatrixRobot.hpp"
#include "MatrixObjectBuilding.hpp"
#include "MatrixObjectCannon.hpp"
#include <algorithm>
#include "Logic/MatrixEnvironment.h"
#include "GroupCounts.hpp"
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
    const auto robots = units();
    for (int repeat = 0; repeat < 3; ++repeat) {
        env.AddToList(robots[0]); env.AddToList(robots[1]);
        MG_CHECK(env.GetEnemyCnt() == 2);
        env.Clear(); env.Clear();
        MG_CHECK(!env.m_FirstEnemy && !env.m_LastEnemy && env.GetEnemyCnt() == 0);
        env.AddToList(robots[2]);
        MG_CHECK(env.GetEnemyCnt() == 1 && env.SearchEnemy(robots[2]));
        env.RemoveFromList(robots[2]);
        MG_CHECK(!env.m_FirstEnemy && !env.m_LastEnemy && env.GetEnemyCnt() == 0);
    }
}

void memory_position_eviction() {
    Session session;
    CInfo env;
    for (int n = 0; n < 40; ++n) {
        const CPoint point(n * 13 + 2, n * -7 - 3);
        env.AddBadPlace(n);
        env.AddBadCoord(point);
        MG_CHECK(env.m_BadPlaceCnt == std::min(n + 1, 8));
        MG_CHECK(env.m_BadCoordCnt == std::min(n + 1, 16));
        // A duplicate must neither evict nor promote the oldest retained record.
        env.AddBadPlace(std::max(0, n - 7));
        const int oldest_coord = std::max(0, n - 15);
        env.AddBadCoord(CPoint(oldest_coord * 13 + 2, oldest_coord * -7 - 3));
        for (int i = 0; i < env.m_BadPlaceCnt; ++i)
            MG_CHECK(env.m_BadPlace[i] == std::max(0, n - 7) + i);
        for (int i = 0; i < env.m_BadCoordCnt; ++i) {
            const int retained = oldest_coord + i;
            MG_CHECK(env.m_BadCoord[i] == CPoint(retained * 13 + 2, retained * -7 - 3));
        }
        if (n >= 8) MG_CHECK(!env.IsBadPlace(n - 8));
        if (n >= 16) MG_CHECK(!env.IsBadCoord(CPoint((n - 16) * 13 + 2, (n - 16) * -7 - 3)));
    }
}

void memory_ignore_eviction() {
    Session session;
    // Retain real objects throughout; only pointer identity is queried. They
    // stay outside the map scheduler and are destroyed after the environment.
    std::array<EnginePtr<CMatrixBuilding>, 33> objects;
    for (auto &object : objects) object = make_engine_object<CMatrixBuilding>();
    CInfo env;
    std::array<int, 33> inserted{};
    for (int n = 0; n < int(objects.size()); ++n) {
        inserted[n] = session.time();
        env.AddIgnore(objects[n].get());
        MG_CHECK(env.m_IgnoreCnt == std::min(n + 1, 16));
        const int oldest = std::max(0, n - 15);
        for (int i = 0; i < env.m_IgnoreCnt; ++i) {
            MG_CHECK(env.m_Ignore[i] == objects[oldest + i].get());
            MG_CHECK(env.m_IgnoreTime[i] == inserted[oldest + i]);
            MG_CHECK(env.IsIgnore(objects[oldest + i].get()));
        }
        if (n >= 16) MG_CHECK(!env.IsIgnore(objects[n - 16].get()));
        session.advance();
    }
    // Duplicates refresh in place even when the array is full.
    env.AddIgnore(objects[17].get());
    MG_CHECK(env.m_IgnoreCnt == 16 && env.m_Ignore[0] == objects[17].get());
    MG_CHECK(env.m_IgnoreTime[0] == session.time());
    while (session.time() < inserted[32] + 4990) session.advance();
    MG_CHECK(session.time() == inserted[32] + 4990 && env.IsIgnore(objects[32].get()));
    session.advance();
    MG_CHECK(session.time() == inserted[32] + 5000 && !env.IsIgnore(objects[32].get()));
    MG_CHECK(env.IsIgnore(objects[17].get()));
    // Expired slots are reused before evicting the refreshed live first entry.
    env.AddIgnore(objects[0].get());
    MG_CHECK(env.m_IgnoreCnt == 16 && env.m_Ignore[0] == objects[17].get());
    MG_CHECK(env.m_Ignore[1] == objects[0].get() && env.m_IgnoreTime[1] == session.time());
    env.DelIgnore(objects[17].get());
    env.AddIgnore(objects[1].get());
    MG_CHECK(env.m_Ignore[0] == objects[1].get() && env.m_IgnoreTime[0] == session.time());
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
    const CPoint origins[]{{8, 8}, {48, 8}, {8, 48}, {48, 48}};
    for (int place = 0; place < 4; ++place) {
        const CPoint origin = origins[place];
        MG_CHECK(map.m_RN.m_Place[place].m_Pos == origin);
        // Each place owns the half-open four-cell square at its origin.
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
            MG_CHECK(map.FindPlace(CPoint(origin.x + x, origin.y + y)) == place);
        for (int i = 0; i < 4; ++i) {
            MG_CHECK(map.FindPlace(CPoint(origin.x - 1, origin.y + i)) == -1);
            MG_CHECK(map.FindPlace(CPoint(origin.x + 4, origin.y + i)) == -1);
            MG_CHECK(map.FindPlace(CPoint(origin.x + i, origin.y - 1)) == -1);
            MG_CHECK(map.FindPlace(CPoint(origin.x + i, origin.y + 4)) == -1);
        }
    }
    // The previous comparison bounded X by the place's Y coordinate.
    MG_CHECK(map.FindPlace(CPoint(20, 48)) == -1);
    MG_CHECK(map.FindPlace(CPoint(51, 8)) == 1 && map.FindPlace(CPoint(8, 51)) == 2);
    MG_CHECK(map.FindPlace(CPoint(8, -1)) == -1 && map.FindPlace(CPoint(8, map.m_SizeMove.y)) == -1);
    // Cells outside every zone have no place, even inside a place square.
    auto &cell = *map.MoveGet(49, 9);
    const int zone = cell.m_Zone;
    cell.m_Zone = -1;
    MG_CHECK(map.FindPlace(CPoint(49, 9)) == -1 && map.FindPlace(CPoint(48, 8)) == 1);
    cell.m_Zone = zone;
    MG_CHECK(map.FindPlace(CPoint(49, 9)) == 1);
}

// Selects the group-count mode for one case and restores the process default.
struct CountMode {
    explicit CountMode(bool original) { GroupCounts::set_original(original); }
    ~CountMode() { GroupCounts::set_original(GroupCounts::original_by_default()); }
};

// Live membership of each logic group of a side.
std::array<int, MAX_LOGIC_GROUP> group_members(const CMatrixSideUnit &side) {
    std::array<int, MAX_LOGIC_GROUP> actual{};
    for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
        if (!object->IsLiveRobot() || object->GetSide() != side.m_Id) continue;
        const int group = object->AsRobot()->GetGroupLogic();
        if (group >= 0 && group < MAX_LOGIC_GROUP) ++actual[group];
    }
    return actual;
}

// Every cached logic-group count must equal the side's live membership.
void check_group_counts(const CMatrixSideUnit &side) {
    const auto actual = group_members(side);
    for (int i = 0; i < MAX_LOGIC_GROUP; ++i) {
        if (actual[i] != side.m_LogicGroup[i].RobotsCnt())
            std::cout << "group=" << i << " actual=" << actual[i] << " cached=" << side.m_LogicGroup[i].RobotsCnt() << '\n';
        MG_CHECK(actual[i] == side.m_LogicGroup[i].RobotsCnt());
    }
}

// In the original mode the split source counts each moved robot as an addition (one member, cached 3),
// and the merged donor keeps its former count (no members, cached 2); every other slot is exact.
void check_original_counts(const CMatrixSideUnit &side, bool split, int source) {
    const auto actual = group_members(side);
    int stale = 0;
    for (int i = 0; i < MAX_LOGIC_GROUP; ++i) {
        const int cached = side.m_LogicGroup[i].RobotsCnt();
        std::cout << "group=" << i << " actual=" << actual[i] << " cached=" << cached << '\n';
        if (actual[i] == cached) continue;
        ++stale;
        if (split) MG_CHECK(i == source && actual[i] == 1 && cached == 3);
        else MG_CHECK(i != source && actual[i] == 0 && cached == 2);
    }
    MG_CHECK(stale == 1);
}

void regroup_counts_correctness(bool split) {
    for (bool original : {false, true}) {
        CountMode counts(original);
        Session session;
        auto robots = units();
        auto *a = robots[0], *b = robots[1];
        MG_CHECK(a->GetSide() == b->GetSide());
        position(*a, 100, 200); position(*b, split ? 501 : 200, 200);
        a->SetTeam(0); b->SetTeam(0);
        a->SetGroupLogic(0); b->SetGroupLogic(split ? 0 : 1);
        if (!split) {
            // A second member gives the merged donor slot a count above one.
            auto *partner = BattleGeometry::create_robot(*g_MatrixMap, a->GetSide(), 60, 200);
            partner->SetTeam(0); partner->SetGroupLogic(0);
        }
        auto &side = *g_MatrixMap->GetSideById(a->GetSide());
        side.Regroup();
        MG_CHECK((a->GetGroupLogic() != b->GetGroupLogic()) == split);
        for (auto *robot : robots) {
            if (robot->GetSide() != side.m_Id) continue;
            MG_CHECK(robot->GetGroupLogic() >= 0 && robot->GetGroupLogic() < MAX_LOGIC_GROUP);
        }
        if (original) check_original_counts(side, split, a->GetGroupLogic());
        else check_group_counts(side);
    }
}
void regroup_split_correctness() { regroup_counts_correctness(true); }
void regroup_merge_correctness() { regroup_counts_correctness(false); }

// Splits and merges in one pass, at the distance boundaries, across several groups and two teams.
// Corrected mode only: the original mode leaves stale counts by design.
void regroup_count_sequences() {
    CountMode counts(false);
    for (float distance : {299.0f, 300.0f, 400.0f, 401.0f}) for (bool initially_together : {false, true}) {
        Session session;
        auto robots = units();
        auto *a = robots[0], *b = robots[1];
        auto &side = *g_MatrixMap->GetSideById(a->GetSide());
        // Initial placement only: a distant member of the first group and a nearby robot of another team.
        auto *distant = BattleGeometry::create_robot(*g_MatrixMap, side.m_Id, 600, 600);
        // A second distant member makes the split move a two-robot subgroup out of the first group.
        auto *distant_pair = BattleGeometry::create_robot(*g_MatrixMap, side.m_Id, 640, 600);
        auto *other = BattleGeometry::create_robot(*g_MatrixMap, side.m_Id, 100 + distance / 2, 260);
        position(*a, 100, 200); position(*b, 100 + distance, 200);
        a->SetTeam(0); b->SetTeam(0); distant->SetTeam(0); distant_pair->SetTeam(0); other->SetTeam(1);
        a->SetGroupLogic(0); b->SetGroupLogic(initially_together ? 0 : 1);
        distant->SetGroupLogic(0); distant_pair->SetGroupLogic(0); other->SetGroupLogic(2);
        side.m_LogicGroup[0].m_Action.m_Type = mlat_Attack;
        side.m_LogicGroup[1].m_Action.m_Type = mlat_None;
        const bool together = initially_together ? distance <= 400 : distance < 300;
        std::array<int, 4> groups{};
        for (int pass = 0; pass < 3; ++pass) {
            side.Regroup();
            check_group_counts(side);
            const std::array<int, 4> current{a->GetGroupLogic(), b->GetGroupLogic(), distant->GetGroupLogic(),
                                             other->GetGroupLogic()};
            MG_CHECK((current[0] == current[1]) == together);
            MG_CHECK(current[2] != current[0] && current[2] != current[1]);
            MG_CHECK(distant_pair->GetGroupLogic() == current[2]);
            MG_CHECK(current[3] != current[0] && current[3] != current[1] && current[3] != current[2]);
            MG_CHECK(side.m_LogicGroup[current[0]].m_Action.m_Type == mlat_Attack);
            MG_CHECK(side.m_LogicGroup[current[2]].m_Team == 0 && side.m_LogicGroup[current[3]].m_Team == 1);
            // Repeated calls keep the regrouped membership.
            if (pass) MG_CHECK(current == groups);
            groups = current;
        }
    }
}

void bomb_escape_capacity() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (bool no_refuge : {false, true}) for (bool invalid_places : {false, true}) {
        Profile profile; profile.mode = mode;
        Session session(profile);
        auto robots = units();
        auto *cover = robots[0], *escaping = robots[1], *bomber = robots[2];
        position(*cover, 120, 100); position(*escaping, 180, 100); position(*bomber, 100, 100);
        IdleBomb bomb(*bomber);
        bomber->GetEnv()->AddToList(cover);
        cover->SetGroupLogic(0); escaping->SetGroupLogic(0);
        auto &map = *g_MatrixMap;
        auto &side = *map.GetPlayerSide();
        side.m_PlayerGroup[0].Order(mpo_AutoAttack);
        for (auto *robot : robots) if (robot != escaping) {
            robot->MoveTo(8, 8);
            robot->GetEnv()->m_Place = 0;
        }
        // Mixed occupants reserve the first candidate, or every place. Duplicate
        // robot reservations alone already exceed the production queue capacity.
        for (int place = 0; place < (no_refuge ? 4 : 2); ++place) {
            auto *cannon = map.StaticAdd<CMatrixCannon>();
            cannon->m_Place = place;
        }
        if (invalid_places) {
            cover->GetEnv()->m_Place = map.m_RN.m_PlaceCnt;
            for (int place : {-1, map.m_RN.m_PlaceCnt}) {
                auto *cannon = map.StaticAdd<CMatrixCannon>();
                cannon->m_Place = place;
            }
            auto &start = map.m_RN.m_Place[0];
            MG_CHECK(start.m_NearCnt + 2 <= 16);
            start.m_Near[start.m_NearCnt] = -1;
            start.m_NearMove[start.m_NearCnt++] = 0;
            start.m_Near[start.m_NearCnt] = map.m_RN.m_PlaceCnt;
            start.m_NearMove[start.m_NearCnt++] = 0;
        }
        map.PrepareBuf();
        constexpr int sentinel = 0x13572468;
        constexpr int backing_count = 64;
        const int capacity = std::max(map.m_RN.m_ZoneCnt, map.m_RN.m_PlaceCnt);
        MG_CHECK(capacity == 4);
        // Oversized, owned backing makes the old append error an explicit bound
        // failure rather than a real heap overflow. Map teardown frees it.
        for (auto **buffer : {&map.m_ZoneIndex, &map.m_ZoneIndexAccess}) {
            HFree(*buffer, g_MatrixHeap);
            *buffer = static_cast<int *>(HAlloc(backing_count * sizeof(int), g_MatrixHeap));
            std::fill_n(*buffer, backing_count, sentinel);
        }
        escaping->MoveTo(20, 20);
        const auto draws = random::draw_count();
        for (int repeat = 0; repeat < 3; ++repeat) {
            side.EscapeFromBomb();
            MG_CHECK(random::draw_count() == draws);
            for (int i = capacity; i < backing_count; ++i) {
                MG_CHECK(map.m_ZoneIndex[i] == sentinel);
                MG_CHECK(map.m_ZoneIndexAccess[i] == sentinel);
            }
            for (int i = 0; i < capacity; ++i) MG_CHECK(map.m_ZoneDataZero[i] == 0);
            CPoint destination, previous;
            MG_CHECK(escaping->GetMoveToCoords(destination));
            MG_CHECK(escaping->GetReturnCoords(previous) == !no_refuge);
            MG_CHECK(destination == (no_refuge ? CPoint(20, 20) : CPoint(8, 48)));
            if (!no_refuge) MG_CHECK(previous == CPoint(20, 20));
            if (no_refuge) {
                auto *begin = map.m_ZoneIndex;
                for (int i = 0; i < capacity; ++i)
                    MG_CHECK(std::count(begin, begin + capacity, i) == 1);
            }
        }
    }
}

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
