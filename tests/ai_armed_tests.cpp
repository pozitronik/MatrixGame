// MatrixGame - licensed under GPLv2 or any later version.
#include "battle_scenario.hpp"
#include "competing_repair_recording.hpp"
#include "battle_geometry.hpp"
#include "battle_combat_geometry.hpp"
#include "battle_mesh.hpp"
#include "test_support.hpp"
#include "MatrixRobot.hpp"
#include "MatrixSide.hpp"
#include "MatrixObjectCannon.hpp"
#include "Logic/MatrixEnvironment.h"
#include "zero_x_mode.hpp"
#include "CStorage.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace tests::ai {
namespace {
CMatrixRobotAI *owner() {
    for (auto *obj = CMatrixMapStatic::GetFirstLogic(); obj; obj = obj->GetNextLogic())
        if (obj->IsLiveRobot() && obj->GetSide() == PLAYER_SIDE) return obj->AsRobot();
    throw std::runtime_error("Missing player robot");
}
void configure() {
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 100;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_PLASMA)] = 300;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_PLASMA)] = 100;
}
void evidence(battle::Session &session, const char *trigger, CMatrixRobotAI *robot, const char *fixture = nullptr) {
    if (!fixture) fixture = robot->HaveRepair() == 2 ? "synthetic-repair-boxes-v1" : "synthetic-armed-boxes-v2";
    std::cout << session.metadata(fixture) << '\n';
    std::cout << "{\"decision\":\"armed-tactical-boundary\",\"trigger\":\"" << trigger
              << "\",\"enemy_count\":" << robot->GetEnv()->GetEnemyCnt()
              << ",\"move_order\":" << (robot->FindOrderLikeThat(ROT_MOVE_TO) ? "true" : "false")
              << ",\"fire_order\":" << (robot->FindOrderLikeThat(ROT_FIRE) ? "true" : "false")
              << ",\"target_present\":" << (robot->GetEnv()->m_Target ? "true" : "false")
              << ",\"attack_target_present\":" << (robot->GetEnv()->m_TargetAttack ? "true" : "false")
              << ",\"rng_draws\":" << random::draw_count() << "}\n";
}
}

void visual_acquisition_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
        for (int distance : {109, 110, 111}) {
            BattleCombatAssets assets;
            battle::Session session({mode, 79});
            assets.install(); configure();
            auto *observer = owner();
            arm_battle_robot(*observer, RUK_WEAPON_LASER);
            observer->m_HullForward = D3DXVECTOR3(0, 1, 0);
            auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2,
                observer->m_PosX + distance * 0.6f, observer->m_PosY - distance * 0.8f);
            MG_CHECK(observer->GetMaxFireDist() == 100);
            MG_CHECK(!observer->GetEnv()->SearchEnemy(target));
            observer->GatherInfo(0);
            MG_CHECK(bool(observer->GetEnv()->SearchEnemy(target)) == (distance <= 110));
            observer->GatherInfo(0);
            MG_CHECK(observer->GetEnv()->GetEnemyCnt() == (distance <= 110 ? 1 : 0));
            evidence(session, "visual-range-boundary", observer);
        }
}

void visual_enemy_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2, 230, 100);
        observer->GatherInfo(0);
        MG_CHECK(!observer->GetEnv()->SearchEnemy(target));
        // Assembly is an explicit fixture intervention, not a simulated upgrade.
        arm_battle_robot(*target, RUK_WEAPON_PLASMA);
        observer->GatherInfo(0);
        MG_CHECK(observer->GetEnv()->SearchEnemy(target));
        MG_CHECK(observer->GetEnv()->GetEnemyCnt() == 1);
        evidence(session, "enemy-weapon-range-increased", observer);
    }
}

void visual_filters() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int variant : {0, 1, 2}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, variant == 0 ? PLAYER_SIDE : 2, 150, 150);
        if (variant == 1) target->m_CurrState = ROBOT_DIP;
        if (variant == 2) observer->GetEnv()->AddIgnore(target);
        observer->GatherInfo(0);
        // Restore setup-only state before assertions and ordinary teardown.
        if (variant == 1) target->m_CurrState = ROBOT_SUCCESSFULLY_BUILD;
        MG_CHECK(!observer->GetEnv()->SearchEnemy(target));
        MG_CHECK(observer->GetEnv()->GetEnemyCnt() == 0);
        evidence(session, variant == 0 ? "friendly" : variant == 1 ? "dying" : "ignored", observer);
    }
}

void visual_obstruction() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2, 160, 120);
        auto *blocker = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 130, 160);
        observer->GatherInfo(0);
        MG_CHECK(!observer->GetEnv()->SearchEnemy(target));
        MG_CHECK(!observer->GetEnv()->IsIgnore(target));
        g_MatrixMap->StaticDelete(blocker);
        observer->GatherInfo(0);
        MG_CHECK(observer->GetEnv()->SearchEnemy(target));
        evidence(session, "visual-blocker-retired", observer);
    }
}

// An enemy straight ahead along the Y axis: the sight line has no X travel. The original trace rejected every
// object hit along such a line, so the observer never saw the enemy.
void visual_axis_aligned() {
    for (bool original : {false, true}) for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        tests::ZeroXMode zero_x(original);
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        observer->m_HullForward = D3DXVECTOR3(0, -1, 0);
        // Another player robot stands further along +Y on x = 100, so the enemy is placed on the -Y side.
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2, observer->m_PosX, observer->m_PosY - 60);
        const D3DXVECTOR3 from = observer->GetGeoCenter(), to = target->GetGeoCenter();
        MG_CHECK(from.x == to.x && from.y != to.y);
        MG_CHECK(g_MatrixMap->IsLogicVisible(observer, target, 0.0f) == !original);
        observer->GatherInfo(0);
        MG_CHECK(bool(observer->GetEnv()->SearchEnemy(target)) == !original);
        evidence(session, original ? "visual-axis-original" : "visual-axis-corrected", observer);
    }
}

void visual_retirement_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int distance : {139, 140, 141}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2,
            observer->m_PosX + distance * 0.6f, observer->m_PosY - distance * 0.8f);
        auto &env = *observer->GetEnv();
        env.AddToList(target); // Initial historical sighting, outside acquisition range.
        env.m_Target = target; env.m_TargetAttack = target;
        for (int observation = 1; observation <= 3; ++observation) {
            observer->GatherInfo(0);
            auto *entry = env.SearchEnemy(target);
            if (distance > 140 && observation == 3) {
                MG_CHECK(!entry && !env.m_Target && !env.m_TargetAttack);
            } else {
                MG_CHECK(entry && entry->m_DelSlowly == (distance > 140 ? observation : 0));
                MG_CHECK(env.m_Target == target && env.m_TargetAttack == target);
            }
        }
        evidence(session, "visual-retirement-boundary", observer);
    }
}

void armed_friendly_line() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_PLASMA);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2, 200, 100);
        auto *blocker = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 150, 150);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto &env = *observer->GetEnv();
        env.AddToList(target);
        const int group = observer->GetGroupLogic();
        const auto intent = session.robot(1);
        MG_CHECK(!side.FirePL(group));
        MG_CHECK(env.m_TargetAttack == target && !observer->FindOrderLikeThat(ROT_FIRE));
        g_MatrixMap->StaticDelete(blocker);
        MG_CHECK(side.FirePL(group));
        MG_CHECK(env.m_TargetAttack == target && env.m_Target == target);
        MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE));
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO));
        const auto after = session.robot(1);
        MG_CHECK(after.order == intent.order && after.destination_x == intent.destination_x &&
                 after.destination_y == intent.destination_y);
        evidence(session, "friendly-blocker-retired", observer);
    }
}

void ineffective_fire_run(bool blocked, bool moving = false, int place_not_found = 0) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (bool multiple : {false, true})
    for (int target_age : {2000, 2001, 4000, 4001})
    for (int hit_age : {4000, 4001, 6000, 6001, 10000, 10001}) {
        if (place_not_found != 0 && (target_age != 4001 || hit_age != 6001)) continue;
        BattleCombatAssets assets;
        battle::Profile profile{mode, 84};
        profile.initial_time_ms = 12000;
        battle::Session session(profile);
        assets.install(); configure();
        MG_CHECK(session.remove(2));
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_PLASMA);
        auto *target = BattleGeometry::create_robot(*g_MatrixMap, 2, 200, 100);
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto &env = *observer->GetEnv();
        // Historical enemy memory and an occupied firing place are initial data.
        // No projectile or hit result is synthesized during the decision call.
        observer->SetGroupLogic(0);
        side.m_PlayerGroup[0].Order(mpo_Stop);
        g_MatrixMap->m_RN.m_Place[0].m_Pos = CPoint(observer->GetMapPosX(), observer->GetMapPosY());
        env.m_Place = 0;
        env.m_PlaceNotFound = place_not_found;
        env.AddToList(target);
        if (multiple) env.AddToList(BattleGeometry::create_robot(*g_MatrixMap, 2, 240, 180));
        env.m_TargetAttack = env.m_TargetLast = target;
        env.m_TargetChange = 12000 - target_age;
        env.m_LastHitTarget = blocked ? 12000 : 12000 - hit_age;
        env.m_LastFire = blocked ? 12000 - hit_age : 12000;
        if (blocked) BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 150, 150);
        MG_CHECK(!observer->FindOrderLikeThat(ROT_MOVE_TO));
        if (moving) observer->MoveTo(observer->GetMapPosX(), observer->GetMapPosY());
        side.WarPL(0);
        const bool stale = !moving && target_age > 4000 && hit_age > 4000;
        const bool abandon_place = !moving && (blocked ? target_age > 4000 && hit_age > 6000 && 12000 - place_not_found > 2000
            : (!multiple && stale) || (target_age > 2000 && hit_age > 10000));
        MG_CHECK(env.m_TargetAttack == (multiple && stale ? nullptr : target));
        MG_CHECK(env.m_Place == (abandon_place ? -1 : 0));
        MG_CHECK(env.IsBadPlace(0) == abandon_place);
        const bool duplicate_abandon = !moving && !blocked && !multiple && stale && hit_age > 10000;
        MG_CHECK(env.m_BadPlaceCnt == (duplicate_abandon ? 2 : abandon_place ? 1 : 0));
        if (abandon_place) MG_CHECK(env.m_BadPlace[0] == 0);
        // Observed quirk: the second rule remembers the already-invalid place.
        if (duplicate_abandon) MG_CHECK(env.m_BadPlace[1] == -1);
        MG_CHECK(env.m_Target == (blocked ? nullptr : target));
        MG_CHECK(env.m_LastFire == (blocked ? 12000 - hit_age : 12000));
        MG_CHECK(env.m_LastHitTarget == (blocked ? 12000 : 12000 - hit_age));
        MG_CHECK(env.m_TargetChange == (moving ? 12000 : 12000 - target_age));
        MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE) == !blocked);
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO) == moving);
        MG_CHECK(side.m_PlayerGroup[0].Order() == mpo_Stop && session.time() == 12000);
        evidence(session, blocked ? "blocked-fire-decision" : "ineffective-fire-decision", observer);
        std::cout << "targets=" << (multiple ? 2 : 1) << " target_age=" << target_age
                  << " place_not_found=" << place_not_found << " pending_move=" << moving << " feedback_age=" << hit_age << " place=" << env.m_Place << '\n';
    }
}

void ineffective_fire_decisions() { ineffective_fire_run(false); }
void blocked_fire_decisions() {
    ineffective_fire_run(true);
    ineffective_fire_run(true, false, 10000); // Exactly 2000 ms since failed place search.
    ineffective_fire_run(true, false, 9999);  // The strict cooldown has expired.
}

void moving_fire_decisions() { ineffective_fire_run(false, true); ineffective_fire_run(true, true); }

void armed_target_retention() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_PLASMA);
        auto *first = BattleGeometry::create_robot(*g_MatrixMap, 2, 220, 100);
        auto *nearer = BattleGeometry::create_robot(*g_MatrixMap, 2, 160, 250);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        auto &env = *observer->GetEnv();
        const int group = observer->GetGroupLogic();
        env.AddToList(first);
        MG_CHECK(side.FirePL(group) && env.m_TargetAttack == first);
        env.AddToList(nearer);
        MG_CHECK(side.FirePL(group) && env.m_TargetAttack == first);
        MG_CHECK(env.m_Target == first && observer->FindOrderLikeThat(ROT_FIRE));
        g_MatrixMap->StaticDelete(first);
        MG_CHECK(!env.m_Target && !env.m_TargetAttack);
        MG_CHECK(side.FirePL(group) && env.m_TargetAttack == nearer);
        MG_CHECK(env.m_Target == nearer && observer->FindOrderLikeThat(ROT_FIRE));
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_MoveTo);
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO));
        evidence(session, "retained-target-deleted", observer);
    }
}
namespace {
CMatrixRobotAI *damaged_friendly(float x, float y) {
    auto *robot = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, x, y);
    robot->InitMaxHitpoint(50);
    g_MatrixMap->m_Difficulty.k_damage_enemy_to_player = 1;
    g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {1, 0, 0};
    MG_CHECK(!robot->Damage(WEAPON_LASER, robot->GetGeoCenter(), D3DXVECTOR3(0, 0, 0), 0, nullptr));
    MG_CHECK(robot->NeedRepair());
    return robot;
}
void require_repair_order(CMatrixRobotAI &robot) {
    int count = 0;
    for (int i = 0; i < robot.GetOrdersInPool(); ++i) {
        const auto &order = *robot.GetOrder(i);
        if (order.m_OrderType != ROT_FIRE) continue;
        MG_CHECK(order.m_Param4 == 2);
        ++count;
    }
    MG_CHECK(count == 1);
}
void configure_repair(CMatrixRobotAI &robot) {
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_REPAIR)] = 100;
    g_Config.m_WeaponCooldown[Weap2Index(WEAPON_REPAIR)] = 100;
    arm_battle_robot(robot, RUK_WEAPON_REPAIR);
    MG_CHECK(robot.HaveRepair() == 2 && robot.GetRepairDist() == 100);
}
}

void repair_acquisition_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (int distance : {99, 100, 101}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install();
        auto *observer = owner();
        configure_repair(*observer);
        auto *patient = damaged_friendly(observer->m_PosX + distance * 0.6f,
                                         observer->m_PosY - distance * 0.8f);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        const int group = observer->GetGroupLogic();
        const CPoint destination = side.m_PlayerGroup[group].m_To;
        const auto draws = random::draw_count();
        side.RepairPL(group);
        MG_CHECK(observer->GetEnv()->m_Target == (distance < 100 ? patient : nullptr));
        MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE) == (distance < 100));
        if (distance < 100) require_repair_order(*observer);
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_MoveTo);
        MG_CHECK(side.m_PlayerGroup[group].m_To == destination);
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO));
        MG_CHECK(random::draw_count() == draws);
        evidence(session, "repair-range-boundary", observer);
    }
}

void repair_target_retention() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install();
        auto *observer = owner();
        configure_repair(*observer);
        auto *first = damaged_friendly(160, 140);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        const int group = observer->GetGroupLogic();
        const CPoint destination = side.m_PlayerGroup[group].m_To;
        auto &env = *observer->GetEnv();
        side.RepairPL(group);
        MG_CHECK(env.m_Target == first && observer->FindOrderLikeThat(ROT_FIRE));
        require_repair_order(*observer);
        evidence(session, "repair-first-patient-selected", observer);
        auto *nearer = damaged_friendly(130, 160);
        side.RepairPL(group);
        MG_CHECK(env.m_Target == first);
        evidence(session, "repair-nearer-patient-does-not-interrupt", observer);
        // A production health update makes the retained patient ineligible.
        g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {1, 0, 1};
        MG_CHECK(!first->Damage(WEAPON_REPAIR, first->GetGeoCenter(), D3DXVECTOR3(0, 0, 0), 0, nullptr));
        MG_CHECK(!first->NeedRepair());
        side.RepairPL(group);
        MG_CHECK(env.m_Target == nearer && observer->FindOrderLikeThat(ROT_FIRE));
        require_repair_order(*observer);
        evidence(session, "repair-healed-patient-replaced", observer);
        g_MatrixMap->StaticDelete(nearer);
        MG_CHECK(env.m_Target == nullptr);
        side.RepairPL(group);
        MG_CHECK(env.m_Target == nullptr);
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_MoveTo);
        MG_CHECK(side.m_PlayerGroup[group].m_To == destination);
        MG_CHECK(observer->FindOrderLikeThat(ROT_MOVE_TO));
        evidence(session, "repair-patient-healed-and-retired", observer);
    }
}

void repair_explicit_priority() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 79});
        assets.install();
        auto *observer = owner();
        configure_repair(*observer);
        auto *first = damaged_friendly(160, 140);
        session.ground_command(500, 300);
        auto &side = *g_MatrixMap->GetPlayerSide();
        const int group = observer->GetGroupLogic();
        side.RepairPL(group);
        MG_CHECK(observer->GetEnv()->m_Target == first);
        auto *requested = damaged_friendly(130, 160);
        side.PGOrderRepair(group, requested);
        side.RepairPL(group);
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_Repair);
        MG_CHECK(side.m_PlayerGroup[group].m_Obj == requested);
        MG_CHECK(observer->GetEnv()->m_Target == requested);
        MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE));
        require_repair_order(*observer);
        evidence(session, "explicit-repair-overrides-retained-patient", observer);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {1, 0, 1};
        MG_CHECK(!requested->Damage(WEAPON_REPAIR, requested->GetGeoCenter(), D3DXVECTOR3(0, 0, 0), 0, nullptr));
        MG_CHECK(!requested->NeedRepair());
        side.TaktPL();
        MG_CHECK(side.m_PlayerGroup[group].Order() == mpo_Stop);
        MG_CHECK(side.m_PlayerGroup[group].m_Obj == nullptr);
        evidence(session, "completed-explicit-repair-stops-group", observer);
    }
}

namespace {
CMatrixCannon *awareness_cannon(float distance, float range, int side = 2) {
    auto &props = g_Config.m_CannonsProps[0];
    props = {};
    props.m_Hitpoint = 1000;
    props.weapon = WEAPON_CANNON2;
    props.m_Strength = 10;
    g_Config.m_WeaponRadius[Weap2Index(WEAPON_CANNON2)] = range;
    // Low-profile synthetic assembly: the production sight endpoint (ground + 20)
    // must intersect the model's sphere, unlike the taller combat fixture.
    const MeshAttachment basis{20, L"Turret", 0, 0, 10};
    const MeshAttachment turret{20, L"Shaft", 0, 0, 10};
    const MeshAttachment muzzle{50, L"muzzle", 0, 15, 0};
    load_battle_mesh(std::span(&basis, 1), L"Matrix\\Cannon\\Basis.vo", true);
    load_battle_mesh(std::span(&turret, 1), L"Matrix\\Cannon\\Turret1.vo", true);
    load_battle_mesh(std::span(&muzzle, 1), L"Matrix\\Cannon\\Shaft1.vo", true);
    auto *target = create_battle_cannon(side, 100 + distance * 0.6f, 200 - distance * 0.8f);
    MG_CHECK(target->IsLiveCannon() && target->GetFireRadius() == range);
    return target;
}
}
namespace {
// Initial terrain data, with real height triangles and rebuilt trace groups.
// All initial session objects must be removed before rebuilding their groups.
template<class Height>
void turret_heightfield(Height height) {
    auto &map = *g_MatrixMap;
    MG_CHECK(CMatrixMapStatic::GetFirstLogic() == nullptr);
    for (int y = 0; y <= 32; ++y) for (int x = 0; x <= 32; ++x) {
        auto &point = *map.PointGet(x, y);
        point.z = point.z_land = height(x);
        point.n = D3DXVECTOR3(0, 0, 1);
    }
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        auto &unit = *map.UnitGet(x, y);
        unit.SetType(CELLFLAG_LAND);
        const float a = map.PointGet(x, y)->z, b = map.PointGet(x + 1, y)->z;
        unit.a1 = unit.a2 = (b - a) / GLOBAL_SCALE;
        unit.b1 = unit.b2 = 0;
        unit.c1 = unit.c2 = a;
    }
    CStorage storage;
    CStorageRecord record(DATA_GROUPS);
    record.AddItem(CStorageRecordItem(DATA_GROUPS_DATA, ST_BYTE));
    storage.AddRecord(record);
    auto *buffer = storage.GetBuf(DATA_GROUPS, DATA_GROUPS_DATA, ST_BYTE);
    for (int gy = 0; gy < 32; gy += MAP_GROUP_SIZE) for (int gx = 0; gx < 32; gx += MAP_GROUP_SIZE) {
        const int width = std::min(MAP_GROUP_SIZE, 32 - gx), height = std::min(MAP_GROUP_SIZE, 32 - gy);
        std::vector<BYTE> raw;
        const auto append = [&]<class T>(const T &value) {
            auto *first = reinterpret_cast<const BYTE *>(&value);
            raw.insert(raw.end(), first, first + sizeof value);
        };
        append(WORD(gx)); append(WORD(gy)); append(DWORD(1)); append(DWORD(0));
        append(DWORD(width * height * 6 * sizeof(WORD)));
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const WORD a = WORD(y * (width + 1) + x), b = WORD(a + 1);
            const WORD c = WORD(a + width + 1), d = WORD(c + 1);
            for (WORD index : {a, b, d, a, d, c}) append(index);
        }
        append(DWORD((width + 1) * (height + 1) * sizeof(SCompileBottomVert)));
        for (int y = 0; y <= height; ++y) for (int x = 0; x <= width; ++x)
            append(SCompileBottomVert{uint16_t(gx + x), uint16_t(gy + y), 0, 0});
        const int array = buffer->AddArray();
        buffer->AddToArray<BYTE>(array, raw.data(), int(raw.size()));
    }
    map.GroupBuild(storage);
}

void turret_terrain(float target_z, bool above, bool ridge) {
    turret_heightfield([=](int x) {
        if (ridge && x == 7) return 160.0f;
        return x <= 6 ? target_z + (above ? -40 : 0) : target_z - 25;
    });
}

struct TurretBoundary {
    float x, y, z, radius, squared_limit;
};
void turret_boundary(bool retirement, bool occlusion) {
    const TurretBoundary acquisition[] = {{60, 70, 60, 50, 12100}, {168, 104, 42, 200, 40804}};
    const TurretBoundary remembered[] = {{100, 100, 50, 50, 22500}, {220, 200, 40, 200, 90000}};
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (bool above : {false, true}) for (const auto &boundary : std::span(retirement ? remembered : acquisition, 2))
    for (float offset : {-0.25f, 0.0f, 0.25f}) {
        if (occlusion && offset != 0) continue;
        std::cout << "turret boundary mode=" << int(mode) << " retirement=" << retirement
                  << " occlusion=" << occlusion << " above=" << above << " range=" << boundary.radius
                  << " offset=" << offset << '\n';
        BattleCombatAssets assets;
        battle::Profile profile{mode, 84};
        profile.layout = NavigationLayout::Open;
        battle::Session session(profile);
        assets.install(); configure();
        for (int id = 1; id <= 4; ++id) MG_CHECK(session.remove(id));
        turret_terrain(boundary.z, above, occlusion);
        auto *observer = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 200);
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = awareness_cannon(0, boundary.radius);
        const D3DXVECTOR3 wanted(100 + boundary.x + offset, 200 + boundary.y, boundary.z);
        target->m_Pos = D3DXVECTOR2(wanted.x, wanted.y);
        // D3DX bounds can round an identity transform by a few ULP. Adjust the
        // initial model placement through its real transform, not its cached center.
        for (int attempt = 0; attempt < 16; ++attempt) {
            target->RChange(MR_Matrix); target->RNeed(MR_Matrix); target->JoinToGroup();
            const auto actual = target->GetGeoCenter();
            if (actual == wanted) break;
            target->m_Pos.x += wanted.x - actual.x;
            target->m_Pos.y += wanted.y - actual.y;
            target->m_AddH += wanted.z - actual.z;
        }
        const auto center = target->GetGeoCenter();
        MG_CHECK(center == wanted);
        MG_CHECK(std::fabs(target->m_Pos.x - wanted.x) < 0.001f &&
                 std::fabs(target->m_Pos.y - wanted.y) < 0.001f && std::fabs(target->m_AddH) < 0.001f);
        MG_CHECK(observer->GetMaxFireDist() == 100);
        MG_CHECK(std::fabs(observer->GetGeoCenter().z - center.z - (above ? -20 : 20)) < 0.0001f);
        const D3DXVECTOR3 delta = center - D3DXVECTOR3(observer->m_PosX, observer->m_PosY, 0);
        const float squared = D3DXVec3LengthSq(&delta);
        if (offset == 0) MG_CHECK(squared == boundary.squared_limit);
        else MG_CHECK((squared < boundary.squared_limit) == (offset < 0));
        const bool visible = g_MatrixMap->IsLogicVisible(observer, target, 0);
        MG_CHECK(visible == !occlusion);
        if (occlusion) {
            MG_CHECK(g_MatrixMap->Trace(nullptr, observer->GetGeoCenter(), center, TRACE_LANDSCAPE, observer) ==
                     TRACE_STOP_LANDSCAPE);
        }
        auto &env = *observer->GetEnv();
        if (retirement) {
            env.AddToList(target); // Declared historical sighting and selected target.
            env.m_Target = env.m_TargetAttack = target;
        }
        for (int scan = 1; scan <= 3; ++scan) {
            observer->GatherInfo(0);
            auto *entry = env.SearchEnemy(target);
            const bool retiring = retirement && offset > 0;
            const bool present = retirement ? !(retiring && scan == 3) : !occlusion && offset <= 0;
            MG_CHECK(bool(entry) == present && env.GetEnemyCnt() == (present ? 1 : 0));
            MG_CHECK(!env.IsIgnore(target));
            if (entry) MG_CHECK(entry->m_DelSlowly == (retiring ? scan : 0));
            MG_CHECK(env.m_Target == (retirement && present ? target : nullptr));
            MG_CHECK(env.m_TargetAttack == (retirement && present ? target : nullptr));
        }
        if (!retirement) {
            observer->SetGroupLogic(0);
            g_MatrixMap->GetPlayerSide()->m_PlayerGroup[0].Order(mpo_Stop);
            g_MatrixMap->GetPlayerSide()->FirePL(0);
            MG_CHECK(env.m_TargetAttack == (!occlusion && offset <= 0 ? target : nullptr));
        }
        if (occlusion) {
            // Visibility prevents a new sighting; it does not retire prior in-range memory.
            env.AddToList(target);
            env.m_Target = env.m_TargetAttack = target;
            for (int scan = 1; scan <= 3; ++scan) {
                observer->GatherInfo(0);
                auto *entry = env.SearchEnemy(target);
                MG_CHECK(env.GetEnemyCnt() == 1 && entry && entry->m_DelSlowly == 0);
                MG_CHECK(env.m_Target == target && env.m_TargetAttack == target);
            }
        }
        MG_CHECK(session.time() == 0 && session.frame() == 0);
        evidence(session, retirement ? "turret-exact-retirement" : occlusion ? "turret-elevated-terrain" :
                 "turret-exact-acquisition", observer, "synthetic-turret-heightfield-v1");
        std::cout << "above=" << above << " turret_range=" << boundary.radius << " offset=" << offset
                  << " squared_distance=" << squared << " squared_limit=" << boundary.squared_limit << '\n';
    }
}
}
void turret_acquisition_equality() { turret_boundary(false, false); }
void turret_retirement_equality() { turret_boundary(true, false); }
void turret_elevated_terrain() { turret_boundary(false, true); }

void turret_elevation_boundary() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (float sign : {-1.0f, 1.0f}) for (int sample : {-1, 0, 1}) {
        BattleCombatAssets assets;
        battle::Profile profile{mode, 84};
        profile.layout = NavigationLayout::Open;
        battle::Session session(profile);
        assets.install(); configure();
        g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 300;
        for (int id = 1; id <= 4; ++id) MG_CHECK(session.remove(id));
        // Fixed binary32 operands: 42.8718719482421875 / 160 equals the engine's float tangent.
        const float threshold = 0x1.126146p-2f;
        MG_CHECK(tan(BARREL_TO_SHOT_ANGLE) == threshold);
        const float boundary_height = 0x1.56f998p+5f;
        const float height = sample < 0 ? boundary_height - 0.01f :
                             sample > 0 ? boundary_height + 0.01f : boundary_height;
        const float wanted_z = 64 + sign * height;
        const float slope = (wanted_z - 69) / 150;
        // Two ULP of initial terrain placement compensate the generated robot
        // bounds, leaving its actual center at 64. No cached center is assigned.
        turret_heightfield([=](int x) { return (44.0f + 0x1p-17f) + std::max(0, x * 20 - 100) * slope; });
        auto *observer = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 100, 200);
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = awareness_cannon(0, 200);
        const D3DXVECTOR3 wanted(240, 180, wanted_z);
        target->m_Pos = D3DXVECTOR2(wanted.x, wanted.y);
        // As in the range fixtures, adjust initial placement through real matrices.
        for (int attempt = 0; attempt < 16; ++attempt) {
            target->RChange(MR_Matrix); target->RNeed(MR_Matrix); target->JoinToGroup();
            const auto actual = target->GetGeoCenter();
            if (actual == wanted) break;
            target->m_Pos.x += wanted.x - actual.x;
            target->m_Pos.y += wanted.y - actual.y;
            target->m_AddH += wanted.z - actual.z;
        }
        const auto center = target->GetGeoCenter();
        MG_CHECK(center.x == wanted.x && center.y == wanted.y && std::fabs(center.z - wanted.z) < 0.00001f);
        MG_CHECK(observer->GetGeoCenter().z == 64);
        MG_CHECK(std::fabs(target->m_Pos.x - wanted.x) < 0.001f &&
                 std::fabs(target->m_Pos.y - wanted.y) < 0.001f && std::fabs(target->m_AddH) < 0.001f);
        MG_CHECK(observer->GetMaxFireDist() == 300);
        MG_CHECK(observer->GetMapPosX() == 8 && observer->GetMapPosY() == 18);
        MG_CHECK(Float2Int(target->m_Pos.x / GLOBAL_SCALE_MOVE) == 24 &&
                 Float2Int(target->m_Pos.y / GLOBAL_SCALE_MOVE) == 18);
        MG_CHECK(g_MatrixMap->IsLogicVisible(observer, target, 0));
        const float ratio = std::fabs(center.z - observer->GetGeoCenter().z) / 160.0f;
        if (sample == 0) {
            MG_CHECK(std::fabs(center.z - observer->GetGeoCenter().z) == height);
            MG_CHECK(ratio == threshold);
        }
        else MG_CHECK((ratio < threshold) == (sample < 0));
        // The 140,-20 world displacement is shorter than the 16 movement cells.
        // Using geometric distance instead would reject even the shallow sample.
        MG_CHECK(std::fabs(center.z - observer->GetGeoCenter().z) / std::sqrt(20000.0f) > threshold);
        const D3DXVECTOR3 range_delta = center - D3DXVECTOR3(observer->m_PosX, observer->m_PosY, 0);
        MG_CHECK(D3DXVec3LengthSq(&range_delta) < 300.0f * 300.0f);
        auto &env = *observer->GetEnv();
        const bool acquired = sample < 0;
        const auto release = [](SObjectCore *core) { core->Release(); };
        std::unique_ptr<SObjectCore, decltype(release)> core(target->GetCore(DEBUG_CALL_INFO), release);
        for (int scan = 0; scan < 3; ++scan) {
            observer->GatherInfo(0);
            MG_CHECK(core->m_Object == target);
            MG_CHECK(bool(env.SearchEnemy(target)) == acquired && env.GetEnemyCnt() == (acquired ? 1 : 0));
            MG_CHECK(env.IsIgnore(target) == !acquired && env.m_IgnoreCnt == (acquired ? 0 : 1));
            if (!acquired) MG_CHECK(env.m_Ignore[0] == target && env.m_IgnoreTime[0] == 0);
        }
        observer->SetGroupLogic(0);
        g_MatrixMap->GetPlayerSide()->m_PlayerGroup[0].Order(mpo_Stop);
        g_MatrixMap->GetPlayerSide()->FirePL(0);
        MG_CHECK(env.m_TargetAttack == (acquired ? target : nullptr));
        // Declared target removal checks the acquired reference's lifetime;
        // ignore expiry is covered separately, without advancing this fixed-time scene.
        if (acquired) {
            g_MatrixMap->StaticDelete(target);
            MG_CHECK(core->m_Object == nullptr && env.GetEnemyCnt() == 0);
            MG_CHECK(!env.m_Target && !env.m_TargetAttack);
            observer->GatherInfo(0);
            MG_CHECK(env.GetEnemyCnt() == 0 && !env.m_Target && !env.m_TargetAttack);
        }
        MG_CHECK(session.time() == 0 && session.frame() == 0);
        evidence(session, "turret-elevation-boundary", observer, "synthetic-turret-elevation-v1");
        std::cout << "sign=" << sign << " sample=" << sample << std::hexfloat
                  << " center_z=" << center.z << " height_ratio=" << ratio
                  << " threshold=" << threshold << std::defaultfloat << '\n';
    }
}

void turret_acquisition_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (bool longer_turret : {false, true}) for (bool inside : {false, true}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 84});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        const float distance = longer_turret ? (inside ? 190 : 210) : (inside ? 95 : 105);
        auto *target = awareness_cannon(distance, longer_turret ? 200 : 50);
        MG_CHECK(g_MatrixMap->IsLogicVisible(observer, target, 0));
        observer->GatherInfo(0);
        MG_CHECK(bool(observer->GetEnv()->SearchEnemy(target)) == inside);
        MG_CHECK(!observer->GetEnv()->IsIgnore(target));
        observer->GatherInfo(0);
        MG_CHECK(observer->GetEnv()->GetEnemyCnt() == (inside ? 1 : 0));
        evidence(session, longer_turret ? "turret-own-range" : "observer-range-for-turret", observer, "synthetic-low-turret-v1");
    }
}
void turret_retirement_range() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT})
    for (bool longer_turret : {false, true})
    for (bool outside : {false, true}) for (bool aiming_at_observer : {false, true}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 84});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        const float distance = longer_turret ? (outside ? 299 : 297) : (outside ? 147 : 145);
        auto *target = awareness_cannon(distance, longer_turret ? 200 : 50);
        auto &env = *observer->GetEnv();
        env.AddToList(target); // A declared historical sighting, beyond acquisition range.
        if (aiming_at_observer) target->m_TargetCore = observer->GetCore(DEBUG_CALL_INFO);
        for (int scan = 1; scan <= 3; ++scan) {
            observer->GatherInfo(0);
            auto *entry = env.SearchEnemy(target);
            const bool retiring = outside && !aiming_at_observer;
            if (retiring && scan == 3) MG_CHECK(entry == nullptr);
            else MG_CHECK(entry && entry->m_DelSlowly == (retiring ? scan : 0));
        }
        evidence(session, aiming_at_observer ? "turret-aim-retains-memory" : "turret-range-retirement", observer, "synthetic-low-turret-v1");
    }
}
void turret_acquisition_filters() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int filter = 0; filter < 3; ++filter) {
        BattleCombatAssets assets;
        battle::Session session({mode, 84});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = awareness_cannon(95, 100, filter == 0 ? PLAYER_SIDE : 2);
        if (filter == 1) target->m_CurrState = CANNON_UNDER_CONSTRUCTION;
        if (filter == 2) observer->GetEnv()->AddIgnore(target);
        observer->GatherInfo(0);
        MG_CHECK(!observer->GetEnv()->SearchEnemy(target));
        if (filter == 0) target->SetSide(2);
        if (filter == 1) target->m_CurrState = CANNON_IDLE;
        if (filter == 2) observer->GetEnv()->DelIgnore(target);
        observer->GatherInfo(0);
        MG_CHECK(observer->GetEnv()->SearchEnemy(target));
        evidence(session, "turret-filter-removed", observer, "synthetic-low-turret-v1");
    }
}
void turret_visual_obstruction() {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Session session({mode, 84});
        assets.install(); configure();
        auto *observer = owner();
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        auto *target = awareness_cannon(95, 100);
        auto *blocker = BattleGeometry::create_robot(*g_MatrixMap, PLAYER_SIDE, 128.5f, 162);
        MG_CHECK(!g_MatrixMap->IsLogicVisible(observer, target, 0));
        observer->GatherInfo(0);
        MG_CHECK(!observer->GetEnv()->SearchEnemy(target) && !observer->GetEnv()->IsIgnore(target));
        g_MatrixMap->StaticDelete(blocker);
        MG_CHECK(g_MatrixMap->IsLogicVisible(observer, target, 0));
        observer->GatherInfo(0);
        MG_CHECK(observer->GetEnv()->SearchEnemy(target));
        evidence(session, "turret-occluder-retired", observer, "synthetic-low-turret-v1");
    }
}

namespace {
void repair_movement(bool retire) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) {
        BattleCombatAssets assets;
        battle::Profile profile{mode, 84, 10, 2000, false, true};
        profile.layout = NavigationLayout::Open;
        battle::Session session(profile);
        assets.install();
        auto *observer = owner();
        g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
        configure_repair(*observer);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 0};
        auto *patient = damaged_friendly(160, 140);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {17, 17, 0};
        MG_CHECK(!patient->Damage(WEAPON_LASER, patient->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
        MG_CHECK(patient->GetHitPoint() == 3.2f);
        MG_CHECK(observer->GetEnv()->m_Target == nullptr);
        session.ground_command(500, 200);
        const auto intent = session.robot(1);
        MG_CHECK(intent.order == mpo_MoveTo);
        MG_CHECK(observer->GetEnv()->m_Target == patient);
        float health = patient->GetHitPoint();
        int repairs = 0;
        for (int i = 0; i < 1800; ++i) {
            const auto *target_before_tick = observer->GetEnv()->m_Target;
            session.advance();
            const auto state = session.robot(1);
            MG_CHECK(state.order == mpo_MoveTo);
            MG_CHECK(state.destination_x == intent.destination_x && state.destination_y == intent.destination_y);
            if (patient && patient->GetHitPoint() != health) {
                MG_CHECK(observer->m_PosX > 100);
                MG_CHECK(target_before_tick == patient);
                MG_CHECK(patient->GetHitPoint() == float(std::min(50, 32 + 4 * (repairs + 1))) / 10.0f);
                health = patient->GetHitPoint();
                ++repairs;
                if (retire && repairs == 2) {
                    g_MatrixMap->StaticDelete(patient);
                    patient = nullptr;
                    MG_CHECK(observer->GetEnv()->m_Target == nullptr);
                }
            }
        }
        MG_CHECK(repairs == (retire ? 2 : 5));
        if (patient) MG_CHECK(patient->GetHitPoint() == 5 && !patient->NeedRepair());
        MG_CHECK(observer->GetEnv()->m_Target == nullptr);
        MG_CHECK(!observer->FindOrderLikeThat(ROT_FIRE));
        MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        MG_CHECK(observer->m_PosX == 500 && observer->m_PosY == 200);
        evidence(session, retire ? "moving-repair-patient-retired" : "moving-repair-completed", observer);
    }
}
}
void repair_movement_completion() { repair_movement(false); }
void repair_movement_retirement() { repair_movement(true); }
namespace {
void repair_competing_run(bool replace_order, bool competitor_in_range = false) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        BattleCombatAssets assets;
        battle::Profile profile{mode, 84, step, 3000, false};
        profile.layout = NavigationLayout::Open;
        battle::Session session(profile);
        assets.install();
        auto *observer = owner();
        g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
        configure_repair(*observer);
        g_Config.m_RobotDamages[Weap2Index(WEAPON_REPAIR)] = {8, 4, 0};
        auto injure = [](float x, float y) {
            auto *patient = damaged_friendly(x, y);
            g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {17, 17, 0};
            MG_CHECK(!patient->Damage(WEAPON_LASER, patient->GetGeoCenter(), D3DXVECTOR3(1, 0, 0), 2, nullptr));
            MG_CHECK(patient->GetHitPoint() == 3.2f);
            return patient;
        };
        // The competitor comes first in logic-list order, so retention differs from rescanning.
        auto *second = injure(210, 220);
        auto *first = injure(160, 140);
        const auto release = [](SObjectCore *core) { core->Release(); };
        std::unique_ptr<SObjectCore, decltype(release)> first_core(first->GetCore(DEBUG_CALL_INFO), release);
        competing_repair::Recording recording(session, mode, step, replace_order, competitor_in_range,
                                               observer, first, second, first_core.get());
        recording.state("initial", 0, 0, first->GetHitPoint(), second->GetHitPoint(), false, 0);
        const auto initial_draws = random::draw_count();
        session.ground_command(500, 200);
        recording.command("move", initial_draws);
        MG_CHECK(observer->GetEnv()->m_Target == first);
        recording.state("after-move", 0, 0, first->GetHitPoint(), second->GetHitPoint(), false,
                        recording.identity(observer->GetEnv()->m_Target));
        auto intent = session.robot(1);
        int first_pulses = 0, second_pulses = 0;
        float first_health = first->GetHitPoint(), second_health = second->GetHitPoint();
        bool changed = false;
        while (session.time() < 25000) {
            const auto *target_before = observer->GetEnv()->m_Target;
            const int recorded_target = recording.identity(observer->GetEnv()->m_Target);
            session.advance();
            const auto state = session.robot(1);
            MG_CHECK(state.order == mpo_MoveTo);
            MG_CHECK(state.destination_x == intent.destination_x && state.destination_y == intent.destination_y);
            if (first && first->GetHitPoint() != first_health) {
                if (changed && competitor_in_range) MG_CHECK(second_pulses == 5);
                MG_CHECK(target_before == first);
                ++first_pulses;
                first_health = first->GetHitPoint();
                MG_CHECK(first_health == float(std::min(50, 32 + first_pulses * 4)) / 10.0f);
            }
            if (second->GetHitPoint() != second_health) {
                MG_CHECK(target_before == second);
                MG_CHECK(changed && (!replace_order || competitor_in_range || first_pulses == 5));
                ++second_pulses;
                second_health = second->GetHitPoint();
                MG_CHECK(second_health == float(std::min(50, 32 + second_pulses * 4)) / 10.0f);
            }
            if (!changed && first_pulses == (competitor_in_range ? 3 : 2)) {
                recording.state("before-change", first_pulses, second_pulses, first_health, second_health,
                                changed, recorded_target);
                const auto change_draws = random::draw_count();
                MG_CHECK(observer->m_PosX > 100 && observer->GetEnv()->m_Target == first);
                MG_CHECK(second_pulses == 0);
                if (replace_order) {
                    const float dx = observer->m_PosX - second->m_PosX;
                    const float dy = observer->m_PosY - second->m_PosY;
                    MG_CHECK((dx * dx + dy * dy < 10000) == competitor_in_range);
                    session.ground_command(300, 500);
                    intent = session.robot(1);
                    MG_CHECK(intent.destination_x == 28 && intent.destination_y == 48);
                    MG_CHECK(observer->GetEnv()->m_Target == (competitor_in_range ? second : first));
                } else {
                    g_MatrixMap->StaticDelete(first);
                    first = nullptr;
                    MG_CHECK(!first_core->m_Object && !observer->GetEnv()->m_Target);
                }
                recording.command(replace_order ? "replace-move" : "retire-patient", change_draws);
                changed = true;
                recording.state("after-change", first_pulses, second_pulses, first_health, second_health,
                                changed, recorded_target);
            }
            if (changed && !replace_order) MG_CHECK(!first_core->m_Object);
            recording.state("tick", first_pulses, second_pulses, first_health, second_health,
                            changed, recorded_target);
        }
        MG_CHECK(changed && first_pulses == (replace_order ? 5 : 2) && second_pulses == 5);
        MG_CHECK(second->GetHitPoint() == 5 && !second->NeedRepair());
        if (replace_order) MG_CHECK(first_core->m_Object == first && first->GetHitPoint() == 5 && !first->NeedRepair());
        MG_CHECK(session.robot(1).in_place && session.robot(2).in_place);
        MG_CHECK(observer->m_PosX == (replace_order ? 300 : 500));
        MG_CHECK(observer->m_PosY == (replace_order ? 500 : 200));
        MG_CHECK(!observer->GetEnv()->m_Target && !observer->FindOrderLikeThat(ROT_FIRE));
        recording.state("final", first_pulses, second_pulses, first_health, second_health, changed, 0);
        recording.finish();
        std::cout << session.metadata("competing-moving-repair-v2") << " replace=" << replace_order << " competitor_in_range=" << competitor_in_range
                  << " first_pulses=" << first_pulses << " second_pulses=" << second_pulses << '\n';
    }
}
}
void repair_competing_retirement() {
    competing_repair::invalidate(false);
    repair_competing_run(false);
}
void repair_replacement_move() {
    competing_repair::invalidate(true);
    repair_competing_run(true);
    repair_competing_run(true, true);
}

namespace {
enum class CombatJourney { Move, Patrol, RetireTarget, ReplaceMove };

void combat_continuation(CombatJourney journey) {
    for (auto mode : {random::Mode::ParkMiller, random::Mode::LegacyCRT}) for (int step : {10, 16}) {
        const char *name = journey == CombatJourney::Move ? "move" : journey == CombatJourney::Patrol ? "patrol"
            : journey == CombatJourney::RetireTarget ? "retire-target" : "replace-move";
        BattleCombatAssets assets;
        battle::Profile profile{mode, 84, step, 1600, false};
        profile.layout = NavigationLayout::Open;
        battle::Session session(profile);
        assets.install();
        MG_CHECK(session.remove(2));
        auto *observer = owner();
        g_Config.m_WeaponRadius[Weap2Index(WEAPON_LASER)] = 160;
        g_Config.m_WeaponCooldown[Weap2Index(WEAPON_LASER)] = 100;
        g_Config.m_RobotDamages[Weap2Index(WEAPON_LASER)] = {2, 0, 0};
        g_Config.m_Overheat[WEAPON_LASER_HEAT] = 10;
        g_Config.m_Overheat[WEAPON_LASER_COOL] = 10;
        g_Config.m_Overheat[WCP_LASER] = 100;
        g_Config.m_ItemChars[ARMOR1_ROTATION_SPEED] = 0.04f;
        arm_battle_robot(*observer, RUK_WEAPON_LASER);
        CMatrixRobotAI *target = nullptr, *sentinel = nullptr;
        for (auto *object = CMatrixMapStatic::GetFirstLogic(); object; object = object->GetNextLogic()) {
            if (!object->IsLiveRobot()) continue;
            if (session.matches(3, object->AsRobot())) target = object->AsRobot();
            if (session.matches(4, object->AsRobot())) sentinel = object->AsRobot();
        }
        MG_CHECK(target && sentinel);
        const auto position = [&](CMatrixRobotAI *robot, float x, float y) {
            robot->m_PosX = x; robot->m_PosY = y;
            robot->SetMaxSpeed(0); robot->m_GroupSpeed = 0;
            robot->MapPosCalc(); robot->RChange(MR_Matrix); robot->RNeed(MR_Matrix);
            robot->m_CalcBoundsLastTime = g_MatrixMap->GetTime() - 100;
            robot->JoinToGroup();
        };
        position(target, 240, 100);
        // Keep a live opponent outside this journey so killing the target cannot end the battle.
        position(sentinel, 600, 600);
        target->InitMaxHitpoint(40);
        const float sentinel_health = sentinel->GetHitPoint();
        const auto release = [](SObjectCore *core) { core->Release(); };
        std::unique_ptr<SObjectCore, decltype(release)> core(target->GetCore(DEBUG_CALL_INFO), release);
        auto &side = *g_MatrixMap->GetPlayerSide();
        if (journey == CombatJourney::Move) session.ground_command(500, 100);
        else {
            g_MatrixData->BlockGet(L"Chars")->BlockGet(L"ChassisSounds")->BlockGet(L"2")
                ->ParSetAdd(L"Patrol", L"Sound.Synthetic");
            side.PGOrderPatrol(side.RobotToLogicGroup(observer), CPoint(48, 8));
        }
        auto intent = session.robot(1);
        MG_CHECK(intent.order == (journey == CombatJourney::Move ? mpo_MoveTo : mpo_Patrol));
        MG_CHECK(intent.destination_x == 48 && intent.destination_y == 8);
        MG_CHECK(observer->GetEnv()->GetEnemyCnt() == 0);
        MG_CHECK(!observer->GetEnv()->m_TargetAttack && !observer->FindOrderLikeThat(ROT_FIRE));
        CPoint initial_move;
        MG_CHECK(observer->GetMoveToCoords(initial_move));
        MG_CHECK(initial_move == (journey == CombatJourney::Move ? CPoint(48, 8) : CPoint(8, 8)));
        int acquisition = -1, first_war = -1, first_hit = -1, terminal = -1, changed = -1, arrival = -1;
        int redirected = -1, pulses = 0;
        int first_failed_search = -1, last_failed_search = -1, failed_searches = 0, resumed_at = -1, idle_at = -1;
        float health = 4;
        bool resumed = false;
        MG_CHECK(observer->GetEnv()->m_PlaceNotFound < 0);
        // TaktPL runs at most once per 100 ms; a frame can overshoot that cadence.
        const int retry_observation_slack = 100 + step;
        const auto check_retry_window = [&](int event_time) {
            std::cout << "retry-window journey=" << name << " rng=" << int(mode) << " step_ms=" << step
                      << " failed_at=" << last_failed_search << " event_at=" << event_time << '\n';
            MG_CHECK(last_failed_search >= 0);
            MG_CHECK(event_time - last_failed_search > 2000);
            MG_CHECK(event_time - last_failed_search <= 2000 + retry_observation_slack);
        };
        while (session.time() < 15000) {
            const auto *firing_target = observer->GetEnv()->m_Target;
            session.advance();
            const auto state = session.robot(1);
            MG_CHECK(state.alive);
            MG_CHECK(state.order == intent.order);
            MG_CHECK(state.destination_x == intent.destination_x && state.destination_y == intent.destination_y);
            MG_CHECK(sentinel->IsLiveRobot() && sentinel->GetHitPoint() == sentinel_health);
            MG_CHECK(sentinel->m_PosX == 600 && sentinel->m_PosY == 600);
            const auto &group = side.m_PlayerGroup[observer->GetGroupLogic()];
            const auto &env = *observer->GetEnv();
            if (journey == CombatJourney::Move || (changed >= 0 && journey == CombatJourney::ReplaceMove))
                MG_CHECK(!group.IsWar());
            if (group.IsWar() && first_war < 0) first_war = session.time();
            if (group.IsWar()) {
                // War never obtains a firing place in this occupied-place fixture.
                MG_CHECK(env.m_Place < 0);
                if (session.time() - first_war >= retry_observation_slack) MG_CHECK(env.m_PlaceNotFound >= 0);
            }
            if (env.m_PlaceNotFound >= 0 && env.m_PlaceNotFound != last_failed_search) {
                MG_CHECK(journey != CombatJourney::Move && group.IsWar() && env.m_Place < 0);
                MG_CHECK(env.m_PlaceNotFound == session.time());
                MG_CHECK(core->m_Object && target->IsLiveRobot());
                const int occupied_place = target->GetEnv()->m_Place;
                MG_CHECK(occupied_place >= 0 && occupied_place < g_MatrixMap->m_RN.m_PlaceCnt);
                MG_CHECK(g_MatrixMap->m_RN.m_Place[occupied_place].m_Pos == CPoint(8, 8));
                if (last_failed_search >= 0) check_retry_window(session.time());
                else first_failed_search = session.time();
                last_failed_search = env.m_PlaceNotFound;
                ++failed_searches;
            }
            if (observer->GetEnv()->m_TargetAttack == target && acquisition < 0) acquisition = session.time();
            if (core->m_Object && target->GetHitPoint() < health) {
                MG_CHECK(acquisition > 0 && firing_target == target);
                MG_CHECK(target->m_PosX == 240 && target->m_PosY == 100);
                ++pulses;
                health = target->GetHitPoint();
                MG_CHECK(std::fabs(health - float(40 - pulses * 2) / 10.0f) < 0.00001f);
                if (first_hit < 0) {
                    first_hit = session.time();
                    MG_CHECK(observer->m_PosX != 100 || observer->m_PosY != 200);
                }
            }
            if ((!core->m_Object || !target->IsLiveRobot()) && terminal < 0) terminal = session.time();
            if (terminal >= 0 && session.time() > terminal + step) {
                MG_CHECK(!observer->GetEnv()->m_Target && !observer->GetEnv()->m_TargetAttack);
                MG_CHECK(observer->GetEnv()->GetEnemyCnt() == 0 && !observer->FindOrderLikeThat(ROT_FIRE));
                if (session.time() > terminal + 200) MG_CHECK(!group.IsWar());
            }
            if (changed < 0 && pulses == 2 && (journey == CombatJourney::RetireTarget || journey == CombatJourney::ReplaceMove)) {
                MG_CHECK(group.IsWar() && observer->GetEnv()->m_TargetAttack == target);
                MG_CHECK(last_failed_search >= 0 && session.time() - last_failed_search <= 2000);
                CPoint previous_move;
                MG_CHECK(observer->GetMoveToCoords(previous_move) && previous_move == CPoint(8, 8));
                changed = session.time();
                if (journey == CombatJourney::RetireTarget) {
                    MG_CHECK(session.remove(3));
                    MG_CHECK(!core->m_Object && !observer->GetEnv()->m_Target && !observer->GetEnv()->m_TargetAttack);
                    MG_CHECK(observer->GetEnv()->GetEnemyCnt() == 0);
                } else {
                    session.ground_command(500, 400);
                    intent = session.robot(1);
                    MG_CHECK(intent.order == mpo_MoveTo && intent.destination_x == 48 && intent.destination_y == 38);
                    // This command lands inside the failed-place-search retry window.
                    CPoint after_command;
                    MG_CHECK(observer->GetMoveToCoords(after_command) && after_command == previous_move);
                    MG_CHECK(!side.m_PlayerGroup[observer->GetGroupLogic()].IsWar());
                    MG_CHECK(observer->GetEnv()->m_PlaceNotFound == last_failed_search);
                    MG_CHECK(observer->FindOrderLikeThat(ROT_FIRE));
                }
            }
            CPoint move;
            if (observer->GetMoveToCoords(move)) {
                if (journey == CombatJourney::Move) MG_CHECK(move == CPoint(48, 8));
                if (terminal >= 0 && move == CPoint(48, 8) && !resumed) {
                    resumed = true;
                    resumed_at = session.time();
                    if (journey != CombatJourney::Move) check_retry_window(resumed_at);
                }
                if (journey == CombatJourney::ReplaceMove && changed >= 0 && move == CPoint(48, 38) && redirected < 0) {
                    redirected = session.time();
                    check_retry_window(redirected);
                }
                if (journey != CombatJourney::Move && last_failed_search >= 0 &&
                    session.time() - last_failed_search <= 2000)
                    MG_CHECK(move == initial_move);
            }
            if (journey == CombatJourney::Patrol && terminal >= 0 && !resumed &&
                observer->m_PosX == 100 && observer->m_PosY == 100 && !observer->FindOrderLikeThat(ROT_MOVE_TO) && idle_at < 0)
                idle_at = session.time();
            const int destination_y = journey == CombatJourney::ReplaceMove ? 400 : 100;
            // Patrol may start its return before stopping exactly at the outbound point.
            const bool moving = journey == CombatJourney::Move || journey == CombatJourney::ReplaceMove;
            const bool arrived = moving ? state.in_place && observer->m_PosX == 500 && observer->m_PosY == destination_y
                : observer->m_PosX > 490 && std::fabs(observer->m_PosY - destination_y) < 5;
            if (arrived) {
                arrival = session.time();
                break;
            }
        }
        MG_CHECK(acquisition > 0 && first_hit >= acquisition && terminal > first_hit && arrival > terminal);
        MG_CHECK(!observer->GetEnv()->m_Target && !observer->GetEnv()->m_TargetAttack);
        MG_CHECK(!observer->FindOrderLikeThat(ROT_FIRE));
        MG_CHECK(first_war == -1 || journey != CombatJourney::Move);
        if (journey != CombatJourney::Move) MG_CHECK(first_war > 0 && first_war < first_hit);
        if (journey == CombatJourney::RetireTarget) {
            MG_CHECK(pulses == 2 && changed > first_hit && terminal == changed + step && !core->m_Object);
        } else MG_CHECK(pulses == 20 && health == 0);
        if (journey == CombatJourney::ReplaceMove) MG_CHECK(redirected > changed && redirected < arrival);
        else MG_CHECK(resumed);
        if (journey == CombatJourney::Move) MG_CHECK(failed_searches == 0);
        else {
            MG_CHECK(first_failed_search > first_war && first_failed_search < first_hit);
            MG_CHECK(failed_searches == (journey == CombatJourney::Patrol ? 2 : 1));
            if (journey == CombatJourney::Patrol) MG_CHECK(idle_at > terminal && idle_at < resumed_at);
        }
        std::cout << session.metadata("combat-journey-v2") << " journey=" << name
                  << " acquisition=" << acquisition << " first_war=" << first_war << " first_hit=" << first_hit << " terminal=" << terminal
                  << " changed=" << changed << " redirected=" << redirected << " arrival=" << arrival
                  << " failed_searches=" << failed_searches << " first_failed_search=" << first_failed_search
                  << " last_failed_search=" << last_failed_search << " idle_at=" << idle_at << " resumed_at=" << resumed_at
                  << " pulses=" << pulses << " final=" << observer->m_PosX << ',' << observer->m_PosY
                  << " rng_draws=" << random::draw_count() << '\n';
    }
}
}
void moving_combat_continuation() { combat_continuation(CombatJourney::Move); }
void patrol_combat_continuation() { combat_continuation(CombatJourney::Patrol); }
void patrol_combat_retirement() { combat_continuation(CombatJourney::RetireTarget); }
void patrol_combat_replacement() { combat_continuation(CombatJourney::ReplaceMove); }

}
